#include <xc.h>
#include "board.h"
#include "hal.h"
#include "buttons.h"
#include "dimmer.h"
#include "storage.h"

void main(void)
{
    /* Restored settings are consumed before the first button sample. */
    union { dimmer_state_t restored; button_events_t events; } input;
    uint8_t changed;
    hw_init();
#if BOARD_INDICATOR_ONLY
    /* The bench image never reads or changes persistent settings. */
    dimmer_defaults(&input.restored);
#else
    storage_init(&input.restored);
#endif
    dimmer_init(&input.restored);
    hw_render(dimmer_output(), dimmer_status_led());
    for (;;) {
        if (hw_take_tick()) {
#if !BOARD_INDICATOR_ONLY
            storage_tick();
#endif
            buttons_tick(hw_buttons(), &input.events);
            changed = dimmer_tick(&input.events);
#if !BOARD_INDICATOR_ONLY
            if (changed & DIMMER_CHANGED)
                storage_request(dimmer_state(), changed & DIMMER_IMMEDIATE_SAVE);
#endif
#if !BOARD_INDICATOR_ONLY
            changed = storage_status_led(dimmer_status_led());
#else
            changed = dimmer_status_led();
#endif
            hw_render(dimmer_output(), changed);
            hw_watchdog_feed(); /* Only completed ticks demonstrate progress. */
        }
#if !BOARD_INDICATOR_ONLY
        storage_service();
#endif
    }
}
