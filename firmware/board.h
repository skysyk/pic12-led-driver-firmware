#ifndef BOARD_H
#define BOARD_H

/* QY-K4 assumptions still requiring bench verification are explicit here. */
#define BOARD_BUTTONS_ACTIVE_LOW 1
#define BOARD_PWM_ACTIVE_HIGH 1
/* QY-K4 indicator cathode connects to GP0; anode goes via R7 to VDD. */
#define BOARD_STATUS_ACTIVE_HIGH 0
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
/* 8 MHz / (4 * 1 * 100) = 20,000 Hz; 400 duty intervals. */
#define BOARD_PWM_PR2 99u
#define BOARD_PWM_PERIOD (4u * (BOARD_PWM_PR2 + 1u))
/* Timer0 free-runs at Fosc/4, prescaler 1:8: 1024 us per overflow. */
#define TICKS_FROM_MS(ms) (((ms) * 1000UL + 1023UL) / 1024UL)
#define BUTTON_DEBOUNCE_TICKS TICKS_FROM_MS(20)
#define BUTTON_HOLD_TICKS TICKS_FROM_MS(400)
#define BUTTON_RESET_ARM_TICKS TICKS_FROM_MS(3000)
#define BUTTON_RESET_COMMIT_TICKS TICKS_FROM_MS(6000)
#define STATUS_BLINK_TICKS TICKS_FROM_MS(500)
#define STATUS_MODE_FLASH_TICKS TICKS_FROM_MS(500)
#define STATUS_EDITOR_BLINK_TICKS TICKS_FROM_MS(250)
/* LFINTOSC/32768: about 1.06 s, software enabled; config stays 0x33C4. */
#define BOARD_WATCHDOG_CONTROL 0x15u
#define BUTTON_RAMP_TICKS TICKS_FROM_MS(20)
/* Brightness consumes one in five repeats: 102.4 ms. Period buttons use all. */
#define BRIGHTNESS_REPEAT_DIVIDER 5u
#define BRIGHTNESS_RAMP_TICKS (BRIGHTNESS_REPEAT_DIVIDER * BUTTON_RAMP_TICKS)
#if BRIGHTNESS_REPEAT_DIVIDER < 1 || BRIGHTNESS_REPEAT_DIVIDER > 8
#error Brightness repeat divider must fit the three-bit gesture counter
#endif
#define BUTTON_MODE_TICKS TICKS_FROM_MS(1000)
#define BUTTON_EDITOR_TICKS TICKS_FROM_MS(1000)
/* 488 Timer0 ticks = 499.712 ms; period is 1..20 half-second steps. */
#define BREATHE_STEP_TICKS 488u
/* Exponential envelope: 2048 phases, Q3 calculation, Q2 output dithering. */
#define BREATHE_PHASE_STEPS 2048u
#define BREATHE_PEAK_PHASE 1024u
#define BREATHE_DEFAULT_PERIOD 6u
#define BREATHE_MAX_PERIOD 20u
#if BREATHE_PHASE_STEPS != 2048u || BREATHE_MAX_PERIOD * BREATHE_STEP_TICKS + BREATHE_PHASE_STEPS > 65535u
#error Breathing timing requires 2048 phases and a sixteen-bit accumulator
#endif
#define BREATHE_MODE 128u
#define BREATHE_PERIOD_MASK 31u
#define STORAGE_DELAY_TICKS TICKS_FROM_MS(900)
#define STORAGE_RETRY_TICKS TICKS_FROM_MS(1000)
#define DIMMER_MIN_PERCENT 10u /* Coarse-step boundary. */
#define DIMMER_MAX_PERCENT 100u
#define DIMMER_DEFAULT_PERCENT 50u
#define DIMMER_STEP_PERCENT 10u
#define DIMMER_RAMP_PERCENT 1u
/* Minimum-editor sentinel: one PWM count, between 0% and 1%. */
#define DIMMER_LOWEST 128u

#if DIMMER_MIN_PERCENT < 1 || DIMMER_MAX_PERCENT > 100 || DIMMER_MIN_PERCENT >= DIMMER_MAX_PERCENT
#error Invalid brightness limits
#endif
#if DIMMER_DEFAULT_PERCENT < DIMMER_MIN_PERCENT || DIMMER_DEFAULT_PERCENT > DIMMER_MAX_PERCENT
#error Invalid default brightness
#endif
#if DIMMER_STEP_PERCENT < 1 || DIMMER_RAMP_PERCENT < 1 || DIMMER_STEP_PERCENT > 100 || DIMMER_RAMP_PERCENT > 100
#error Brightness increments must be positive
#endif
#if BUTTON_DEBOUNCE_TICKS < 1 || BUTTON_DEBOUNCE_TICKS > 255 || BUTTON_RAMP_TICKS < 1 || BUTTON_RAMP_TICKS > 255
#error Button debounce and ramp intervals must fit eight-bit counters
#endif
#if BUTTON_HOLD_TICKS < 1 || BUTTON_HOLD_TICKS > 65535 || STORAGE_DELAY_TICKS > 65535 || STORAGE_RETRY_TICKS > 65535
#error Hold and storage intervals must fit sixteen-bit counters
#endif
#if BUTTON_MODE_TICKS <= BUTTON_HOLD_TICKS || BUTTON_RESET_ARM_TICKS <= BUTTON_MODE_TICKS || BUTTON_RESET_COMMIT_TICKS <= BUTTON_RESET_ARM_TICKS || BUTTON_RESET_COMMIT_TICKS > 65535
#error Reset hold must exceed the normal hold and fit a sixteen-bit counter
#endif
#if BOARD_PWM_PERIOD != 400u
#error Percent-to-duty scaling requires a 400-count PWM period
#endif
#if STATUS_MODE_FLASH_TICKS < 1 || STATUS_MODE_FLASH_TICKS > 65535
#error Mode flash interval must fit a sixteen-bit counter
#endif
#if STATUS_EDITOR_BLINK_TICKS < 1 || STATUS_EDITOR_BLINK_TICKS > 255
#error Editor blink interval must fit the shared byte counter
#endif
#endif
