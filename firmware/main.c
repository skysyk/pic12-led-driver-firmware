#include <xc.h>
#include "board.h"
#include "hal.h"
#include "buttons.h"
#include "dimmer.h"
#include "storage.h"

void main(void)
{
    dimmer_state_t restored;
    button_events_t events;
    uint8_t changed;
    hw_init();
#if BOARD_INDICATOR_ONLY
    /* The bench image never reads or changes persistent settings. */
    dimmer_defaults(&restored);
#else
    storage_init(&restored);
#endif
    dimmer_init(&restored);
    hw_render(dimmer_output(), dimmer_status_led());
    for (;;) {
        if (hw_take_tick()) {
#if !BOARD_INDICATOR_ONLY
            storage_tick();
#endif
            buttons_tick(hw_buttons(), &events);
            changed = dimmer_tick(&events);
            if (changed) {
#if !BOARD_INDICATOR_ONLY
                if (changed & DIMMER_CHANGED)
                    storage_request(dimmer_state(), changed & DIMMER_IMMEDIATE_SAVE);
#endif
                hw_render(dimmer_output(), dimmer_status_led());
            }
            hw_watchdog_feed(); /* Only completed ticks demonstrate progress. */
        }
#if !BOARD_INDICATOR_ONLY
        storage_service();
#endif
    }
}
