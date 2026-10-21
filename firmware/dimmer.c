#include "board.h"
#include "dimmer.h"
#include <stddef.h>

#define EVENT(member) in[offsetof(button_events_t, member)]
static dimmer_state_t current;
static uint8_t blocked; /* Bits 0/1: gesture blocks; bits 2..4: ramp repeats to skip. */
static uint16_t breath_phase;
/* Animation and editor blinking are mutually exclusive; share the timer storage. */
static union { uint16_t fraction; uint8_t blink; } breath_clock;
#define breath_time breath_clock.fraction
static uint16_t mode_flash;
static uint16_t editor_hold;
/* Bits 0..1: 0 normal, 1 minimum, 2 maximum; bit 2: maximum's dark blink phase. */
static uint8_t editor;
static uint8_t reset_state; /* 1: eligible, 2/3: armed LED on/off, 4: completed. */

/* Simulator exponential: (exp(-cos(2*pi*phase/2048)) - exp(-1)) /
   (exp(1) - exp(-1)). Symmetry needs only 33 Q8 knots for the rising half.
   The final zero encodes 256; its preceding wrapped delta is still +1. */
static const uint8_t breath_knots[33] = {
    0, 0, 1, 2, 3, 5, 7, 10, 14, 18, 22, 28, 34, 41, 50, 59,
    69, 80, 92, 106, 120, 134, 150, 165, 181, 196, 210, 223,
    234, 244, 250, 255, 0
};

/* Full cycle, phase 0..2047; reflect the exhale onto the rising half. */
static uint16_t exponential_level(uint16_t phase, uint8_t span)
{
    uint8_t index, base;
    uint16_t level;
    if (phase & 1024u) phase = BREATHE_PHASE_STEPS - phase;
    index = (uint8_t)(phase >> 5);
    if (index == 32u) level = 8192u;
    else {
        base = breath_knots[index];
        level = ((uint16_t)base << 5) +
                (uint16_t)(uint8_t)(breath_knots[index + 1u] - base) * (phase & 31u);
    }
    /* Q13 * span / 256 gives Q3 counts with two bounded 16-bit products. */
    return (level >> 8) * span + (((uint16_t)(uint8_t)level * span + 128u) >> 8);
}

void dimmer_defaults(dimmer_state_t *s)
{
    uint8_t *out = (uint8_t *)s;
    /* Settings are five consecutive bytes; advance one RAM address per field. */
    *out++ = DIMMER_DEFAULT_PERCENT;
    *out++ = 0;
    *out++ = BREATHE_DEFAULT_PERIOD;
    *out++ = DIMMER_MIN_PERCENT;
    *out = DIMMER_MAX_PERCENT;
}

#ifdef __XC8
void dimmer_copy_state(uint8_t destination, uint8_t source)
{
    uint8_t n = sizeof(dimmer_state_t);
    do {
        *(uint8_t *)(uintptr_t)destination++ = *(uint8_t *)(uintptr_t)source++;
    } while (--n);
}
#else
void dimmer_copy_state(uint8_t *destination, const uint8_t *source)
{
    uint8_t n = sizeof(dimmer_state_t);
    do { *destination++ = *source++; } while (--n);
}
#endif

void dimmer_init(const dimmer_state_t *s)
{
    DIMMER_COPY(&current, s);
    blocked = 0;
    breath_phase = 0;
    breath_time = 0;
    mode_flash = 0;
    editor_hold = editor = 0;
    reset_state = 0;
}

#ifdef FIRMWARE_TEST
uint8_t dimmer_editing(void) { return editor & 3u; }
uint16_t dimmer_phase(void) { return breath_phase; }
uint16_t dimmer_envelope(uint16_t phase)
{
    return exponential_level(phase, 128u);
}
#endif

#if !BOARD_INDICATOR_ONLY || defined(FIRMWARE_TEST)
const dimmer_state_t *dimmer_state(void) { return &current; }
#endif

uint16_t dimmer_output(void)
{
    uint8_t span, percent = current.brightness;
    uint16_t level;
    if (!current.power_on) return 0;
    if (editor == 1u) percent = current.minimum;
    else if (editor) percent = current.maximum;
    if (editor || !(current.breath & BREATHE_MODE)) {
        if (percent == DIMMER_LOWEST || (!editor && !percent)) return 8;
        return dimmer_duty(percent) << 3;
    }
    percent = current.minimum & 127u;
    span = current.maximum - percent;
    level = (dimmer_duty(percent) << 3) + exponential_level(breath_phase, span);
    if (level < 8u && current.minimum == DIMMER_LOWEST) level = 8;
    return level;
}

uint8_t dimmer_status_led(void)
{
    if (editor) return !(editor & 4u);
    return (uint8_t)(reset_state == 2u || mode_flash);
}

uint8_t dimmer_tick(const button_events_t *e)
{
    const uint8_t *in = (const uint8_t *)e;
    uint8_t amount, action, previous, was_on, changed = 0;
    uint8_t low = 0, high = DIMMER_MAX_PERCENT, value;
    uint8_t *level = &current.brightness;
    uint16_t period;
    if (mode_flash && --mode_flash == 0) changed = DIMMER_RENDER;
    if ((editor & 2u) && --breath_clock.blink == 0) {
        breath_clock.blink = STATUS_EDITOR_BLINK_TICKS;
        editor ^= 4u;
        changed = DIMMER_RENDER;
    }
    if (EVENT(pressed) & BUTTON_POWER) reset_state = current.power_on ? 0 : 1;
    if (EVENT(released) & BUTTON_POWER) {
        if (reset_state >= 2u) changed = DIMMER_RENDER;
        reset_state = 0;
    }
    if (reset_state == 1u && (EVENT(power_hold) & POWER_RESET_ARM)) {
        reset_state = 2;
        breath_time = 0;
        changed = DIMMER_RENDER;
    } else if (reset_state == 2u || reset_state == 3u) {
        if (++breath_time >= STATUS_BLINK_TICKS) {
            breath_time = 0;
            reset_state ^= 1u;
            changed = DIMMER_RENDER;
        }
        if (EVENT(power_hold) & POWER_RESET_COMMIT) {
            dimmer_defaults(&current);
            reset_state = 4;
            return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
        }
    }
    if (current.power_on && (current.breath & BREATHE_MODE) && !editor) {
        breath_time += BREATHE_PHASE_STEPS;
        period = (uint16_t)(current.breath & BREATHE_PERIOD_MASK) * BREATHE_STEP_TICKS;
        while (breath_time >= period) {
            breath_time -= period;
            breath_phase = (breath_phase + 1u) & (BREATHE_PHASE_STEPS - 1u);
            changed = DIMMER_RENDER;
        }
    }
    if (EVENT(pressed) & BUTTON_BRIGHTNESS) blocked &= 3u;
    /* Only releasing both brightness buttons rearms a completed or
       power-cancelled editor gesture. */
    if (!(blocked & 2u) && current.power_on && (current.breath & BREATHE_MODE) &&
        EVENT(down) == BUTTON_BRIGHTNESS) {
        if (++editor_hold == BUTTON_EDITOR_TICKS) {
            blocked |= 2u;
            if (editor) {
                editor = 0;
                breath_phase = 0;
                breath_time = 0;
                return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
            }
            editor = 1;
            return DIMMER_RENDER;
        }
    } else editor_hold = 0;
    if ((EVENT(down) | EVENT(released)) & BUTTON_POWER)
        blocked = 3;
    else if ((EVENT(down) & BUTTON_BRIGHTNESS) == BUTTON_BRIGHTNESS)
        blocked |= 1u;
    /* Power cancels overlapping brightness gestures, including release events
       and later repeats. Re-arm only after all brightness buttons are up. */
    if (blocked & 1u) {
        if (!(EVENT(down) & BUTTON_BRIGHTNESS)) blocked = 0;
        if (!((EVENT(short_press) | EVENT(held)) & BUTTON_POWER)) return changed;
    }
    if ((EVENT(held) & BUTTON_POWER) && current.power_on && !editor) {
        current.breath ^= BREATHE_MODE;
        mode_flash = STATUS_MODE_FLASH_TICKS;
        value = current.minimum & 127u;
        if (current.brightness <= value) breath_phase = 0;
        else if (current.brightness >= current.maximum) breath_phase = BREATHE_PEAK_PHASE;
        else {
            /* Search the same monotone rising envelope near saved brightness. */
            action = current.maximum - value;
            breath_time = (uint16_t)(current.brightness - value) * 32u;
            low = 0;
            /* 256 four-phase positions span the rising half up to phase 1020. */
            high = 128;
            do {
                value = low | high;
                if (exponential_level((uint16_t)value << 2, action) <= breath_time) low = value;
            } while ((high >>= 1) != 0);
            breath_phase = (uint16_t)low << 2;
        }
        breath_time = 0;
        return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
    }
    if (EVENT(short_press) & BUTTON_POWER) {
        if (editor) {
            editor = (uint8_t)(3u - (editor & 3u));
            breath_clock.blink = STATUS_EDITOR_BLINK_TICKS;
            return DIMMER_RENDER;
        }
        current.power_on ^= 1u;
        mode_flash = 0;
        breath_phase = 0;
        breath_time = 0;
        return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    }
    if ((EVENT(down) | EVENT(pressed) | EVENT(held)) & BUTTON_POWER) return changed;
    action = (uint8_t)((EVENT(short_press) | EVENT(repeat)) & BUTTON_BRIGHTNESS);
    if (!action || action == BUTTON_BRIGHTNESS) return changed;
    amount = (EVENT(repeat) & action) ? DIMMER_RAMP_PERCENT : DIMMER_STEP_PERCENT;
    /* Taps retain the 10% boundary; fine holds can reach every whole percent. */
    if (!EVENT(repeat)) low = DIMMER_MIN_PERCENT;
    if (!editor && (current.breath & BREATHE_MODE)) {
        if (action == BUTTON_UP) {
            if ((current.breath & BREATHE_PERIOD_MASK) == BREATHE_MAX_PERIOD)
                current.breath -= BREATHE_MAX_PERIOD - 1u;
            else ++current.breath;
        } else {
            if ((current.breath & BREATHE_PERIOD_MASK) == 1u)
                current.breath += BREATHE_MAX_PERIOD - 1u;
            else --current.breath;
        }
        breath_time = 0;
        was_on = current.power_on;
        current.power_on = 1;
        if (!was_on) {
            breath_phase = 0;
            return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
        }
        return DIMMER_CHANGED; /* Every period step changes or wraps. */
    }
    /* Apply the first hold step, then skip four repeats for 102.4 ms steps.
       Period controls above retain their cadence. New presses clear the count. */
    if (EVENT(repeat)) {
        if (blocked & 28u) {
            blocked -= 4u;
            return changed;
        }
        blocked |= (BRIGHTNESS_REPEAT_DIVIDER - 1u) << 2;
    }
    if (editor == 1u) {
        level = &current.minimum;
        low = 0;
        high = (uint8_t)(current.maximum - 1u);
    } else if (editor) {
        level = &current.maximum;
        low = (uint8_t)((current.minimum & 127u) + 1u);
    }
    previous = value = *level;
    value &= 127u;
    was_on = current.power_on;
    if (action == BUTTON_UP) {
        value += amount; /* Valid levels and steps <=100 cannot overflow. */
        if (value > high) value = high;
    } else {
        if (value < low + amount) value = low;
        else value -= amount;
    }
    if (!editor && !EVENT(repeat) && previous <= DIMMER_MIN_PERCENT && action == BUTTON_DOWN)
        value = 0;
    else if (editor == 1u) {
        /* Insert the one-count level next to off. Repeats cannot leave it
           downward; only a distinct short Down press can select off. */
        if ((!previous && action == BUTTON_UP) ||
            (!value && previous && (previous != DIMMER_LOWEST ||
                                   !(EVENT(short_press) & BUTTON_DOWN)))) value = DIMMER_LOWEST;
    }
    *level = value;
    if (editor) return DIMMER_RENDER;
    current.power_on = 1;
    if (!was_on) return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    return previous != value ? DIMMER_CHANGED : changed;
}
