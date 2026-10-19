#ifndef STORAGE_H
#define STORAGE_H
#include "dimmer.h"
void storage_init(dimmer_state_t *restored);
void storage_request(const dimmer_state_t *state, uint8_t immediate);
void storage_tick(void);
/* Nonblocking: at most one EEPROM byte is started per call. */
void storage_service(void);
#ifdef FIRMWARE_TEST
uint8_t storage_busy(void);
uint8_t storage_fault(void);
#endif
#endif
