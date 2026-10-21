#include <xc.h>
#include "board.h"
#include "buttons.h"
#include "hal.h"

#pragma config FOSC = INTOSCIO
#pragma config WDTE = OFF
#pragma config PWRTE = ON
#pragma config MCLRE = OFF
#pragma config CP = OFF
#pragma config CPD = OFF
#pragma config BOREN = ON
#pragma config IESO = OFF
#pragma config FCMEN = OFF

static volatile uint8_t pending_ticks;
static uint8_t gpio_shadow;
/* Packed Q3 register layout with bit 0 always zero: two fractional bits in
   bits 1..2 and DC1B in bits 3..4. This keeps the ISR shift to one instruction.
   Publish with TMR2IE disabled. Only breathing produces fractional targets. */
static volatile uint8_t pwm_high, pwm_low;
static uint8_t dither; /* Preserve the two-bit residual across target changes. */

void __interrupt() timer_isr(void)
{
    if (PIE1bits.TMR2IE && PIR1bits.TMR2IF) {
        PIR1bits.TMR2IF = 0;
        dither = (dither & 6u) + pwm_low;
        CCPR1L = pwm_high;
        if (dither & 32u) ++CCPR1L;
        CCP1CON = (uint8_t)(0x0cu | ((dither & 24u) << 1));
        if (!(pwm_low & 6u)) PIE1bits.TMR2IE = 0;
    }
    if (INTCONbits.T0IF) {
        INTCONbits.T0IF = 0;
        if (pending_ticks != 255u) ++pending_ticks;
    }
}

void hw_init(void)
{
    INTCON = 0;
    PIE1 = 0;
    OSCCON = 0x71; /* HFINTOSC 8 MHz, internal system clock */
    ANSEL = 0;
    ADCON0 = 0;
    CMCON0 = 0x07;
    VRCON = 0;
    TRISIO = 0x3f; /* Initialize latches before enabling output drivers. */
    gpio_shadow = BOARD_PWM_ACTIVE_HIGH ? 0 : BOARD_PWM_MASK;
    /* Start with the status indicator extinguished. */
#if !BOARD_STATUS_ACTIVE_HIGH
    gpio_shadow |= BOARD_LED_MASK;
#endif
    GPIO = gpio_shadow;
    WPU = BOARD_UP_MASK | BOARD_DOWN_MASK;
    IOC = 0;
    /* nGPPU=0, T0CS=0, PSA=0, PS=010 (1:8). */
    OPTION_REG = 0x02;
    T1CON = 0;
    CCP1CON = 0;
    CCPR1L = 0;
    PR2 = BOARD_PWM_PR2;
    TMR2 = 0;
    T2CON = 0x0c; /* 20 kHz PWM, postscale 1:2 for 10 kHz dither updates. */
    TRISIO = BOARD_INPUT_MASK;
    EECON1 = 0;
    pending_ticks = 0;
    pwm_high = 0xffu; /* Invalid high byte forces the first render. */
    pwm_low = 0; /* The updater starts idle, including after explicit re-init. */
    TMR0 = 0;
    INTCONbits.T0IF = 0;
    INTCONbits.T0IE = 1;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;
    WDTCON = BOARD_WATCHDOG_CONTROL;
    CLRWDT();
}

uint8_t hw_take_tick(void)
{
    uint8_t available;
    INTCONbits.GIE = 0;
    available = pending_ticks != 0;
    if (available) --pending_ticks;
    INTCONbits.GIE = 1;
    return available;
}

uint8_t hw_buttons(void)
{
    uint8_t raw = GPIO;
#if BOARD_BUTTONS_ACTIVE_LOW
    raw = (uint8_t)~raw;
#endif
    /* GP3/4/5 map consecutively to POWER/UP/DOWN bits 0/1/2. */
    return (uint8_t)((raw >> 3) & (BUTTON_POWER | BUTTON_BRIGHTNESS));
}

void hw_render(uint16_t duty, uint8_t indicator)
{
    uint8_t high, low;
#if BOARD_INDICATOR_ONLY
    duty = 0;
#endif
    /* Round the Q3 envelope to two fractional bits (Q2), not to an integer. */
    duty = (duty + 1u) & 0xfffeu;
    if (indicator == BOARD_STATUS_ACTIVE_HIGH) gpio_shadow |= BOARD_LED_MASK;
    else gpio_shadow &= (uint8_t)~BOARD_LED_MASK;
    /* Full GPIO writes avoid read-modify-write of a live CCP1 output. */
    GPIO = gpio_shadow;
    high = (uint8_t)(duty >> 5);
    low = (uint8_t)duty & 31u;
    if (high == pwm_high && low == pwm_low) return;
    PIE1bits.TMR2IE = 0; /* Publish both duty bytes atomically to the ISR. */
    /* Fractional targets have a running updater: retain pending boundaries.
       Only an idle integer target can leave a stale flag to discard. */
    if (!(pwm_low & 6u)) PIR1bits.TMR2IF = 0;
    pwm_high = high; pwm_low = low;
    if (duty == 0 || duty >= BOARD_PWM_PERIOD * 8u) {
#if BOARD_PWM_ACTIVE_HIGH
        if (duty) gpio_shadow |= BOARD_PWM_MASK;
#else
        if (!duty) gpio_shadow |= BOARD_PWM_MASK;
#endif
        else gpio_shadow &= (uint8_t)~BOARD_PWM_MASK;
        GPIO = gpio_shadow;
        CCP1CON = 0; /* Static endpoint gives true 0% and 100%. */
    } else {
        uint8_t polls = 0;
#if !BOARD_PWM_ACTIVE_HIGH
#error PWM currently requires an active-high output
#endif
        /* Wait for a postscaled PWM boundary before enabling the updater.
           Both duty fields then finish before the following 50 us latch. */
        while (!PIR1bits.TMR2IF) {
            /* A byte-sized budget bounds the wait even if Timer0 also stops. */
            if (--polls == 0) {
#if BOARD_PWM_ACTIVE_HIGH
                gpio_shadow &= (uint8_t)~BOARD_PWM_MASK;
#else
                gpio_shadow |= BOARD_PWM_MASK;
#endif
                GPIO = gpio_shadow;
                CCP1CON = 0;
                /* Leave the output off until the independent watchdog resets. */
                for (;;) { NOP(); }
            }
        }
        /* Integer targets (all steady/editor levels) stop after one IRQ. */
        PIE1bits.TMR2IE = 1;
    }
}

#if !BOARD_INDICATOR_ONLY
#ifndef __XC8
uint8_t hal_eeprom_busy(void) { return EECON1bits.WR; }
#endif

uint8_t hal_eeprom_read(uint8_t address)
{
    EEADR = address;
    EECON1bits.RD = 1;
    return EEDAT;
}

void hal_eeprom_start_write(uint8_t address, uint8_t value)
{
    uint8_t interrupts = INTCON;
    EEADR = address;
    EEDAT = value;
    EECON1bits.WREN = 1;
    INTCONbits.GIE = 0;
    EECON2 = 0x55;
    EECON2 = 0xaa;
    EECON1bits.WR = 1;
    EECON1bits.WREN = 0;
    if (interrupts & 0x80u) INTCONbits.GIE = 1;
}
#endif
