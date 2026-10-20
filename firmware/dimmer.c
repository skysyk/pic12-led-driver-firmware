#include "board.h"
#include "dimmer.h"
#include <stddef.h>

#define EVENT(member) in[offsetof(button_events_t, member)]
static dimmer_state_t current;
static uint8_t brightness_blocked;
static uint16_t breath_phase;
/* Animation and editor blinking are mutually exclusive; share the timer storage. */
static union { uint16_t fraction; uint8_t blink; } breath_clock;
#define breath_time breath_clock.fraction
static uint16_t mode_flash;
static uint16_t editor_hold;
static uint8_t editor_blocked;
/* Bits 0..1: 0 normal, 1 minimum, 2 maximum; bit 2: maximum's dark blink phase. */
static uint8_t editor;
static uint8_t reset_state; /* 1: eligible, 2/3: armed LED on/off, 4: completed. */

/* Q9 half-cosine, first quarter sampled every 64 of 2048 phases.
   The last byte encodes 256 modulo 256; its preceding delta is still 50.
   The exact quarter endpoint is handled separately to retain all 513 weights. */
static const uint8_t sine_quarter[9] = {
    0, 5, 19, 43, 75, 114, 158, 206, 0
};

/* Rising half only, phase 0..1024. */
static uint16_t sine_level(uint16_t phase)
{
    uint8_t index, base, invert;
    invert = phase > 512u;
    if (invert) phase = 1024u - phase;
    index = (uint8_t)(phase >> 6);
    if (index == 8u) return 256u;
    base = sine_quarter[index];
    phase &= 63u;
    if (phase)
        base += (uint8_t)(((uint16_t)(uint8_t)(sine_quarter[index + 1u] - base) *
                           phase + 32u) >> 6);
    return invert ? 512u - base : base;
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

void dimmer_copy_state(uint8_t *destination, const uint8_t *source)
{
    uint8_t n = sizeof(dimmer_state_t);
    do { *destination++ = *source++; } while (--n);
}

void dimmer_init(const dimmer_state_t *s)
{
    DIMMER_COPY(&current, s);
    brightness_blocked = 0;
    breath_phase = 0;
    breath_time = 0;
    mode_flash = 0;
    editor_hold = editor = 0;
    editor_blocked = 0;
    reset_state = 0;
}

#ifdef FIRMWARE_TEST
uint8_t dimmer_editing(void) { return editor & 3u; }
uint16_t dimmer_phase(void) { return breath_phase; }
uint16_t dimmer_sine(uint16_t phase)
{
    return sine_level(phase > 1024u ? BREATHE_PHASE_STEPS - phase : phase);
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
    if (editor) percent = editor == 1u ? current.minimum : current.maximum;
    if (editor || !(current.breath & BREATHE_MODE)) return dimmer_duty(percent);
    level = sine_level(breath_phase > 1024u ?
                       BREATHE_PHASE_STEPS - breath_phase : breath_phase);
    span = current.maximum - current.minimum;
    /* Q9 times four counts/percent: dividing by 128 keeps the product in 16 bits. */
    return dimmer_duty(current.minimum) + ((level * span + 64u) >> 7);
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
    uint8_t low = DIMMER_MIN_PERCENT, high = DIMMER_MAX_PERCENT, value;
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
    /* Only releasing both brightness buttons rearms a completed or
       power-cancelled editor gesture. */
    if (!editor_blocked && current.power_on && (current.breath & BREATHE_MODE) &&
        EVENT(down) == BUTTON_BRIGHTNESS) {
        if (++editor_hold == BUTTON_EDITOR_TICKS) {
            editor_blocked = 1;
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
        brightness_blocked = editor_blocked = 1;
    else if ((EVENT(down) & BUTTON_BRIGHTNESS) == BUTTON_BRIGHTNESS)
        brightness_blocked = 1;
    /* Power cancels overlapping brightness gestures, including release events
       and later repeats. Re-arm only after all brightness buttons are up. */
    if (brightness_blocked) {
        if (!(EVENT(down) & BUTTON_BRIGHTNESS)) brightness_blocked = editor_blocked = 0;
        if (!((EVENT(short_press) | EVENT(held)) & BUTTON_POWER)) return changed;
    }
    if ((EVENT(held) & BUTTON_POWER) && current.power_on && !editor) {
        current.breath ^= BREATHE_MODE;
        mode_flash = STATUS_MODE_FLASH_TICKS;
        if (current.brightness <= current.minimum) breath_phase = 0;
        else if (current.brightness >= current.maximum) breath_phase = 1024;
        else {
            /* Invert the same rising sine envelope to enter near the saved
               steady brightness, rather than treating sine phase as linear. */
            action = current.maximum - current.minimum;
            breath_time = (uint16_t)(current.brightness - current.minimum) * 512u;
            low = 0;
            /* Search phase in four-position increments with byte-sized bounds. */
            high = 255;
            while (low < high) {
                value = high - low;
                value = (uint8_t)(low + (value >> 1) + (value & 1u));
                if (sine_level((uint16_t)value << 2) * action > breath_time) high = value - 1u;
                else low = value;
            }
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
    if (editor == 1u) {
        level = &current.minimum;
        low = 0;
        high = (uint8_t)(current.maximum - 1u);
    } else if (editor) {
        level = &current.maximum;
        low = (uint8_t)(current.minimum + 1u);
    }
    previous = value = *level;
    was_on = current.power_on;
    if (action == BUTTON_UP) {
        value += amount; /* Valid levels and steps <=100 cannot overflow. */
        if (value > high) value = high;
    } else {
        if (value < low + amount) value = low;
        else value -= amount;
    }
    *level = value;
    if (editor) return DIMMER_RENDER;
    current.power_on = 1;
    if (!was_on) return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    return previous != value ? DIMMER_CHANGED : changed;
}
