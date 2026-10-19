#include "board.h"
#include "buttons.h"

static uint8_t stable, long_press;
static uint8_t debounce[3], repeat_count[3];
static uint16_t duration[3];

void buttons_init(void)
{
    uint8_t i;
    stable = long_press = 0;
    for (i = 0; i < 3; ++i) {
        debounce[i] = repeat_count[i] = 0;
        duration[i] = 0;
    }
}

void buttons_tick(uint8_t raw, button_events_t *e)
{
    uint8_t i, mask;
    e->pressed = e->released = e->short_press = e->held = e->repeat = 0;
    for (i = 0, mask = 1; i < 3; ++i, mask <<= 1) {
        if ((raw ^ stable) & mask) {
            if (++debounce[i] >= BUTTON_DEBOUNCE_TICKS) {
                debounce[i] = 0;
                stable ^= mask;
                if (stable & mask) {
                    e->pressed |= mask;
                    duration[i] = 0;
                    repeat_count[i] = 0;
                    long_press &= (uint8_t)~mask;
                } else {
                    e->released |= mask;
                    if (!(long_press & mask)) e->short_press |= mask;
                }
            }
        } else debounce[i] = 0;

        /* Pause hold timing while the physical contact is being released. */
        if ((stable & raw & mask) && !(e->pressed & mask)) {
            if (!(long_press & mask)) {
                if (++duration[i] >= BUTTON_HOLD_TICKS) {
                    long_press |= mask;
                    e->held |= mask;
                    e->repeat |= mask;
                }
            } else if (++repeat_count[i] >= BUTTON_RAMP_TICKS) {
                repeat_count[i] = 0;
                e->repeat |= mask;
            }
        }
    }
    e->down = stable;
}
