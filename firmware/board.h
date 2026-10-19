#ifndef BOARD_H
#define BOARD_H

/* QY-K4 assumptions still requiring bench verification are explicit here. */
#define BOARD_BUTTONS_ACTIVE_LOW 1
#define BOARD_PWM_ACTIVE_HIGH 1
#define BOARD_STANDBY_ACTIVE_HIGH 1
#ifndef BOARD_INDICATOR_ONLY
#define BOARD_INDICATOR_ONLY 0
#endif

#define BOARD_POWER_MASK 0x08u /* GP3, pin 4: external 10k pull-up required */
#define BOARD_UP_MASK    0x10u /* GP4, pin 3 */
#define BOARD_DOWN_MASK  0x20u /* GP5, pin 2 */
#define BOARD_PWM_MASK   0x04u /* GP2/CCP1, pin 5 */
#define BOARD_LED_MASK   0x01u /* GP0, pin 7 */
#define BOARD_INPUT_MASK 0x3au /* GP1 remains high impedance */

#define _XTAL_FREQ 8000000UL
/* 8 MHz / (4 * 1 * 53) = 37,735.849 Hz (nearest to 38 kHz). */
#define BOARD_PWM_PR2 52u
#define BOARD_PWM_PERIOD (4u * (BOARD_PWM_PR2 + 1u))
/* Timer0 free-runs at Fosc/4, prescaler 1:8: 1024 us per overflow. */
#define TICKS_FROM_MS(ms) (((ms) * 1000UL + 1023UL) / 1024UL)
#define BUTTON_DEBOUNCE_TICKS TICKS_FROM_MS(20)
#define BUTTON_HOLD_TICKS TICKS_FROM_MS(400)
#define BUTTON_STANDBY_HOLD_TICKS TICKS_FROM_MS(5000)
#define BUTTON_RAMP_TICKS TICKS_FROM_MS(20)
#define STORAGE_DELAY_TICKS TICKS_FROM_MS(900)
#define STORAGE_RETRY_TICKS TICKS_FROM_MS(1000)
#define DIMMER_MIN_PERCENT 10u
#define DIMMER_MAX_PERCENT 100u
#define DIMMER_DEFAULT_PERCENT 50u
#define DIMMER_STEP_PERCENT 10u
#define DIMMER_RAMP_PERCENT 1u

#if DIMMER_MIN_PERCENT < 1 || DIMMER_MAX_PERCENT > 100 || DIMMER_MIN_PERCENT >= DIMMER_MAX_PERCENT
#error Invalid brightness limits
#endif
#if DIMMER_DEFAULT_PERCENT < DIMMER_MIN_PERCENT || DIMMER_DEFAULT_PERCENT > DIMMER_MAX_PERCENT
#error Invalid default brightness
#endif
#if DIMMER_STEP_PERCENT < 1 || DIMMER_RAMP_PERCENT < 1
#error Brightness increments must be positive
#endif
#if BUTTON_DEBOUNCE_TICKS < 1 || BUTTON_DEBOUNCE_TICKS > 255 || BUTTON_RAMP_TICKS < 1 || BUTTON_RAMP_TICKS > 255
#error Button debounce and ramp intervals must fit eight-bit counters
#endif
#if BUTTON_HOLD_TICKS < 1 || BUTTON_HOLD_TICKS > 65535 || STORAGE_DELAY_TICKS > 65535 || STORAGE_RETRY_TICKS > 65535
#error Hold and storage intervals must fit sixteen-bit counters
#endif
#if BUTTON_STANDBY_HOLD_TICKS <= BUTTON_HOLD_TICKS || BUTTON_STANDBY_HOLD_TICKS > 65535
#error Standby hold must exceed the normal hold and fit a sixteen-bit counter
#endif
#endif
