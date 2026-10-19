#include "board.h"
#include "dimmer.h"
static dimmer_state_t current;
static uint8_t brightness_blocked;
static uint8_t breath_phase;
static uint16_t breath_time, power_wait;

void dimmer_init(const dimmer_state_t *s)
{
    current = *s;
    brightness_blocked = 0;
    breath_phase = 0;
    breath_time = power_wait = 0;
}

#if !BOARD_INDICATOR_ONLY || defined(FIRMWARE_TEST)
const dimmer_state_t *dimmer_state(void) { return &current; }
#endif

uint16_t dimmer_duty(uint8_t percent)
{
    /* round(percent * 1023 / 100), with no 32-bit arithmetic. */
    return (uint16_t)((uint16_t)percent * 10u +
                      ((uint16_t)percent * 23u + 50u) / 100u);
}

uint16_t dimmer_output(void)
{
    uint8_t phase;
    if (!current.power_on) return 0;
    if (!(current.breath & BREATHE_MODE)) return dimmer_duty(current.brightness);
    phase = breath_phase;
    if (phase & 128u) phase = (uint8_t)(0u - phase);
    return dimmer_duty((uint8_t)(DIMMER_MIN_PERCENT +
        ((uint16_t)phase * (DIMMER_MAX_PERCENT - DIMMER_MIN_PERCENT) + 64u) / 128u));
}

uint8_t dimmer_standby_led(void)
{
    return (uint8_t)(!current.power_on && !current.standby_led_disabled);
}

uint8_t dimmer_tick(const button_events_t *e)
{
    uint8_t amount, action, previous, was_on, changed = 0;
    uint16_t period;
    if (current.power_on && (current.breath & BREATHE_MODE)) {
        breath_time += 256u;
        period = (uint16_t)(current.breath & BREATHE_PERIOD_MASK) * BREATHE_STEP_TICKS;
        if (breath_time >= period) {
            breath_time -= period;
            ++breath_phase;
            changed = DIMMER_RENDER;
        }
    }
    if (power_wait && --power_wait == 0) {
        current.power_on = 0;
        return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    }
    if ((e->down & BUTTON_BRIGHTNESS) == BUTTON_BRIGHTNESS ||
        ((e->down | e->released) & BUTTON_POWER))
        brightness_blocked = 1;
    if ((e->long_held & BUTTON_POWER) && !current.power_on && !current.standby_led_disabled) {
        current.standby_led_disabled = 1;
        return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
    }
    /* Power cancels overlapping brightness gestures, including release events
       and later repeats. Re-arm only after all brightness buttons are up. */
    if (brightness_blocked) {
        if (!(e->down & BUTTON_BRIGHTNESS)) brightness_blocked = 0;
        if (!(e->short_press & BUTTON_POWER)) return changed;
    }
    if (e->short_press & BUTTON_POWER) {
        if (current.power_on) {
            if (power_wait) {
                power_wait = 0;
                current.breath ^= BREATHE_MODE;
                breath_time = 0;
                /* Approximate the inverse ramp with a compile-time scale;
                   avoid another runtime division on this small target. */
                breath_phase = (uint8_t)(((uint16_t)(current.brightness - DIMMER_MIN_PERCENT) *
                    ((8192u + (DIMMER_MAX_PERCENT - DIMMER_MIN_PERCENT) / 2u) /
                     (DIMMER_MAX_PERCENT - DIMMER_MIN_PERCENT)) + 32u) >> 6);
                return DIMMER_CHANGED | DIMMER_PREFERENCE_CHANGED;
            }
            power_wait = BUTTON_DOUBLE_TICKS;
            return changed;
        }
        current.power_on = 1;
        breath_phase = 0;
        breath_time = 0;
        return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    }
    if ((e->down | e->pressed | e->held) & BUTTON_POWER) return changed;
    action = (uint8_t)((e->short_press | e->repeat) & BUTTON_BRIGHTNESS);
    if (!action || action == BUTTON_BRIGHTNESS) return changed;
    if (power_wait) return changed; /* Finish the power gesture first. */
    if (current.breath & BREATHE_MODE) {
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
    amount = (e->repeat & action) ? DIMMER_RAMP_PERCENT : DIMMER_STEP_PERCENT;
    previous = current.brightness;
    was_on = current.power_on;
    if (action == BUTTON_UP) {
        if ((uint16_t)current.brightness + amount >= DIMMER_MAX_PERCENT)
            current.brightness = DIMMER_MAX_PERCENT;
        else current.brightness += amount;
    } else {
        if (current.brightness <= DIMMER_MIN_PERCENT + amount)
            current.brightness = DIMMER_MIN_PERCENT;
        else current.brightness -= amount;
    }
    current.power_on = 1;
    if (!was_on) return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    return previous != current.brightness ? DIMMER_CHANGED : 0;
}
