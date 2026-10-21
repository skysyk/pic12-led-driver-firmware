#ifdef NDEBUG
#error Tests require assertions to be enabled
#endif
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>

/* Exercise the production main loop and hardware driver together. Supply
   deterministic Timer0 interrupts and raw buttons at the HAL boundary. */
#define hw_take_tick original_hw_take_tick
#define hw_buttons original_hw_buttons
#include "../firmware/hw_init.c"
#undef hw_take_tick
#undef hw_buttons
#include "storage.h"

static unsigned scenario, iterations;
static unsigned failed_writes, recovered_writes, alarm_iteration, recovery_iteration;
static unsigned blink_on, blink_off, stopped_iteration;
static jmp_buf healthy_completion;
uint8_t hw_take_tick(void)
{
    /* Dispatch the pending peripheral IRQ, as the enabled PIC interrupt
       controller does immediately after hw_render publishes a boundary. */
    if (PIE1bits.TMR2IE) timer_isr();
    ++iterations;
    if (scenario == 1) {
        assert(test_watchdog_clears == 1); /* No ticks means no progress/feed. */
        assert(iterations <= 1024);
    } else {
        assert(test_watchdog_clears == iterations);
        if (scenario == 0 && iterations == 1001)
            longjmp(healthy_completion, 1);
        if (scenario >= 3) {
            assert(iterations < 60000);
            if (EECON1bits.WR) {
                EECON1bits.WR = 0;
                if (scenario == 4 || failed_writes < 2) {
                    /* Scenario 3 loses one slot; scenario 4 loses all slots. */
                    assert(EEADR == (failed_writes / 2) * 16);
                    EEDAT = 0xff;
                    ++failed_writes;
                } else {
                    /* Save in the next healthy slot after retirement. The simple
                       SFR model retains each new value for its readback. */
                    assert(alarm_iteration != 0);
                    assert(iterations >= alarm_iteration + STORAGE_RETRY_TICKS);
                    assert(EEADR / 16 == 1);
                    ++recovered_writes;
                }
            }
            if (storage_fault()) {
                if (!alarm_iteration) alarm_iteration = iterations;
                else {
                    /* Boot scans erased EEPROM, so the reused clock starts
                       at 255. GPIO reflects the preceding completed tick. */
                    unsigned lit = ((iterations - 2u) & 128u) != 0;
                    assert(((GPIO & BOARD_LED_MASK) == 0) == lit);
                    if (lit) ++blink_on; else ++blink_off;
                }
                if (scenario == 3) assert(failed_writes == 2);
                else if (storage_stopped()) {
                    assert(failed_writes == 32 && storage_retired() == 0xffff);
                    if (!stopped_iteration) stopped_iteration = iterations;
                    if (iterations >= stopped_iteration + 1024) {
                        assert(blink_on >= 128 && blink_off >= 128);
                        /* Brightness still responds after persistent saves stop. */
                        assert(CCP1CON == 0x0c && CCPR1L == 70);
                        longjmp(healthy_completion, 1);
                    }
                }
            } else if (alarm_iteration) {
                if (!recovery_iteration) recovery_iteration = iterations;
                else {
                    assert(GPIO & BOARD_LED_MASK);
                    assert(recovered_writes == 12 && storage_retired() == 1);
                    assert(blink_on >= 128 && blink_off >= 128);
                    assert(CCP1CON == 0x0c && CCPR1L == 60); /* 60% output. */
                    longjmp(healthy_completion, 1);
                }
            }
        }
        INTCONbits.T0IF = 1;
        timer_isr();
        assert(test_watchdog_clears == iterations); /* ISR must not feed. */
        if (scenario == 2 && iterations == 45) test_timer_running = 0;
    }
    test_watchdog_advance(32u);
    return original_hw_take_tick();
}
uint8_t hw_buttons(void)
{
    if (scenario == 4 && stopped_iteration &&
        iterations >= stopped_iteration + 512 && iterations < stopped_iteration + 537)
        return BUTTON_UP;
    return scenario >= 2 && iterations <= 25 ? BUTTON_UP : 0;
}
#define main firmware_main
#include "../firmware/main.c"
#undef main

static void test_breathing_only_dithering(void)
{
    dimmer_state_t state = {50, 1, BREATHE_MODE | BREATHE_DEFAULT_PERIOD, 0, 1};
    button_events_t event = {0};
    unsigned i, saw_fractional = 0;
    test_timer_running = 1; test_expect_watchdog_reset = 0;
    hw_init(); dimmer_init(&state);
    for (i = 0; i < BREATHE_DEFAULT_PERIOD * BREATHE_STEP_TICKS; ++i) {
        hw_render(dimmer_output(), 0);
        if (PIE1bits.TMR2IE) timer_isr();
        if (PIE1bits.TMR2IE) saw_fractional = 1;
        (void)dimmer_tick(&event);
    }
    assert(saw_fractional);
    event.down = BUTTON_BRIGHTNESS;
    for (i = 0; i < BUTTON_EDITOR_TICKS; ++i) (void)dimmer_tick(&event);
    assert(dimmer_editing() == 1);
    hw_render(dimmer_output(), 1);
    if (PIE1bits.TMR2IE) timer_isr();
    assert(!PIE1bits.TMR2IE && CCP1CON == 0); /* Minimum editor: off. */
    event.down = 0; event.short_press = BUTTON_POWER;
    (void)dimmer_tick(&event);
    assert(dimmer_editing() == 2);
    hw_render(dimmer_output(), 1);
    if (PIE1bits.TMR2IE) timer_isr();
    assert(!PIE1bits.TMR2IE && CCPR1L == 1 && CCP1CON == 0x0c);
    state.breath = BREATHE_DEFAULT_PERIOD;
    for (i = 0; i <= 100; ++i) {
        state.brightness = (uint8_t)i; dimmer_init(&state);
        hw_render(dimmer_output(), 0);
        if (PIE1bits.TMR2IE) timer_isr();
        assert(!PIE1bits.TMR2IE);
    }
}

int main(void)
{
    for (scenario = 0; scenario < 5; ++scenario) {
        iterations = 0;
        failed_writes = recovered_writes = alarm_iteration = recovery_iteration = 0;
        blink_on = blink_off = stopped_iteration = 0;
        test_watchdog_clears = test_watchdog_resets = test_watchdog_lf_ticks = 0;
        test_timer_running = 1; test_timer_ready_at = 0;
        WDTCON = 0x08; EEDAT = 0xff;
        buttons_init(); /* Real resets clear static button state in C startup. */
        test_expect_watchdog_reset = scenario == 1 || scenario == 2;
        if (scenario == 0 || scenario >= 3) {
            if (setjmp(healthy_completion) == 0) firmware_main();
            assert(test_watchdog_clears == iterations);
            if (scenario == 0) assert(iterations == 1001);
            else if (scenario == 3) assert(alarm_iteration && recovery_iteration);
            else assert(alarm_iteration && stopped_iteration);
            assert(test_watchdog_resets == 0);
        } else {
            if (setjmp(test_watchdog_reset_context) == 0) firmware_main();
            assert(test_watchdog_resets == 1);
            assert(CCP1CON == 0 && !(GPIO & BOARD_PWM_MASK));
            if (scenario == 1) assert(iterations == 1024 && test_watchdog_clears == 1);
            else assert(iterations == 45 && test_watchdog_clears == 45);
        }
    }
    test_breathing_only_dithering();
    puts("PASS: production main-loop watchdog servicing, stopped-timer recovery, retirement LED/backoff/recovery, responsive controls after EEPROM exhaustion, and dithering only in breathing (steady/editor fixed).");
    return 0;
}
