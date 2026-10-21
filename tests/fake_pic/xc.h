#ifndef TEST_XC_H
#define TEST_XC_H
#include <stdint.h>
#include <assert.h>
#include <setjmp.h>

#define __interrupt()
typedef union {
    uint8_t value;
    struct {
        unsigned GPIF:1, INTF:1, T0IF:1, GPIE:1, INTE:1, T0IE:1, PEIE:1, GIE:1;
    } bits;
} test_intcon_t;
typedef union {
    uint8_t value;
    struct { unsigned RD:1, WR:1, WREN:1; } bits;
} test_eecon_t;
typedef struct { unsigned TMR2IF:1; } test_pir1_t;
typedef union {
    uint8_t value;
    struct { unsigned unused:1, TMR2IE:1; } bits;
} test_pie1_t;

static volatile test_intcon_t test_intcon;
static volatile test_eecon_t test_eecon;
static volatile test_pir1_t test_pir1;
static volatile test_pie1_t test_pie1;
static unsigned test_timer_accesses;
static unsigned test_timer_ready_at, test_timer_running = 1;
/* A boundary can arrive immediately, after several polls, or never. */
static volatile test_pir1_t *test_timer_boundary(void)
{
    ++test_timer_accesses;
    if (test_timer_running && test_timer_accesses >= test_timer_ready_at)
        test_pir1.TMR2IF = 1;
    return &test_pir1;
}
#define INTCON test_intcon.value
#define INTCONbits test_intcon.bits
#define EECON1 test_eecon.value
#define EECON1bits test_eecon.bits
#define PIR1bits (*test_timer_boundary())
#define PIE1 test_pie1.value
#define PIE1bits test_pie1.bits
static volatile uint8_t OSCCON, ANSEL, ADCON0, CMCON0, VRCON, TRISIO;
static volatile uint8_t GPIO, WPU, IOC, OPTION_REG, T1CON, CCP1CON, CCPR1L;
static volatile uint8_t PR2, TMR2, T2CON, TMR0, EEADR, EEDAT, EECON2;
static volatile uint8_t WDTCON;
static unsigned test_watchdog_clears, test_watchdog_lf_ticks, test_watchdog_resets;
static unsigned test_expect_watchdog_reset;
static jmp_buf test_watchdog_reset_context;
static void test_watchdog_advance(unsigned ticks)
{
    if (!(WDTCON & 1u)) return;
    test_watchdog_lf_ticks += ticks;
    if (test_watchdog_lf_ticks >= 32768u) {
        assert(test_expect_watchdog_reset);
        ++test_watchdog_resets;
        longjmp(test_watchdog_reset_context, 1);
    }
}
static void test_watchdog_clear(void)
{
    ++test_watchdog_clears;
    test_watchdog_lf_ticks = 0;
}
#define CLRWDT() test_watchdog_clear()
/* Advance the independent LF clock in the terminal fault loop. This model
   verifies recovery/control flow; it is not an instruction timing simulator. */
#define NOP() test_watchdog_advance(1024u)
#endif
