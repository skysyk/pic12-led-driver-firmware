#ifndef HAL_H
#define HAL_H
#include <stdint.h>
uint8_t hal_eeprom_read(uint8_t address);
#ifdef __XC8
#include <xc.h>
#define hal_eeprom_busy() EECON1bits.WR
#else
uint8_t hal_eeprom_busy(void);
#endif
void hal_eeprom_start_write(uint8_t address, uint8_t value);
void hw_init(void);
uint8_t hw_take_tick(void);
uint8_t hw_buttons(void);
/* Q3 duty, 0..BOARD_PWM_PERIOD*8; rounded to Q2 and dithered if fractional. */
void hw_render(uint16_t duty, uint8_t indicator);
/* Device header supplies the single-instruction watchdog clear. */
#define hw_watchdog_feed() CLRWDT()
#endif
