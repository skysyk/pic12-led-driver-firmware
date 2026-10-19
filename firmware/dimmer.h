#ifndef DIMMER_H
#define DIMMER_H
#include <stdint.h>
#include "buttons.h"
typedef struct {
    uint8_t brightness, power_on;
    uint8_t breath; /* Bit 7: breathing enabled; bits 0..4: period in 0.5 s steps. */
    uint8_t minimum, maximum;
} dimmer_state_t;
/* State changes request persistence; power/preferences save immediately. */
#define DIMMER_CHANGED 1u
#define DIMMER_POWER_CHANGED 2u
#define DIMMER_PREFERENCE_CHANGED 4u
#define DIMMER_RENDER 8u /* Animation only: never request an EEPROM save. */
#define DIMMER_IMMEDIATE_SAVE (DIMMER_POWER_CHANGED | DIMMER_PREFERENCE_CHANGED)
void dimmer_init(const dimmer_state_t *state);
void dimmer_defaults(dimmer_state_t *state);
uint8_t dimmer_tick(const button_events_t *events);
const dimmer_state_t *dimmer_state(void);
uint8_t dimmer_duty(uint8_t percent);
uint8_t dimmer_output(void);
uint8_t dimmer_status_led(void);
#ifdef FIRMWARE_TEST
uint8_t dimmer_editing(void);
#endif
#endif
