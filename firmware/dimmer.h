#ifndef DIMMER_H
#define DIMMER_H
#include <stdint.h>
#include "buttons.h"
typedef struct { uint8_t brightness, power_on; } dimmer_state_t;
/* Return flags let the foreground request prompt or deferred persistence. */
#define DIMMER_CHANGED 1u
#define DIMMER_POWER_CHANGED 2u
void dimmer_init(const dimmer_state_t *state);
uint8_t dimmer_tick(const button_events_t *events);
const dimmer_state_t *dimmer_state(void);
uint16_t dimmer_duty(uint8_t percent);
uint16_t dimmer_output(void);
#endif
