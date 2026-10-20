#include "board.h"
#include "buttons.h"
#include <stddef.h>

#define EVENT(member) out[offsetof(button_events_t, member)]

static uint8_t stable, long_press;
static uint8_t debounce[3], repeat_count[3];
static uint16_t duration[3];

/* C startup clears this state on every real reset. Tests reuse the process. */
#ifdef FIRMWARE_TEST
void buttons_init(void)
{
    uint8_t i;
    stable = long_press = 0;
    for (i = 0; i < 3; ++i) {
        debounce[i] = repeat_count[i] = 0;
        duration[i] = 0;
    }
}
#endif

void buttons_tick(uint8_t raw, button_events_t *e)
{
    uint8_t i, mask;
    uint16_t elapsed;
    uint8_t *out = (uint8_t *)e;
    /* Byte access keeps event addresses eight-bit on PIC; offsetof preserves layout. */
    for (i = 0; i < sizeof *e; ++i) out[i] = 0;
    for (i = 0, mask = 1; i < 3; ++i, mask <<= 1) {
        elapsed = duration[i];
        if ((raw ^ stable) & mask) {
            if (++debounce[i] >= BUTTON_DEBOUNCE_TICKS) {
                debounce[i] = 0;
                stable ^= mask;
                if (stable & mask) {
                    EVENT(pressed) |= mask;
                    elapsed = 0;
                    repeat_count[i] = 0;
                    long_press &= (uint8_t)~mask;
                } else {
                    EVENT(released) |= mask;
                    if (!(long_press & mask)) EVENT(short_press) |= mask;
                }
            }
        } else debounce[i] = 0;

        /* Pause hold timing while the physical contact is being released. */
        if ((stable & raw & mask) && !(EVENT(pressed) & mask)) {
            if (!(long_press & mask)) {
                if (++elapsed >= (mask == BUTTON_POWER ?
                                  (uint16_t)BUTTON_MODE_TICKS : (uint16_t)BUTTON_HOLD_TICKS)) {
                    long_press |= mask;
                    EVENT(held) |= mask;
                    EVENT(repeat) |= mask;
                }
            } else if (mask == BUTTON_POWER) {
                /* Continue timing power after the normal hold suppresses its
                   short-press event; saturate so a long hold cannot retrigger. */
                if (elapsed < BUTTON_RESET_COMMIT_TICKS) {
                    ++elapsed;
                    if (elapsed == BUTTON_RESET_ARM_TICKS) EVENT(power_hold) = POWER_RESET_ARM;
                    if (elapsed == BUTTON_RESET_COMMIT_TICKS) EVENT(power_hold) = POWER_RESET_COMMIT;
                }
            } else if (++repeat_count[i] >= BUTTON_RAMP_TICKS) {
                repeat_count[i] = 0;
                EVENT(repeat) |= mask;
            }
        }
        duration[i] = elapsed;
    }
    EVENT(down) = stable;
}
