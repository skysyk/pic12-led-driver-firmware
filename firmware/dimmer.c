#include "board.h"
#include "dimmer.h"
static dimmer_state_t current;
static uint8_t brightness_blocked;

void dimmer_init(const dimmer_state_t *s)
{
    current = *s;
    brightness_blocked = 0;
}

const dimmer_state_t *dimmer_state(void) { return &current; }

uint16_t dimmer_duty(uint8_t percent)
{
    /* round(percent * 1023 / 100), with no 32-bit arithmetic. */
    return (uint16_t)((uint16_t)percent * 10u +
                      ((uint16_t)percent * 23u + 50u) / 100u);
}

uint16_t dimmer_output(void)
{
    return current.power_on ? dimmer_duty(current.brightness) : 0;
}

uint8_t dimmer_tick(const button_events_t *e)
{
    uint8_t amount, action, previous, was_on;
    if ((e->down & BUTTON_BRIGHTNESS) == BUTTON_BRIGHTNESS ||
        ((e->down | e->released) & BUTTON_POWER))
        brightness_blocked = 1;
    /* Power cancels overlapping brightness gestures, including release events
       and later repeats. Re-arm only after all brightness buttons are up. */
    if (brightness_blocked) {
        if (!(e->down & BUTTON_BRIGHTNESS)) brightness_blocked = 0;
        if (!(e->short_press & BUTTON_POWER)) return 0;
    }
    if (e->short_press & BUTTON_POWER) {
        current.power_on ^= 1u;
        return DIMMER_CHANGED | DIMMER_POWER_CHANGED;
    }
    if ((e->down | e->pressed | e->held) & BUTTON_POWER) return 0;
    action = (uint8_t)((e->short_press | e->repeat) & BUTTON_BRIGHTNESS);
    if (!action || action == BUTTON_BRIGHTNESS) return 0;
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
