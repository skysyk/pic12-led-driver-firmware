#ifdef NDEBUG
#error Tests require assertions to be enabled
#endif
#include <assert.h>
#include <stdio.h>
/* Compile the production register path against the small PIC register model. */
#include "../firmware/hw_init.c"

static unsigned output_count(void)
{
    if (!CCP1CON) return GPIO & BOARD_PWM_MASK ? BOARD_PWM_PERIOD : 0;
    return ((unsigned)CCPR1L << 2) | ((CCP1CON >> 4) & 3u);
}

static void test_fractional_duties(void)
{
    unsigned q3, q2, residual, step, sum, sample, before, feeds = test_watchdog_clears;
    unsigned target_sum = 0, actual_sum = 0;
    /* Exhaust all Q3 inputs, Q2 rounding ties, four starting residuals, and
       exact averages over four 100 us slots, including integer/endpoints. */
    for (q3 = 0; q3 <= BOARD_PWM_PERIOD * 8u; ++q3) {
        q2 = (q3 + 1u) / 2u;
        for (residual = 0; residual < 4; ++residual) {
            hw_render(0, 0); dither = (uint8_t)(residual << 1);
            hw_render((uint16_t)q3, 0); sum = 0;
            for (step = 0; step < 4; ++step) {
                if (PIE1bits.TMR2IE) timer_isr();
                sample = output_count();
                assert(sample >= q2 / 4 && sample <= (q2 + 3) / 4);
                sum += sample;
                assert(!!PIE1bits.TMR2IE == !!(q2 & 3));
            }
            assert(sum == q2 && (dither & 6u) == residual * 2u);
            before = test_timer_accesses; sample = dither;
            hw_render((uint16_t)q3, 1);
            assert(test_timer_accesses == before && dither == sample);
        }
    }
    dither = 0;
    for (step = 0; step < 100000; ++step) {
        q3 = (step * 997u) % (BOARD_PWM_PERIOD * 8u + 1u);
        q2 = (q3 + 1u) / 2u;
        hw_render((uint16_t)q3, 0);
        if (PIE1bits.TMR2IE) timer_isr();
        target_sum += q2; actual_sum += output_count() * 4;
        assert(target_sum >= actual_sum && target_sum - actual_sum < 4);
    }
    hw_render(9, 0); INTCONbits.T0IF = 1; timer_isr();
    assert(hw_take_tick() && !hw_take_tick());
    assert(PIE1bits.TMR2IE && test_watchdog_clears == feeds);
    hw_render(0, 0);
}

static void render_and_check(uint16_t duty, uint8_t indicator)
{
    unsigned before = test_timer_accesses;
    unsigned changed = pwm_high != (uint8_t)(duty >> 2) ||
                       pwm_low != (uint8_t)((duty << 3) & 31u);
    unsigned clear = changed && !(pwm_low & 6u);
    hw_render(duty << 3, indicator);
    assert(!!(GPIO & BOARD_LED_MASK) == !indicator);
    if (duty == 0 || duty == BOARD_PWM_PERIOD) {
        assert(CCP1CON == 0);
        assert(!!(GPIO & BOARD_PWM_MASK) == !!duty);
        assert(test_timer_accesses == before + clear);
    } else {
        assert(test_timer_accesses == before + clear + 1);
        assert(PIE1bits.TMR2IE);
        timer_isr();
        assert((CCP1CON & 15u) == 12u);
        assert((((unsigned)CCPR1L << 2) | ((CCP1CON >> 4) & 3u)) == duty);
        assert(!PIE1bits.TMR2IE); /* Exact duties need no further dither IRQs. */
    }
}

static void test_pending_boundary(void)
{
    unsigned before;
    hw_render(9, 0); timer_isr();
    /* No future boundary can arrive. The already-pending one must be kept. */
    test_timer_running = 0; test_pir1.TMR2IF = 1;
    before = test_timer_accesses;
    hw_render(11, 0);
    assert(test_timer_accesses == before + 1 && PIE1bits.TMR2IE);
    timer_isr();
    assert(output_count() >= 1 && output_count() <= 2);
    test_timer_running = 1;
    hw_render(0, 0);
}

int main(void)
{
    uint16_t duty;
    unsigned before;
    pwm_low = 30; /* Explicit re-init must not retain a fractional cached target. */
    hw_init();
    assert(pwm_low == 0);
    assert(PR2 == 99 && T2CON == 0x0c && OSCCON == 0x71);
    assert(TRISIO == BOARD_INPUT_MASK && !(GPIO & BOARD_PWM_MASK));
    assert(GPIO & BOARD_LED_MASK);
    assert(INTCONbits.GIE && INTCONbits.T0IE && INTCONbits.PEIE);
    assert(WDTCON == 0x15 && test_watchdog_clears == 1);
    assert(!hw_take_tick());
    INTCONbits.T0IF = 1; timer_isr();
    assert(!INTCONbits.T0IF && hw_take_tick() && !hw_take_tick());
    assert(test_watchdog_clears == 1); /* Neither the ISR nor tick polling feeds. */
    for (duty = 0; duty <= BOARD_PWM_PERIOD; ++duty)
        render_and_check(duty, (uint8_t)(duty & 1u));
    /* Exercise endpoints and a change only in bit 8 of the duty cache. */
    render_and_check(0, 1);

    test_pending_boundary();
    test_fractional_duties();

    /* A late boundary inside the poll budget must still produce the right duty. */
    before = test_timer_accesses;
    test_timer_ready_at = before + 257u;
    hw_render(123u << 3, 0);
    assert(test_timer_accesses == before + 257u);
    timer_isr();
    assert((((unsigned)CCPR1L << 2) | ((CCP1CON >> 4) & 3u)) == 123);
    test_timer_ready_at = 0;
    render_and_check(400, 0);
    /* Stopped Timer2 cannot leave the prior static full-on output asserted. */
    before = test_timer_accesses;
    test_timer_running = 0;
    test_expect_watchdog_reset = 1;
    if (setjmp(test_watchdog_reset_context) == 0) {
        hw_render(200u << 3, 0);
        assert(0 && "A stalled boundary must enter watchdog recovery");
    }
    assert(test_timer_accesses == before + 257u);
    assert(!(GPIO & BOARD_PWM_MASK) && CCP1CON == 0);
    assert(test_watchdog_resets == 1 && test_watchdog_clears == 1);
    test_timer_running = 1; test_expect_watchdog_reset = 0;
    CLRWDT(); /* Model a fresh healthy interval before the second injected fault. */
    render_and_check(123, 0);
    assert((CCP1CON & 15u) == 12u);
    test_pir1.TMR2IF = 1; /* An idle updater's stale flag is not a fresh boundary. */
    test_timer_running = 0; test_expect_watchdog_reset = 1;
    if (setjmp(test_watchdog_reset_context) == 0) {
        hw_render(124u << 3, 0);
        assert(0 && "Recovery must also disconnect an already active CCP output");
    }
    assert(!(GPIO & BOARD_PWM_MASK) && CCP1CON == 0);
    assert(test_watchdog_resets == 2 && test_watchdog_clears == 2);
    test_timer_running = 1; test_expect_watchdog_reset = 0;
    render_and_check(256, 0);
    before = test_timer_accesses;
    hw_render(256u << 3, 1);
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
    puts("PASS: 20 kHz PWM / 10 kHz Q2 dithering, all 3,201 Q3 inputs x 4 residuals, four-slot exact means, 100,000 target changes, endpoints, watchdog recovery, ticks, and EEPROM IRQ restoration.");
    return 0;
}
