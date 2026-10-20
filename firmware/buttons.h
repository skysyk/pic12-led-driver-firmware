#ifndef BUTTONS_H
#define BUTTONS_H
#include <stdint.h>
#define BUTTON_POWER 1u
#define BUTTON_UP 2u
#define BUTTON_DOWN 4u
#define BUTTON_BRIGHTNESS (BUTTON_UP | BUTTON_DOWN)
#define POWER_RESET_ARM 1u
#define POWER_RESET_COMMIT 2u
typedef struct {
    uint8_t down, pressed, released, short_press, held, repeat, power_hold;
} button_events_t;
#ifdef FIRMWARE_TEST
void buttons_init(void);
#endif
/* Logical pressed mask, independent of GPIO polarity. Call once per tick. */
void buttons_tick(uint8_t raw, button_events_t *events);
#endif
