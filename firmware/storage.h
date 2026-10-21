#ifndef STORAGE_H
#define STORAGE_H
#include "dimmer.h"
void storage_init(dimmer_state_t *restored);
void storage_request(const dimmer_state_t *state, uint8_t immediate);
void storage_tick(void);
/* Nonblocking: at most one EEPROM byte is started per call. */
void storage_service(void);
/* Override normal indication with a rapid fault blink; nonzero means lit. */
uint8_t storage_status_led(uint8_t normal);
#ifdef FIRMWARE_TEST
/* Unsaved retirement or exhausted capacity; cleared by a verified usable save. */
uint8_t storage_fault(void);
uint8_t storage_busy(void);
uint16_t storage_retired(void);
uint8_t storage_stopped(void);
#endif
#endif
