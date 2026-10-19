#include "board.h"
#include "dimmer.h"
static dimmer_state_t current;
static uint8_t brightness_blocked;
static uint8_t breath_phase;
static uint16_t breath_time;
static uint16_t mode_flash;
static uint16_t editor_hold;
static uint8_t editor_blocked;
static uint8_t editor; /* 0: normal, 1: minimum preview, 2: maximum preview. */
static uint8_t reset_state; /* 1: eligible, 2/3: armed LED on/off, 4: completed. */

void dimmer_defaults(dimmer_state_t *s)
{
    s->brightness = DIMMER_DEFAULT_PERCENT;
    s->power_on = 0;
    s->breath = BREATHE_DEFAULT_PERIOD;
    s->minimum = DIMMER_MIN_PERCENT;
    s->maximum = DIMMER_MAX_PERCENT;
}

void dimmer_init(const dimmer_state_t *s)
{
    current = *s;
    brightness_blocked = 0;
    breath_phase = 0;
    breath_time = 0;
    mode_flash = 0;
    editor_hold = editor = 0;
    editor_blocked = 0;
    reset_state = 0;
}

#ifdef FIRMWARE_TEST
uint8_t dimmer_editing(void) { return editor; }
#endif

#if !BOARD_INDICATOR_ONLY || defined(FIRMWARE_TEST)
const dimmer_state_t *dimmer_state(void) { return &current; }
#endif

uint8_t dimmer_duty(uint8_t percent)
{
    /* Convert directly to the 212-count hardware period, avoiding a second
       conversion from legacy 10-bit units. EEPROM v4 already stores percent. */
    return (uint8_t)(((uint16_t)percent * BOARD_PWM_PERIOD + 50u) / 100u);
}

uint8_t dimmer_output(void)
{
    uint8_t phase;
    if (!current.power_on) return 0;
    if (editor) return dimmer_duty(editor == 1u ? current.minimum : current.maximum);
    if (!(current.breath & BREATHE_MODE)) return dimmer_duty(current.brightness);
    phase = breath_phase;
    if (phase & 128u) phase = (uint8_t)(0u - phase);
    return dimmer_duty((uint8_t)(current.minimum +
        ((uint16_t)phase * (current.maximum - current.minimum) + 64u) / 128u));
}

uint8_t dimmer_status_led(void)
{
    return (uint8_t)(editor || reset_state == 2u || mode_flash);
}

uint8_t dimmer_tick(const button_events_t *e)
{
    uint8_t amount, action, previous, was_on, changed = 0;
    uint8_t low = DIMMER_MIN_PERCENT, high = DIMMER_MAX_PERCENT, value;
    uint8_t *level = &current.brightness;
    uint16_t period;
    if (mode_flash && --mode_flash == 0) changed = DIMMER_RENDER;
    if (e->pressed & BUTTON_POWER) reset_state = current.power_on ? 0 : 1;
    if (e->released & BUTTON_POWER) {
        if (reset_state >= 2u) changed = DIMMER_RENDER;
        reset_state = 0;
    }
    if (reset_state == 1u && (e->power_hold & POWER_RESET_ARM)) {
        reset_state = 2;
        breath_time = 0;
        changed = DIMMER_RENDER;
    } else if (reset_state == 2u || reset_state == 3u) {
        if (++breath_time >= STATUS_BLINK_TICKS) {
            breath_time = 0;
            reset_state ^= 1u;
            changed = DIMMER_RENDER;
        }
        if (e->power_hold & POWER_RESET_COMMIT) {
            dimmer_defaults(&current);
            reset_state = 4;
            return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
        }
    }
    if (current.power_on && (current.breath & BREATHE_MODE) && !editor) {
        breath_time += 256u;
        period = (uint16_t)(current.breath & BREATHE_PERIOD_MASK) * BREATHE_STEP_TICKS;
        if (breath_time >= period) {
            breath_time -= period;
            ++breath_phase;
            changed = DIMMER_RENDER;
        }
    }
    /* Only releasing both brightness buttons rearms a completed or
       power-cancelled editor gesture. */
    if (!editor_blocked && current.power_on && (current.breath & BREATHE_MODE) &&
        e->down == BUTTON_BRIGHTNESS) {
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
    if ((e->down | e->released) & BUTTON_POWER)
        brightness_blocked = editor_blocked = 1;
    else if ((e->down & BUTTON_BRIGHTNESS) == BUTTON_BRIGHTNESS)
        brightness_blocked = 1;
    /* Power cancels overlapping brightness gestures, including release events
       and later repeats. Re-arm only after all brightness buttons are up. */
    if (brightness_blocked) {
        if (!(e->down & BUTTON_BRIGHTNESS)) brightness_blocked = editor_blocked = 0;
        if (!((e->short_press | e->held) & BUTTON_POWER)) return changed;
    }
    if ((e->held & BUTTON_POWER) && current.power_on && !editor) {
        current.breath ^= BREATHE_MODE;
        mode_flash = STATUS_MODE_FLASH_TICKS;
        breath_time = 0;
        if (current.brightness <= current.minimum) breath_phase = 0;
        else if (current.brightness >= current.maximum) breath_phase = 128;
        else breath_phase = (uint8_t)((uint16_t)(current.brightness - current.minimum) *
            128u / (current.maximum - current.minimum));
        return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
    }
    if (e->short_press & BUTTON_POWER) {
        if (editor) {
            editor = (uint8_t)(3u - editor);
            return DIMMER_RENDER;
        }
        current.power_on ^= 1u;
        mode_flash = 0;
        breath_phase = 0;
        breath_time = 0;
        return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    }
    if ((e->down | e->pressed | e->held) & BUTTON_POWER) return changed;
    action = (uint8_t)((e->short_press | e->repeat) & BUTTON_BRIGHTNESS);
    if (!action || action == BUTTON_BRIGHTNESS) return changed;
    amount = (e->repeat & action) ? DIMMER_RAMP_PERCENT : DIMMER_STEP_PERCENT;
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
    } else if (editor == 2u) {
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
