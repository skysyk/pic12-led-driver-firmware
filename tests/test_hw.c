#ifdef NDEBUG
#error Tests require assertions to be enabled
#endif
#include <assert.h>
#include <stdio.h>
/* Compile the production register path against the small PIC register model. */
#include "../firmware/hw_init.c"

static void render_and_check(uint16_t duty, uint8_t indicator)
{
    unsigned before = test_timer_accesses;
    hw_render(duty, indicator);
    assert(!!(GPIO & BOARD_LED_MASK) == !indicator);
    if (duty == 0 || duty == BOARD_PWM_PERIOD) {
        assert(CCP1CON == 0);
        assert(!!(GPIO & BOARD_PWM_MASK) == !!duty);
        assert(test_timer_accesses == before);
    } else {
        assert((CCP1CON & 15u) == 12u);
        assert((((unsigned)CCPR1L << 2) | ((CCP1CON >> 4) & 3u)) == duty);
        assert(test_timer_accesses == before + 2);
    }
}

int main(void)
{
    uint16_t duty;
    unsigned before;
    hw_init();
    assert(PR2 == 99 && T2CON == 4 && OSCCON == 0x71);
    assert(TRISIO == BOARD_INPUT_MASK && !(GPIO & BOARD_PWM_MASK));
    assert(GPIO & BOARD_LED_MASK);
    assert(INTCONbits.GIE && INTCONbits.T0IE);
    assert(WDTCON == 0x15 && test_watchdog_clears == 1);
    assert(!hw_take_tick());
    INTCONbits.T0IF = 1; timer_isr();
    assert(!INTCONbits.T0IF && hw_take_tick() && !hw_take_tick());
    assert(test_watchdog_clears == 1); /* Neither the ISR nor tick polling feeds. */
    for (duty = 0; duty <= BOARD_PWM_PERIOD; ++duty)
        render_and_check(duty, (uint8_t)(duty & 1u));
    /* Exercise endpoints and a change only in bit 8 of the duty cache. */
    render_and_check(0, 1);

    /* A late boundary inside the poll budget must still produce the right duty. */
    before = test_timer_accesses;
    test_timer_ready_at = before + 257u;
    hw_render(123, 0);
    assert(test_timer_accesses == before + 257u);
    assert((((unsigned)CCPR1L << 2) | ((CCP1CON >> 4) & 3u)) == 123);
    test_timer_ready_at = 0;
    render_and_check(400, 0);
    /* Stopped Timer2 cannot leave the prior static full-on output asserted. */
    before = test_timer_accesses;
    test_timer_running = 0;
    test_expect_watchdog_reset = 1;
    if (setjmp(test_watchdog_reset_context) == 0) {
        hw_render(200, 0);
        assert(0 && "A stalled boundary must enter watchdog recovery");
    }
    assert(test_timer_accesses == before + 257u);
    assert(!(GPIO & BOARD_PWM_MASK) && CCP1CON == 0);
    assert(test_watchdog_resets == 1 && test_watchdog_clears == 1);
    test_timer_running = 1; test_expect_watchdog_reset = 0;
    CLRWDT(); /* Model a fresh healthy interval before the second injected fault. */
    render_and_check(123, 0);
    assert((CCP1CON & 15u) == 12u);
    test_timer_running = 0; test_expect_watchdog_reset = 1;
    if (setjmp(test_watchdog_reset_context) == 0) {
        hw_render(124, 0);
        assert(0 && "Recovery must also disconnect an already active CCP output");
    }
    assert(!(GPIO & BOARD_PWM_MASK) && CCP1CON == 0);
    assert(test_watchdog_resets == 2 && test_watchdog_clears == 2);
    test_timer_running = 1; test_expect_watchdog_reset = 0;
    render_and_check(256, 0);
    before = test_timer_accesses;
    hw_render(256, 1);
    assert(!(GPIO & BOARD_LED_MASK) && test_timer_accesses == before);
    assert(CCPR1L == 64 && (CCP1CON & 0x3fu) == 12);
    render_and_check(1, 0);
    render_and_check(257, 1);
    render_and_check(400, 0);
    render_and_check(0, 1);

    hal_eeprom_start_write(123, 0x5a);
    assert(INTCONbits.GIE && EECON1bits.WR && !EECON1bits.WREN);
    assert(EEADR == 123 && EEDAT == 0x5a && EECON2 == 0xaa);
    EECON1bits.WR = 0; INTCONbits.GIE = 0;
    hal_eeprom_start_write(45, 0xa5);
    assert(!INTCONbits.GIE && EECON1bits.WR && !EECON1bits.WREN);
    puts("PASS: 20 kHz setup, all 401 PWM duties, endpoints, bounded timer wait, watchdog fault recovery, ticks, and EEPROM interrupt restoration.");
    return 0;
}
