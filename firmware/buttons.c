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
    uint8_t *clear = (uint8_t *)e;
    /* Sequential clearing avoids repeated generic-pointer field offsets on PIC. */
    for (i = 0; i < sizeof *e; ++i) *clear++ = 0;
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
                if (++duration[i] >= (mask == BUTTON_POWER ? BUTTON_MODE_TICKS : BUTTON_HOLD_TICKS)) {
                    long_press |= mask;
                    e->held |= mask;
                    e->repeat |= mask;
                }
            } else if (mask == BUTTON_POWER) {
                /* Continue timing power after the normal hold suppresses its
                   short-press event; saturate so a long hold cannot retrigger. */
                if (duration[i] < BUTTON_RESET_COMMIT_TICKS) {
                    ++duration[i];
                    if (duration[i] == BUTTON_RESET_ARM_TICKS) e->power_hold = POWER_RESET_ARM;
                    if (duration[i] == BUTTON_RESET_COMMIT_TICKS) e->power_hold = POWER_RESET_COMMIT;
                }
            } else if (++repeat_count[i] >= BUTTON_RAMP_TICKS) {
                repeat_count[i] = 0;
                e->repeat |= mask;
            }
        }
    }
    e->down = stable;
}
