#ifndef DIMMER_H
#define DIMMER_H
#include <stdint.h>
#include "buttons.h"
typedef struct {
    uint8_t brightness, power_on;
    uint8_t breath; /* Bit 7: breathing enabled; bits 0..4: period in 0.5 s steps. */
    uint8_t minimum, maximum;
} dimmer_state_t;
typedef char dimmer_state_must_be_five_bytes[(sizeof(dimmer_state_t) == 5u) ? 1 : -1];
/* Byte pointers keep RAM addresses eight-bit on this PIC. Copy one settings state. */
void dimmer_copy_state(uint8_t *destination, const uint8_t *source);
#define DIMMER_COPY(destination, source) \
    dimmer_copy_state((uint8_t *)(destination), (const uint8_t *)(source))
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
/* Four hardware counts per percent at 20 kHz; evaluate percent once. */
#define dimmer_duty(percent) ((uint16_t)((uint16_t)(percent) << 2))
uint16_t dimmer_output(void);
uint8_t dimmer_status_led(void);
#ifdef FIRMWARE_TEST
uint8_t dimmer_editing(void);
uint16_t dimmer_phase(void);
uint16_t dimmer_sine(uint16_t phase);
#endif
#endif
