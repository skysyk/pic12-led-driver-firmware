#include <xc.h>
#include "board.h"
#include "buttons.h"
#include "hal.h"
#include "pwm.h"

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
static uint16_t last_duty;

void __interrupt() timer_isr(void)
{
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
    /* Start with standby indicator extinguished. */
    if (!BOARD_STANDBY_ACTIVE_HIGH) gpio_shadow |= BOARD_LED_MASK;
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
    T2CON = 0x04; /* Timer2 on, prescale 1:1, postscale 1:1. */
    TRISIO = BOARD_INPUT_MASK;
    EECON1 = 0;
    pending_ticks = 0;
    last_duty = 0xffffu;
    TMR0 = 0;
    INTCONbits.T0IF = 0;
    INTCONbits.T0IE = 1;
    INTCONbits.GIE = 1;
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
    uint8_t raw = GPIO, result = 0;
#if BOARD_BUTTONS_ACTIVE_LOW
    raw = (uint8_t)~raw;
#endif
    if (raw & BOARD_POWER_MASK) result |= BUTTON_POWER;
    if (raw & BOARD_UP_MASK) result |= BUTTON_UP;
    if (raw & BOARD_DOWN_MASK) result |= BUTTON_DOWN;
    return result;
}

void hw_render(uint16_t duty, uint8_t standby)
{
    uint16_t hardware_duty;
    uint8_t level;
#if BOARD_INDICATOR_ONLY
    duty = 0;
#endif
    if (standby == BOARD_STANDBY_ACTIVE_HIGH) gpio_shadow |= BOARD_LED_MASK;
    else gpio_shadow &= (uint8_t)~BOARD_LED_MASK;
    /* Full GPIO writes avoid read-modify-write of a live CCP1 output. */
    GPIO = gpio_shadow;
    if (duty == last_duty) return;
    last_duty = duty;
    if (duty == 0 || duty >= 1023u) {
        level = (uint8_t)((duty != 0) == BOARD_PWM_ACTIVE_HIGH);
        if (level) gpio_shadow |= BOARD_PWM_MASK;
        else gpio_shadow &= (uint8_t)~BOARD_PWM_MASK;
        GPIO = gpio_shadow;
        CCP1CON = 0; /* Static endpoint gives true 0% and 100%. */
    } else {
        hardware_duty = pwm_duty_counts(duty);
        if (!BOARD_PWM_ACTIVE_HIGH) hardware_duty = BOARD_PWM_PERIOD - hardware_duty;
        /* Update shortly after a PWM boundary so the two duty fields latch
           together at the following boundary. Timer2 IRQ remains disabled. */
        PIR1bits.TMR2IF = 0;
        while (!PIR1bits.TMR2IF) { }
        CCPR1L = (uint8_t)(hardware_duty >> 2);
        CCP1CON = (uint8_t)(0x0cu | ((hardware_duty & 3u) << 4));
    }
}

#if !BOARD_INDICATOR_ONLY
uint8_t hal_eeprom_busy(void) { return EECON1bits.WR; }

uint8_t hal_eeprom_read(uint8_t address)
{
    EEADR = address;
    EECON1bits.RD = 1;
    return EEDAT;
}

void hal_eeprom_start_write(uint8_t address, uint8_t value)
{
    uint8_t gie = INTCONbits.GIE;
    EEADR = address;
    EEDAT = value;
    EECON1bits.WREN = 1;
    INTCONbits.GIE = 0;
    EECON2 = 0x55;
    EECON2 = 0xaa;
    EECON1bits.WR = 1;
    EECON1bits.WREN = 0;
    if (gie) INTCONbits.GIE = 1;
}
#endif
