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

static unsigned scenario, iterations;
static jmp_buf healthy_completion;
uint8_t hw_take_tick(void)
{
    ++iterations;
    if (scenario == 1) {
        assert(test_watchdog_clears == 1); /* No ticks means no progress/feed. */
        assert(iterations <= 1024);
    } else {
        assert(test_watchdog_clears == iterations);
        if (scenario == 0 && iterations == 1001)
            longjmp(healthy_completion, 1);
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
    return scenario == 2 && iterations <= 25 ? BUTTON_UP : 0;
}
#define main firmware_main
#include "../firmware/main.c"
#undef main

int main(void)
{
    for (scenario = 0; scenario < 3; ++scenario) {
        iterations = 0;
        test_watchdog_clears = test_watchdog_resets = test_watchdog_lf_ticks = 0;
        test_timer_running = 1; test_timer_ready_at = 0;
        WDTCON = 0x08; EEDAT = 0xff;
        test_expect_watchdog_reset = scenario != 0;
        if (scenario == 0) {
            if (setjmp(healthy_completion) == 0) firmware_main();
            assert(iterations == 1001 && test_watchdog_clears == 1001);
            assert(test_watchdog_resets == 0);
        } else {
            if (setjmp(test_watchdog_reset_context) == 0) firmware_main();
            assert(test_watchdog_resets == 1);
            assert(CCP1CON == 0 && !(GPIO & BOARD_PWM_MASK));
            if (scenario == 1) assert(iterations == 1024 && test_watchdog_clears == 1);
            else assert(iterations == 45 && test_watchdog_clears == 45);
        }
    }
    puts("PASS: production main-loop watchdog servicing; recovery with missing ticks or a stalled PWM render.");
    return 0;
}
