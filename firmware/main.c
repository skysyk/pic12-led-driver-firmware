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
    buttons_init();
#if BOARD_INDICATOR_ONLY
    /* The bench image never reads or changes persistent settings. */
    restored.brightness = DIMMER_DEFAULT_PERCENT;
    restored.power_on = 0;
#else
    storage_init(&restored);
#endif
    dimmer_init(&restored);
    hw_render(dimmer_output(), (uint8_t)!restored.power_on);
    for (;;) {
        if (hw_take_tick()) {
#if !BOARD_INDICATOR_ONLY
            storage_tick();
#endif
            buttons_tick(hw_buttons(), &events);
            changed = dimmer_tick(&events);
            if (changed) {
#if !BOARD_INDICATOR_ONLY
                storage_request(dimmer_state(), changed & DIMMER_POWER_CHANGED);
#endif
                hw_render(dimmer_output(), (uint8_t)!dimmer_state()->power_on);
            }
        }
#if !BOARD_INDICATOR_ONLY
        storage_service();
#endif
    }
}
