#ifndef BUTTONS_H
#define BUTTONS_H
#include <stdint.h>
#define BUTTON_POWER 1u
#define BUTTON_UP 2u
#define BUTTON_DOWN 4u
#define BUTTON_BRIGHTNESS (BUTTON_UP | BUTTON_DOWN)
typedef struct {
    uint8_t down, pressed, released, short_press, held, repeat, long_held;
} button_events_t;
void buttons_init(void);
/* Logical pressed mask, independent of GPIO polarity. Call once per tick. */
void buttons_tick(uint8_t raw, button_events_t *events);
#endif
