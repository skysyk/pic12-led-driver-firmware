#ifdef NDEBUG
#error Tests require assertions to be enabled
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "buttons.h"
#include "dimmer.h"
#include "hal.h"
#include "storage.h"

static uint8_t eeprom[256], pending, pending_address, pending_value;
static unsigned writes;
static button_events_t events;
static uint8_t rendered_status;
static void settings_are(const dimmer_state_t *s);

uint8_t hal_eeprom_read(uint8_t address)
{
    assert(!pending);
    return eeprom[address];
}
uint8_t hal_eeprom_busy(void) { return pending; }
void hal_eeprom_start_write(uint8_t address, uint8_t value)
{
    assert(!pending);
    pending = 1;
    pending_address = address;
    pending_value = value;
    ++writes;
}
static void finish_write(void)
{
    assert(pending);
    eeprom[pending_address] = pending_value;
    pending = 0;
}
static dimmer_state_t boot(void)
{
    dimmer_state_t result;
    pending = 0;
    storage_init(&result);
    dimmer_init(&result);
    buttons_init();
    rendered_status = dimmer_status_led();
    return result;
}
static void fresh(void)
{
    memset(eeprom, 0xff, sizeof eeprom);
    writes = 0;
    (void)boot();
}
static void drain(void)
{
    unsigned guard = 0;
    while (storage_busy()) {
        assert(++guard < 5000);
        storage_tick();
        storage_service();
        if (pending) finish_write();
    }
}
static void save(uint8_t brightness, uint8_t power_on)
{
    dimmer_state_t s = {brightness, power_on, BREATHE_DEFAULT_PERIOD, 10, 100};
    storage_request(&s, 1);
    drain();
}
static void ui_tick(uint8_t raw)
{
    uint8_t changed;
    storage_tick();
    buttons_tick(raw, &events);
    changed = dimmer_tick(&events);
    if (changed) rendered_status = dimmer_status_led();
    if (changed & DIMMER_CHANGED) storage_request(dimmer_state(), changed & DIMMER_IMMEDIATE_SAVE);
    storage_service();
    if (pending) finish_write();
}
static void ui_ticks(uint8_t raw, unsigned n)
{
    while (n--) ui_tick(raw);
}
static void click(uint8_t mask)
{
    ui_ticks(mask, BUTTON_DEBOUNCE_TICKS + 30);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
}
static void tap(uint8_t mask)
{
    click(mask);
}
static void hold_mode(void)
{
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
}
static void state_is(uint8_t brightness, uint8_t power_on)
{
    if (dimmer_state()->brightness != brightness || dimmer_state()->power_on != power_on)
        fprintf(stderr, "Expected brightness/power %u/%u; got %u/%u\n", brightness, power_on,
                dimmer_state()->brightness, dimmer_state()->power_on);
    assert(dimmer_state()->brightness == brightness);
    assert(dimmer_state()->power_on == power_on);
}
static void status_is(uint8_t expected)
{
    assert(dimmer_status_led() == expected && rendered_status == expected);
}
static void test_pwm_scaling(void)
{
    unsigned percent;
    uint16_t counts, previous = 0;
    assert(_XTAL_FREQ / (4u * (BOARD_PWM_PR2 + 1u)) == 20000u);
    assert(BOARD_PWM_PERIOD == 400u && BREATHE_PHASE_STEPS == 2048u);
    for (percent = 0; percent <= 100; ++percent) {
        counts = dimmer_duty((uint8_t)percent);
        assert(counts == ((uint32_t)percent * BOARD_PWM_PERIOD + 50u) / 100u);
        if (percent) assert(counts > previous);
        previous = counts;
    }
    assert(dimmer_duty(0) == 0 && dimmer_duty(100) == BOARD_PWM_PERIOD);
}
static void test_buttons_and_dimming(void)
{
    unsigned i;
    fresh();
    state_is(50, 0);
    assert(dimmer_output() == 0);
    assert(dimmer_duty(10) == 40 && dimmer_duty(50) == 200);
    assert(dimmer_duty(100) == 400);
    for (i = 0; i < 30; ++i) {
        ui_tick(BUTTON_POWER);
        ui_tick(0);
    }
    state_is(50, 0); /* Bounce never reaches stable down. */
    assert(writes == 0);
    tap(BUTTON_POWER);
    state_is(50, 1);
    drain();
    assert(dimmer_output() == dimmer_duty(50));
    tap(BUTTON_UP);
    state_is(60, 1);
    tap(BUTTON_DOWN);
    state_is(50, 1);
    for (i = 0; i < 20; ++i) tap(BUTTON_DOWN);
    state_is(10, 1);
    for (i = 0; i < 9; ++i) tap(BUTTON_UP);
    state_is(100, 1);
    tap(BUTTON_UP);
    state_is(100, 1);

    save(50, 1);
    (void)boot();
    ui_ticks(BUTTON_UP, BUTTON_DEBOUNCE_TICKS + BUTTON_HOLD_TICKS - 1);
    state_is(50, 1);
    ui_tick(BUTTON_UP);
    assert(events.held == BUTTON_UP);
    state_is(51, 1);
    ui_ticks(BUTTON_UP, BUTTON_RAMP_TICKS * 4);
    state_is(55, 1);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(!(events.short_press & BUTTON_UP));
    state_is(55, 1);
    ui_ticks(BUTTON_DOWN, 70000); /* No long-hold counter overflow/retrigger. */
    state_is(10, 1);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(10, 1);

    tap(BUTTON_POWER);
    state_is(10, 0);
    tap(BUTTON_UP);
    state_is(20, 1); /* Adjusting in standby turns on. */
    tap(BUTTON_POWER);
    state_is(20, 0);
    tap(BUTTON_POWER);
    state_is(20, 1); /* Power toggles preserve level. */
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS + 50);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(20, 1); /* Long power press is not a short press. */
}
static void test_chords(void)
{
    fresh();
    save(50, 1);
    (void)boot();
    ui_ticks(BUTTON_BRIGHTNESS, 1000);
    state_is(50, 1);
    ui_ticks(BUTTON_DOWN, 1000); /* Releasing one must not start a ramp. */
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(50, 1);
    tap(BUTTON_UP);
    state_is(60, 1);
    tap(BUTTON_POWER | BUTTON_UP);
    state_is(60, 0);
    tap(BUTTON_POWER | BUTTON_BRIGHTNESS);
    state_is(60, 1);
}
static void test_power_long_hold_event(void)
{
    unsigned i;
    button_events_t e;
    buttons_init();
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_ARM_TICKS - 1; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.power_hold);
    }
    buttons_tick(BUTTON_POWER, &e);
    assert(e.power_hold == POWER_RESET_ARM);
    for (i = BUTTON_RESET_ARM_TICKS + 1; i < BUTTON_RESET_COMMIT_TICKS; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.power_hold);
    }
    buttons_tick(BUTTON_POWER, &e);
    assert(e.power_hold == POWER_RESET_COMMIT);
    for (i = 0; i < 70000; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.power_hold); /* No repeat or 16-bit timer wraparound. */
    }
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS; ++i) buttons_tick(0, &e);
    assert((e.released & BUTTON_POWER) && !(e.short_press & BUTTON_POWER));

    buttons_init();
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_ARM_TICKS - 1; ++i)
        buttons_tick(BUTTON_POWER, &e);
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS; ++i) {
        buttons_tick(0, &e);
        assert(!e.power_hold);
    }
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_ARM_TICKS - 1; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.power_hold); /* Separate presses must not accumulate time. */
    }
    buttons_tick(BUTTON_POWER, &e);
    assert(e.power_hold == POWER_RESET_ARM);
}
static void test_power_overlap(void)
{
    uint8_t button, initially_on, release_order, before;
    unsigned committed_writes;
    /* Tap chords must give power priority for either release order, both
       brightness directions, and both initial power states. */
    for (button = BUTTON_UP; button <= BUTTON_DOWN; button <<= 1) {
        for (initially_on = 0; initially_on <= 1; ++initially_on) {
            for (release_order = 0; release_order < 3; ++release_order) {
                fresh(); save(50, initially_on); (void)boot();
                ui_ticks(BUTTON_POWER | button, BUTTON_DEBOUNCE_TICKS + 30);
                if (release_order == 0) {
                    ui_ticks(button, BUTTON_DEBOUNCE_TICKS); /* Power first. */
                    state_is(50, (uint8_t)!initially_on);
                } else if (release_order == 1) {
                    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS); /* Brightness first. */
                    state_is(50, initially_on);
                }
                ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
                state_is(50, (uint8_t)!initially_on);
                drain();
                committed_writes = writes;
                ui_ticks(0, STORAGE_DELAY_TICKS + 20);
                assert(writes == committed_writes);
                (void)boot(); state_is(50, (uint8_t)!initially_on);
                tap(button); /* A separate press works after release. */
                state_is(button == BUTTON_UP ? 60 : 40, 1);
            }
        }

        fresh(); save(50, 1); (void)boot();
        ui_ticks(BUTTON_POWER | button, BUTTON_DEBOUNCE_TICKS + 30);
        ui_ticks(button, BUTTON_DEBOUNCE_TICKS);
        ui_ticks(button, BUTTON_HOLD_TICKS + BUTTON_RAMP_TICKS * 5);
        state_is(50, 0); /* A hold started with power cannot resume ramping. */
        ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
        state_is(50, 0);
        tap(button);
        state_is(button == BUTTON_UP ? 60 : 40, 1);

        fresh(); save(50, 1); (void)boot();
        ui_ticks(button, BUTTON_DEBOUNCE_TICKS + BUTTON_HOLD_TICKS + BUTTON_RAMP_TICKS);
        before = dimmer_state()->brightness;
        ui_ticks(BUTTON_POWER | button, BUTTON_DEBOUNCE_TICKS + 30);
        /* A repeat before power finishes debouncing is still legitimate. */
        assert(button == BUTTON_UP ? dimmer_state()->brightness >= before :
                                    dimmer_state()->brightness <= before);
        before = dimmer_state()->brightness;
        ui_ticks(button, BUTTON_DEBOUNCE_TICKS);
        state_is(before, 0);
        ui_ticks(button, BUTTON_RAMP_TICKS * 5);
        state_is(before, 0); /* Power also cancels a ramp already in progress. */
        ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
        state_is(before, 0);
    }
}
static void test_settings_reset(void)
{
    const dimmer_state_t defaults = {50, 0, 6, 10, 100};
    dimmer_state_t s = {80, 0, BREATHE_MODE | 13u, 27, 88};
    const unsigned cancel_at[] = {BUTTON_RESET_ARM_TICKS - 1, BUTTON_RESET_ARM_TICKS + 1,
                                 BUTTON_RESET_ARM_TICKS + STATUS_BLINK_TICKS + 1,
                                 BUTTON_RESET_COMMIT_TICKS - 1};
    unsigned before, i, j;
    fresh(); storage_request(&s, 1); drain(); (void)boot(); before = writes;
    status_is(0);
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_ARM_TICKS - 1);
    status_is(0); settings_are(&s);
    ui_tick(BUTTON_POWER);
    status_is(1); assert(!dimmer_output() && writes == before);
    for (i = 0; i < 5; ++i) {
        ui_ticks(BUTTON_POWER, STATUS_BLINK_TICKS - 1);
        status_is((uint8_t)!(i & 1u));
        ui_tick(BUTTON_POWER);
        status_is((uint8_t)(i & 1u));
        settings_are(&s); assert(writes == before);
    }
    ui_ticks(BUTTON_POWER, BUTTON_RESET_COMMIT_TICKS - BUTTON_RESET_ARM_TICKS - 5 * STATUS_BLINK_TICKS - 1);
    settings_are(&s);
    ui_tick(BUTTON_POWER);
    settings_are(&defaults);
    status_is(0); assert(writes == before + 1);
    drain(); before = writes;
    ui_ticks(BUTTON_POWER, 70000);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(!dimmer_status_led() && writes == before);
    (void)boot(); settings_are(&defaults);
    /* Releasing before confirmation never resets or turns on the output. */
    for (i = 0; i < sizeof cancel_at / sizeof cancel_at[0]; ++i) {
        fresh(); storage_request(&s, 1); drain(); (void)boot(); before = writes;
        ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + cancel_at[i]);
        ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
        assert(!dimmer_status_led() && writes == before);
        settings_are(&s); (void)boot(); settings_are(&s);
    }
    /* An on-state hold toggles mode once and cannot reset. After a completed
       tap turns off, a new hold is eligible for the ordinary reset gesture. */
    for (j = 0; j < 2; ++j) {
        s.power_on = 1;
        fresh(); storage_request(&s, 1); drain(); (void)boot();
        if (j) click(BUTTON_POWER);
        ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_COMMIT_TICKS + 10);
        ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
        if (j) settings_are(&defaults);
        else {
            s.breath ^= BREATHE_MODE;
            settings_are(&s);
        }
        assert(!dimmer_status_led());
    }
    fresh();
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_COMMIT_TICKS);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    settings_are(&defaults); assert(!writes); /* Already-default reset avoids wear. */
}
static void test_save_policy(void)
{
    unsigned before, i;
    dimmer_state_t s = {60, 1, BREATHE_DEFAULT_PERIOD, 10, 100};
    fresh();
    ui_ticks(0, 2000);
    assert(writes == 0); /* Erased EEPROM is left alone. */
    save(50, 1);
    (void)boot();
    before = writes;
    storage_request(dimmer_state(), 1);
    drain();
    assert(writes == before);
    storage_request(&s, 0);
    for (i = 0; i < STORAGE_DELAY_TICKS - 1; ++i) {
        storage_tick(); storage_service();
    }
    assert(writes == before);
    s.brightness = 61;
    storage_request(&s, 0); /* Another adjustment restarts the quiet window. */
    for (i = 0; i < STORAGE_DELAY_TICKS - 1; ++i) {
        storage_tick(); storage_service();
    }
    assert(writes == before);
    storage_tick(); storage_service();
    assert(pending && writes == before + 1);
    /* Writes remain busy across calls; no overlapping access is permitted. */
    for (i = 0; i < 10; ++i) storage_service();
    assert(writes == before + 1);
    finish_write();
    drain();
    (void)boot(); state_is(61, 1);
    s.brightness = 62;
    storage_request(&s, 0);
    s.power_on = 0;
    storage_request(&s, 1);
    storage_service();
    assert(pending); /* A power change bypasses the delayed save. */
    finish_write(); drain();
    (void)boot(); state_is(62, 0);
    for (i = 10; i <= 100; ++i) {
        save((uint8_t)i, 1);
        (void)boot(); state_is((uint8_t)i, 1);
    }
}
static void test_inflight_changes(void)
{
    dimmer_state_t s = {60, 1, BREATHE_DEFAULT_PERIOD, 10, 100};
    fresh(); save(50, 1);
    storage_request(&s, 1);
    storage_service();
    assert(pending);
    s.brightness = 70; s.power_on = 0;
    storage_request(&s, 1);
    finish_write();
    drain();
    (void)boot(); state_is(70, 0);
}

static uint8_t reference_crc(const uint8_t *p, unsigned length)
{
    unsigned i;
    uint8_t c = 0;
    while (length--) {
        c ^= *p++;
        for (i = 0; i < 8; ++i) c = (uint8_t)((c << 1) ^ ((c & 128) ? 7 : 0));
    }
    return c;
}
static void seed(unsigned slot, uint16_t seq, uint16_t duty, uint8_t flags)
{
    uint8_t *r = eeprom + slot * 8;
    r[0] = 0xa5; r[1] = 1;
    r[2] = (uint8_t)seq; r[3] = (uint8_t)(seq >> 8);
    r[4] = (uint8_t)duty; r[5] = (uint8_t)(duty >> 8);
    r[6] = flags; r[7] = reference_crc(r + 1, 6);
}
static uint16_t legacy_duty(uint8_t percent)
{
    return (uint16_t)(((uint32_t)percent * 1023u + 50u) / 100u);
}
static void seed_v4(unsigned slot, uint16_t seq, const dimmer_state_t *s)
{
    uint8_t *r = eeprom + slot * 16;
    r[0] = 0xa5; r[1] = 4;
    r[2] = (uint8_t)seq; r[3] = (uint8_t)(seq >> 8);
    r[4] = (uint8_t)(s->brightness | (s->power_on << 7));
    r[5] = s->minimum;
    r[6] = (uint8_t)(s->maximum | (s->breath & BREATHE_MODE));
    r[7] = s->breath & BREATHE_PERIOD_MASK;
    r[8] = reference_crc(r + 1, 7); r[9] = 0;
}
static void editor_gesture(void)
{
    ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
}
static void test_editor_gestures_and_save(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 6u, 10, 100};
    unsigned before;
    fresh(); storage_request(&s, 1); drain(); (void)boot();
    before = writes;
    ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS - 2);
    assert(!dimmer_editing());
    ui_tick(BUTTON_BRIGHTNESS);
    assert(dimmer_editing() == 1);
    status_is(1);
    assert(dimmer_output() == dimmer_duty(10));
    ui_ticks(BUTTON_BRIGHTNESS, BUTTON_EDITOR_TICKS * 2);
    assert(dimmer_editing() == 1); /* A continuous hold triggers only once. */
    ui_ticks(BUTTON_UP, BUTTON_DEBOUNCE_TICKS + BUTTON_HOLD_TICKS + 50);
    assert(dimmer_state()->minimum == 10); /* Staggered release cannot edit. */
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    tap(BUTTON_UP);
    assert(dimmer_state()->minimum == 20 && dimmer_output() == dimmer_duty(20));
    ui_ticks(0, STORAGE_DELAY_TICKS + 10);
    assert(writes == before); /* Preview changes remain uncommitted until exit. */
    (void)boot();
    assert(!dimmer_editing() && dimmer_state()->minimum == 10);

    editor_gesture();
    tap(BUTTON_UP);
    tap(BUTTON_POWER);
    assert(dimmer_editing() == 2 && dimmer_output() == dimmer_duty(100));
    tap(BUTTON_DOWN);
    assert(dimmer_state()->maximum == 90);
    tap(BUTTON_POWER); tap(BUTTON_POWER);
    assert(dimmer_editing() == 2); /* Power clicks select only; no mode/off gesture. */
    assert(dimmer_state()->breath == s.breath);
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_COMMIT_TICKS + 10);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(dimmer_editing() == 2 && dimmer_state()->power_on);
    editor_gesture();
    assert(!dimmer_editing());
    status_is(0);
    assert(dimmer_output() >= dimmer_duty(20) && dimmer_output() <= dimmer_duty(90));
    drain(); (void)boot();
    state_is(50, 1);
    assert(dimmer_state()->minimum == 20 && dimmer_state()->maximum == 90);
    assert(dimmer_output() == dimmer_duty(20));
    assert(dimmer_state()->breath == s.breath);
    before = writes;
    editor_gesture(); editor_gesture(); drain();
    assert(writes == before); /* Opening/closing without edits does not wear EEPROM. */

    editor_gesture();
    ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS + 10);
    assert(!dimmer_editing());
    ui_ticks(BUTTON_BRIGHTNESS, BUTTON_EDITOR_TICKS * 3);
    assert(!dimmer_editing()); /* Exit hold also cannot re-enter automatically. */
    ui_ticks(BUTTON_DOWN, BUTTON_DEBOUNCE_TICKS + BUTTON_HOLD_TICKS + 50);
    assert(dimmer_state()->breath == s.breath);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);

    tap(BUTTON_POWER); /* Standby cannot enter the editor. */
    editor_gesture();
    assert(!dimmer_editing() && !dimmer_state()->power_on);
    tap(BUTTON_POWER);
    ui_ticks(BUTTON_POWER | BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS + 10);
    assert(!dimmer_editing()); /* Power keeps priority over the chord. */
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
}
static void test_editor_status_blink(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | BREATHE_DEFAULT_PERIOD, 10, 100};
    unsigned before, i;
    fresh(); storage_request(&s, 1); drain(); (void)boot();
    before = writes;
    editor_gesture();
    ui_ticks(0, STATUS_EDITOR_BLINK_TICKS * 4);
    assert(dimmer_editing() == 1 && dimmer_output() == dimmer_duty(10));
    status_is(1); /* Minimum stays solid for any animation phase. */
    tap(BUTTON_POWER); assert(dimmer_editing() == 2); status_is(1);
    ui_ticks(0, STATUS_EDITOR_BLINK_TICKS - 1); status_is(1);
    ui_tick(0); status_is(0);
    assert(dimmer_output() == dimmer_duty(100)); /* Only the indicator blinks. */
    tap(BUTTON_DOWN); /* Editing during the dark phase still selects maximum. */
    assert(dimmer_state()->maximum == 90 && dimmer_editing() == 2);
    assert(dimmer_output() == dimmer_duty(90)); status_is(0);
    tap(BUTTON_POWER); /* A dark maximum must select minimum, not exit editing. */
    assert(dimmer_editing() == 1 && dimmer_state()->power_on); status_is(1);
    assert(dimmer_output() == dimmer_duty(10));
    tap(BUTTON_POWER); assert(dimmer_editing() == 2); status_is(1);
    /* Long power holds cannot change the selected bound or disturb the blink. */
    for (i = 1; i <= 70000; ++i) {
        ui_tick(BUTTON_POWER);
        status_is((uint8_t)!((i / STATUS_EDITOR_BLINK_TICKS) & 1u));
        assert(dimmer_editing() == 2 && dimmer_state()->power_on);
        assert(dimmer_output() == dimmer_duty(90));
    }
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(writes == before && dimmer_state()->breath == s.breath);
    editor_gesture(); assert(!dimmer_editing()); status_is(0);
    assert(writes > before); drain(); (void)boot();
    assert(dimmer_state()->minimum == 10 && dimmer_state()->maximum == 90);
    before = writes;
    editor_gesture(); tap(BUTTON_POWER);
    ui_ticks(0, STATUS_EDITOR_BLINK_TICKS * 8);
    editor_gesture(); drain();
    assert(writes == before); /* Transient indicator state is never persisted. */
}
static void test_editor_chord_rearming(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 6u, 10, 100};
    unsigned editing, held, during, before;
    uint8_t expected;
    for (editing = 0; editing < 2; ++editing) {
        for (held = BUTTON_UP; held <= BUTTON_DOWN; held <<= 1) {
            s.breath = BREATHE_MODE | BREATHE_DEFAULT_PERIOD;
            fresh(); storage_request(&s, 1); drain(); (void)boot();
            if (editing) editor_gesture();
            ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS);
            expected = (uint8_t)!editing;
            assert(dimmer_editing() == expected);
            drain(); before = writes;
            /* Releasing either button alone cannot rearm a completed chord. */
            ui_ticks((uint8_t)held, BUTTON_DEBOUNCE_TICKS + 30);
            ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS * 2);
            assert(dimmer_editing() == expected && writes == before);
            status_is(expected);
            ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
            editor_gesture();
            assert(dimmer_editing() == editing);

            for (during = 0; during < 2; ++during) {
                /* Outside the editor, the power hold changes steady to
                   breathing; its overlapping brightness chord stays cancelled. */
                s.breath = (uint8_t)(BREATHE_DEFAULT_PERIOD | (editing ? BREATHE_MODE : 0));
                fresh(); storage_request(&s, 1); drain(); (void)boot();
                if (editing) editor_gesture();
                before = writes;
                if (during)
                    ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS / 2);
                ui_ticks(BUTTON_POWER | BUTTON_BRIGHTNESS,
                         BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS + 10);
                drain(); before = writes;
                ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS * 2);
                assert(dimmer_editing() == editing && writes == before);
                /* Cancellation also survives a partial release/repress. */
                ui_ticks((uint8_t)held, BUTTON_DEBOUNCE_TICKS + 30);
                ui_ticks(BUTTON_BRIGHTNESS, BUTTON_DEBOUNCE_TICKS + BUTTON_EDITOR_TICKS * 2);
                assert(dimmer_editing() == editing && writes == before);
                status_is((uint8_t)editing);
                assert(dimmer_state()->breath == (BREATHE_MODE | BREATHE_DEFAULT_PERIOD));
                ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
                editor_gesture();
                assert(dimmer_editing() == !editing);
            }
        }
    }
}
static void test_editor_bounds_and_ramping(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 1u, 10, 100};
    unsigned i;
    fresh(); storage_request(&s, 1); drain(); (void)boot();
    editor_gesture();
    for (i = 0; i < 15; ++i) tap(BUTTON_DOWN);
    assert(dimmer_state()->minimum == 0 && dimmer_output() == 0);
    tap(BUTTON_POWER);
    for (i = 0; i < 110; ++i) tap(BUTTON_DOWN);
    assert(dimmer_state()->maximum == 1 && dimmer_output() == dimmer_duty(1));
    tap(BUTTON_POWER);
    tap(BUTTON_UP);
    assert(dimmer_state()->minimum == 0); /* Cannot equal/cross the upper bound. */
    tap(BUTTON_POWER);
    ui_ticks(BUTTON_UP, BUTTON_DEBOUNCE_TICKS + BUTTON_HOLD_TICKS - 1);
    assert(dimmer_state()->maximum == 1);
    ui_tick(BUTTON_UP);
    assert(dimmer_state()->maximum == 2); /* Hold ramp changes by 1%, not 10%. */
    ui_ticks(BUTTON_UP, BUTTON_RAMP_TICKS * 4);
    assert(dimmer_state()->maximum == 6);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(dimmer_state()->maximum == 6);
    ui_ticks(BUTTON_UP, 70000);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(dimmer_state()->maximum == 100);
    editor_gesture(); drain(); (void)boot();
    assert(dimmer_state()->minimum == 0 && dimmer_state()->maximum == 100);
    assert(dimmer_output() == 0);
    ui_ticks(0, BREATHE_STEP_TICKS / 2);
    assert(dimmer_output() == dimmer_duty(100));
    ui_ticks(0, BREATHE_STEP_TICKS / 2);
    assert(dimmer_output() == 0);
    assert(!dimmer_status_led());

    /* Minimum and maximum may also differ by just one percentage point. */
    s.minimum = 99; s.maximum = 100;
    storage_request(&s, 1); drain(); (void)boot();
    editor_gesture(); tap(BUTTON_UP);
    assert(dimmer_state()->minimum == 99);
    tap(BUTTON_POWER); tap(BUTTON_DOWN);
    assert(dimmer_state()->maximum == 100);
    editor_gesture();
    ui_ticks(0, BREATHE_STEP_TICKS);
    assert(dimmer_output() >= dimmer_duty(99) && dimmer_output() <= dimmer_duty(100));
}
static void test_custom_bounds_mode_entry(void)
{
    dimmer_state_t s = {50, 1, BREATHE_DEFAULT_PERIOD, 20, 80};
    unsigned i;
    const uint8_t limits[][3] = {{20, 80, 50}, {60, 80, 60}, {0, 20, 20}};
    for (i = 0; i < sizeof limits / sizeof limits[0]; ++i) {
        fresh(); s.minimum = limits[i][0]; s.maximum = limits[i][1];
        storage_request(&s, 1); drain(); (void)boot();
        ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS);
        assert(dimmer_output() == dimmer_duty(limits[i][2]));
        ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
        hold_mode();
        assert(dimmer_output() == dimmer_duty(50));
    }
}
static void test_all_editor_ranges(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 6u, 0, 100};
    unsigned low, high, selection, direction, i;
    for (low = 0; low < 100; ++low) {
        for (high = low + 1; high <= 100; ++high) {
            for (selection = 1; selection <= 2; ++selection) {
                for (direction = 0; direction < 2; ++direction) {
                    button_events_t e = {0};
                    int expected = (int)(selection == 1 ? low : high) + (direction ? 10 : -10);
                    int floor = selection == 1 ? 0 : (int)low + 1;
                    int ceiling = selection == 1 ? (int)high - 1 : 100;
                    if (expected < floor) expected = floor;
                    if (expected > ceiling) expected = ceiling;
                    s.minimum = (uint8_t)low; s.maximum = (uint8_t)high;
                    dimmer_init(&s); e.down = BUTTON_BRIGHTNESS;
                    for (i = 0; i < BUTTON_EDITOR_TICKS; ++i) (void)dimmer_tick(&e);
                    e.down = 0; (void)dimmer_tick(&e);
                    if (selection == 2) { e.short_press = BUTTON_POWER; (void)dimmer_tick(&e); }
                    e.short_press = direction ? BUTTON_UP : BUTTON_DOWN;
                    (void)dimmer_tick(&e);
                    assert(dimmer_state()->minimum < dimmer_state()->maximum);
                    assert(dimmer_state()->maximum <= 100);
                    assert((selection == 1 ? dimmer_state()->minimum : dimmer_state()->maximum) == expected);
                }
            }
        }
    }
}
/* Independent double-precision cosine reference; twelve Taylor terms are
   sufficient on 0..pi and need no host-specific math-library linker option. */
static double reference_sine(unsigned phase)
{
    double angle, term = 1, cosine = 1;
    unsigned k;
    if (phase > BREATHE_PHASE_STEPS / 2) phase = BREATHE_PHASE_STEPS - phase;
    angle = phase * 2 * 3.14159265358979323846 / BREATHE_PHASE_STEPS;
    for (k = 1; k <= 12; ++k) {
        term *= -angle * angle / ((2 * k - 1) * (2 * k));
        cosine += term;
    }
    return (1 - cosine) / 2;
}
static void test_sine_curve_and_levels(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 6u, 10, 100};
    const uint8_t ranges[][2] = {{0, 100}, {10, 100}, {99, 100}, {0, 1}};
    uint8_t seen[BOARD_PWM_PERIOD + 1];
    button_events_t e = {0};
    unsigned phase, span, weight, previous = 0, actual, ideal, range, tick, count;
    int error;
    double weight_error;
    for (phase = 0; phase < BREATHE_PHASE_STEPS; ++phase) {
        weight = dimmer_sine((uint16_t)phase);
        assert(weight <= 512);
        if (phase <= BREATHE_PHASE_STEPS / 2) assert(weight >= previous && weight <= previous + 1);
        else assert(weight <= previous && weight + 1 >= previous);
        previous = weight;
        assert(weight == dimmer_sine((uint16_t)((BREATHE_PHASE_STEPS - phase) % BREATHE_PHASE_STEPS)));
        weight_error = weight - 512 * reference_sine(phase);
        assert(weight_error >= -1.8 && weight_error <= 1.8);
        for (span = 1; span <= BOARD_PWM_PERIOD; ++span) {
            actual = (weight * span + 256) / 512;
            ideal = (unsigned)(reference_sine(phase) * span + 0.5);
            error = (int)actual - (int)ideal;
            assert(error >= -2 && error <= 2);
        }
    }
    assert(dimmer_sine(0) == 0 && dimmer_sine(512) == 256 && dimmer_sine(1024) == 512);
    for (range = 0; range < sizeof ranges / sizeof ranges[0]; ++range) {
        s.minimum = ranges[range][0]; s.maximum = ranges[range][1];
        memset(seen, 0, sizeof seen); dimmer_init(&s);
        for (tick = 0; tick < BREATHE_DEFAULT_PERIOD * BREATHE_STEP_TICKS; ++tick) {
            actual = dimmer_output();
            assert(actual >= dimmer_duty(s.minimum) && actual <= dimmer_duty(s.maximum));
            ideal = dimmer_duty(s.minimum) + (unsigned)(reference_sine(dimmer_phase()) *
                    (dimmer_duty(s.maximum) - dimmer_duty(s.minimum)) + 0.5);
            error = (int)actual - (int)ideal;
            assert(error >= -2 && error <= 2);
            seen[actual] = 1;
            (void)dimmer_tick(&e);
        }
        count = 0;
        for (actual = 0; actual <= BOARD_PWM_PERIOD; ++actual) count += seen[actual];
        assert(seen[dimmer_duty(s.minimum)] && seen[dimmer_duty(s.maximum)]);
        assert(count == (unsigned)(dimmer_duty(s.maximum) - dimmer_duty(s.minimum)) + 1);
        if (range == 0) assert(count == 401);
        else if (range == 1) assert(count == 361);
        else assert(count == 5);
    }
}
static void test_sine_timing_and_entry(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 1u, 10, 100};
    button_events_t e = {0};
    uint8_t seen[BREATHE_PHASE_STEPS];
    unsigned period, ticks, tick, count, low, high, brightness, target;
    int error;
    for (period = 1; period <= BREATHE_MAX_PERIOD; ++period) {
        s.breath = (uint8_t)(BREATHE_MODE | period); dimmer_init(&s);
        ticks = period * BREATHE_STEP_TICKS;
        memset(seen, 0, sizeof seen);
        for (tick = 0; tick < ticks; ++tick) {
            assert(dimmer_phase() == (uint32_t)tick * BREATHE_PHASE_STEPS / ticks);
            seen[dimmer_phase()] = 1;
            (void)dimmer_tick(&e);
        }
        assert(!dimmer_phase());
        count = 0; for (tick = 0; tick < BREATHE_PHASE_STEPS; ++tick) count += seen[tick];
        assert(count == (ticks < BREATHE_PHASE_STEPS ? ticks : BREATHE_PHASE_STEPS));
    }
    e.down = e.held = BUTTON_POWER;
    s.breath = BREATHE_DEFAULT_PERIOD;
    for (low = 0; low < 100; ++low) {
        for (high = low + 1; high <= 100; ++high) {
            s.minimum = (uint8_t)low; s.maximum = (uint8_t)high;
            for (brightness = 10; brightness <= 100; ++brightness) {
                s.brightness = (uint8_t)brightness; dimmer_init(&s);
                (void)dimmer_tick(&e);
                target = brightness < low ? low : brightness > high ? high : brightness;
                error = (int)dimmer_output() - dimmer_duty((uint8_t)target);
                assert(error >= -3 && error <= 3);
            }
        }
    }
}
static void test_breathing_duty_coverage(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 1u, 0, 100};
    button_events_t e = {0};
    uint8_t seen[BOARD_PWM_PERIOD + 1];
    unsigned period, span, tick, duty, count;
    /* An offset translates duties without changing coverage, so every percentage
       span covers all 5,050 endpoint pairs without repeating identical work. */
    for (period = 1; period <= BREATHE_MAX_PERIOD; ++period) {
        s.breath = (uint8_t)(BREATHE_MODE | period);
        for (span = 1; span <= 100; ++span) {
            s.maximum = (uint8_t)span;
            dimmer_init(&s); memset(seen, 0, sizeof seen);
            for (tick = 0; tick < period * BREATHE_STEP_TICKS; ++tick) {
                duty = dimmer_output();
                assert(duty <= 4u * span);
                seen[duty] = 1;
                (void)dimmer_tick(&e);
            }
            assert(!dimmer_phase() && seen[0] && seen[4u * span]);
            count = 0;
            for (duty = 0; duty <= 4u * span; ++duty) count += seen[duty];
            if (period >= 3) assert(count == 4u * span + 1);
            else if (span == 100) assert(count == (period == 1 ? 293u : 391u));
        }
    }
}
static void test_v4_validation(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 20u, 0, 100};
    uint8_t valid[10];
    unsigned offset, bit, field, value;
    fresh(); seed_v4(0, 1, &s); memcpy(valid, eeprom, sizeof valid);
    for (offset = 0; offset < sizeof valid; ++offset) {
        for (bit = 0; bit < 8; ++bit) {
            memcpy(eeprom, valid, sizeof valid);
            eeprom[offset] ^= (uint8_t)(1u << bit);
            (void)boot(); state_is(50, 0);
        }
    }
    for (field = 4; field <= 7; ++field) {
        for (value = 0; value < 256; ++value) {
            uint8_t brightness, low, high, period;
            memcpy(eeprom, valid, sizeof valid); eeprom[field] = (uint8_t)value;
            eeprom[8] = reference_crc(eeprom + 1, 7);
            brightness = eeprom[4] & 127; low = eeprom[5] & 127;
            high = eeprom[6] & 127; period = eeprom[7];
            (void)boot();
            if (brightness >= 10 && brightness <= 100 && low < high && high <= 100 && period >= 1 && period <= 20) {
                state_is(brightness, eeprom[4] >> 7);
                assert(dimmer_state()->minimum == low && dimmer_state()->maximum == high);
                assert(dimmer_state()->breath == (uint8_t)((eeprom[6] & 128) | period));
            } else state_is(50, 0);
        }
    }
    fresh(); memcpy(eeprom + 8, valid, sizeof valid);
    (void)boot(); state_is(50, 0); /* v4 is valid only on 16-byte boundaries. */
}
static unsigned migration_cases;
static void settings_are(const dimmer_state_t *s)
{
    state_is(s->brightness, s->power_on);
    assert(dimmer_state()->breath == s->breath);
    assert(dimmer_state()->minimum == s->minimum && dimmer_state()->maximum == s->maximum);
}
static void test_legacy_migration_interruption(void)
{
    const uint8_t representative_tears[] = {0, 0xff, 0xa5};
    dimmer_state_t desired = {80, 0, BREATHE_MODE | 13u, 27, 88};
    dimmer_state_t baseline = {70, 1, BREATHE_DEFAULT_PERIOD, 10, 100};
    uint8_t image[256];
    unsigned version, slot, j, phase, tear, preceding, limit;
    for (version = 1; version <= 3; ++version) {
        baseline.breath = (uint8_t)(version == 3 ? BREATHE_MODE | 20u : BREATHE_DEFAULT_PERIOD);
        for (slot = 0; slot < 32; ++slot) {
            fresh();
            /* A full legacy ring, with the newest record at each possible
               address, including odd half-slots and sequence rollover. */
            for (j = 0; j < 32; ++j) {
                uint8_t flags = (uint8_t)(version == 1 ? 1 : version == 2 ? 3 : 207);
                seed(j, (uint16_t)(0xffffu - ((slot + 32u - j) % 32u)), legacy_duty(70), flags);
                eeprom[j * 8 + 1] = (uint8_t)version;
                eeprom[j * 8 + 7] = reference_crc(eeprom + j * 8 + 1, 6);
            }
            (void)boot(); settings_are(&baseline);
            memcpy(image, eeprom, sizeof image);
            /* Exhaust every torn value for representative even/odd layouts;
               cover all other slot/version combinations with three values. */
            limit = (version == 1 && slot == 0) || (version == 2 && slot == 31) ||
                    (version == 3 && slot == 15) ? 256 : 3;
            for (phase = 0; phase < 11; ++phase) {
                for (tear = 0; tear < limit; ++tear) {
                    uint8_t torn = limit == 256 ? (uint8_t)tear : representative_tears[tear];
                    memcpy(eeprom, image, sizeof image); (void)boot();
                    storage_request(&desired, 1);
                    for (preceding = 0; preceding < phase; ++preceding) {
                        storage_service(); assert(pending);
                        assert(pending_address < slot * 8 || pending_address >= slot * 8 + 8);
                        finish_write();
                    }
                    storage_service(); assert(pending);
                    assert(pending_address < slot * 8 || pending_address >= slot * 8 + 8);
                    eeprom[pending_address] = torn;
                    (void)boot();
                    settings_are(phase == 10 && torn == 0xa5 ? &desired : &baseline);
                    ++migration_cases;
                }
            }
            memcpy(eeprom, image, sizeof image); (void)boot();
            storage_request(&desired, 1); drain(); (void)boot(); settings_are(&desired);
        }
    }
    /* Once a full v4 rotation completes, every legacy odd-slot version is
       destroyed. A CRC byte equal to its old commit marker cannot revive it. */
    for (j = 0; j < 16; ++j) {
        desired.brightness = (uint8_t)(80 + (j & 1u));
        storage_request(&desired, 1); drain();
    }
    for (j = 0; j < 16; ++j) assert(eeprom[j * 16 + 9] == 0);
}
static void test_breathing_gesture(void)
{
    const unsigned short_holds[] = {30, BUTTON_HOLD_TICKS + 50, BUTTON_MODE_TICKS - 1};
    unsigned before, i;
    fresh(); save(50, 1); (void)boot(); before = writes;
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS - 1);
    state_is(50, 1); status_is(0);
    assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD && writes == before);
    ui_tick(BUTTON_POWER);
    assert(events.held == BUTTON_POWER);
    assert(dimmer_state()->breath == (BREATHE_MODE | BREATHE_DEFAULT_PERIOD));
    state_is(50, 1); status_is(1);
    assert(writes == before + 1); /* Mode save starts at the threshold. */
    assert(dimmer_output() >= dimmer_duty(49) && dimmer_output() <= dimmer_duty(51));
    drain(); before = writes;
    ui_ticks(BUTTON_POWER, 70000); /* One continuous hold switches only once. */
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(!(events.short_press & BUTTON_POWER));
    assert(dimmer_state()->breath == (BREATHE_MODE | BREATHE_DEFAULT_PERIOD));
    state_is(50, 1); status_is(0); assert(writes == before);
    (void)boot();
    assert(dimmer_state()->breath == (BREATHE_MODE | BREATHE_DEFAULT_PERIOD));
    hold_mode();
    assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD);
    assert(dimmer_output() == dimmer_duty(50));
    drain(); (void)boot();
    assert(!(dimmer_state()->breath & BREATHE_MODE));

    /* Every release below one second is a tap, including 400..999 ms presses.
       No hold event may occur during release debounce just below the threshold. */
    for (i = 0; i < sizeof short_holds / sizeof short_holds[0]; ++i) {
        fresh(); save(50, 1); (void)boot(); before = writes;
        ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + short_holds[i]);
        ui_ticks(0, BUTTON_DEBOUNCE_TICKS - 1); state_is(50, 1);
        assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD && writes == before);
        ui_tick(0); state_is(50, 0); status_is(0);
        assert(writes == before + 1 && !dimmer_output());
        assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD);
        click(BUTTON_POWER); state_is(50, 1); status_is(0);
    }
    click(BUTTON_POWER); state_is(50, 0);
    click(BUTTON_POWER); state_is(50, 1);
    assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD); /* Double tap is off/on. */

    fresh();
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(50, 0); status_is(0);
    assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD && !writes);
}
static void test_mode_change_flash(void)
{
    dimmer_state_t s = {50, 1, BREATHE_DEFAULT_PERIOD, 10, 100};
    /* hold_mode() includes 20 release-debounce ticks after the mode event. */
    const unsigned remaining = STATUS_MODE_FLASH_TICKS - BUTTON_DEBOUNCE_TICKS;
    unsigned mode, before;
    for (mode = 0; mode < 2; ++mode) {
        fresh(); s.breath = (uint8_t)(BREATHE_DEFAULT_PERIOD | (mode ? BREATHE_MODE : 0));
        storage_request(&s, 1); drain(); (void)boot(); status_is(0);
        hold_mode(); status_is(1);
        assert(dimmer_state()->breath == (uint8_t)(s.breath ^ BREATHE_MODE));
        state_is(50, 1);
        drain(); before = writes;
        ui_ticks(0, remaining - 1); status_is(1);
        ui_tick(0); status_is(0);
        ui_ticks(0, STORAGE_DELAY_TICKS);
        assert(writes == before); /* Flash completion never writes settings. */

        /* Each separate hold gives a new half-second indication. */
        hold_mode(); status_is(1);
        ui_ticks(0, remaining - 1); status_is(1);
        ui_tick(0); status_is(0);

        hold_mode(); status_is(1);
        click(BUTTON_POWER); state_is(50, 0); status_is(0);
        click(BUTTON_POWER); state_is(50, 1); status_is(0);

        /* Only the mode preference, not its transient indication, survives boot. */
        hold_mode(); status_is(1);
        drain(); (void)boot(); status_is(0);
        tap(BUTTON_POWER); state_is(50, 0); status_is(0);
        tap(BUTTON_UP); assert(dimmer_state()->power_on); status_is(0);

        hold_mode(); status_is(1);
        before = (dimmer_state()->breath & BREATHE_MODE) ?
                 (dimmer_state()->breath & BREATHE_PERIOD_MASK) : dimmer_state()->brightness;
        tap(BUTTON_UP); status_is(1);
        if (dimmer_state()->breath & BREATHE_MODE)
            assert((dimmer_state()->breath & BREATHE_PERIOD_MASK) == before + 1);
        else assert(dimmer_state()->brightness == before + DIMMER_STEP_PERCENT);
    }
    s.brightness = 100; s.breath = BREATHE_MODE | BREATHE_DEFAULT_PERIOD;
    fresh(); storage_request(&s, 1); drain(); (void)boot();
    hold_mode(); status_is(1); drain(); before = writes;
    ui_ticks(0, remaining - (2 * BUTTON_DEBOUNCE_TICKS + 30));
    click(BUTTON_UP); state_is(100, 1); status_is(0);
    assert(writes == before);

    s.brightness = 50; s.breath = BREATHE_DEFAULT_PERIOD;
    fresh(); storage_request(&s, 1); drain(); (void)boot();
    hold_mode(); status_is(1);
    editor_gesture(); assert(dimmer_editing() == 1); status_is(1);
    ui_ticks(0, STATUS_MODE_FLASH_TICKS); status_is(1);
    editor_gesture(); assert(!dimmer_editing()); status_is(0);
}
static void test_power_mode_overlap(void)
{
    dimmer_state_t s = {50, 1, BREATHE_DEFAULT_PERIOD, 10, 100};
    unsigned mode, button, release_order, before;
    for (mode = 0; mode < 2; ++mode) {
        for (button = BUTTON_UP; button <= BUTTON_BRIGHTNESS; button += 2) {
            for (release_order = 0; release_order < 2; ++release_order) {
                fresh();
                s.breath = (uint8_t)(BREATHE_DEFAULT_PERIOD | (mode ? BREATHE_MODE : 0));
                storage_request(&s, 1); drain(); (void)boot();
                ui_ticks((uint8_t)(BUTTON_POWER | button), BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS);
                assert(dimmer_state()->breath == (uint8_t)(s.breath ^ BREATHE_MODE));
                state_is(50, 1);
                drain(); before = writes;
                ui_ticks((uint8_t)(release_order ? BUTTON_POWER : button),
                         BUTTON_DEBOUNCE_TICKS + BUTTON_MODE_TICKS * 2);
                ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
                assert(dimmer_state()->breath == (uint8_t)(s.breath ^ BREATHE_MODE));
                state_is(50, 1); status_is(0);
                assert(!dimmer_editing() && writes == before);
                assert(dimmer_state()->minimum == 10 && dimmer_state()->maximum == 100);
            }
        }
    }
}
static void test_breathing_periods(void)
{
    dimmer_state_t s = {50, 1, BREATHE_MODE | 1u, 10, 100};
    unsigned period, i, before;
    uint16_t last, output;
    for (period = 1; period <= BREATHE_MAX_PERIOD; ++period) {
        fresh();
        s.breath = (uint8_t)(BREATHE_MODE | period);
        storage_request(&s, 1); drain(); (void)boot();
        assert(dimmer_state()->breath == s.breath);
        before = writes;
        last = dimmer_output();
        assert(last == dimmer_duty(DIMMER_MIN_PERCENT));
        for (i = 0; i < period * BREATHE_STEP_TICKS / 2; ++i) {
            ui_tick(0); output = dimmer_output();
            assert(output >= last && output <= dimmer_duty(DIMMER_MAX_PERCENT));
            last = output;
        }
        assert(last == dimmer_duty(DIMMER_MAX_PERCENT));
        for (i = 0; i < period * BREATHE_STEP_TICKS / 2; ++i) {
            ui_tick(0); output = dimmer_output();
            assert(output <= last && output >= dimmer_duty(DIMMER_MIN_PERCENT));
            last = output;
        }
        assert(last == dimmer_duty(DIMMER_MIN_PERCENT));
        assert(writes == before);
    }
    /* At 10 seconds, up wraps to 0.5; down wraps back to 10. */
    before = writes;
    tap(BUTTON_UP);
    assert(dimmer_state()->breath == (BREATHE_MODE | 1u));
    assert(writes == before);
    ui_ticks(0, STORAGE_DELAY_TICKS - 1);
    assert(writes == before);
    ui_tick(0);
    assert(writes == before + 1);
    drain();
    tap(BUTTON_DOWN);
    assert(dimmer_state()->breath == (BREATHE_MODE | 20u));
    for (i = 19; i >= 1; --i) {
        tap(BUTTON_DOWN);
        assert(dimmer_state()->breath == (BREATHE_MODE | i));
    }
    state_is(50, 1); /* Period controls do not overwrite steady brightness. */
    drain(); before = writes;
    ui_ticks(0, STORAGE_DELAY_TICKS + 50);
    assert(writes == before);
    (void)boot();
    assert(dimmer_state()->breath == (BREATHE_MODE | 1u));
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_RESET_COMMIT_TICKS + 5);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(50, 1);
    assert(dimmer_state()->breath == 1u); /* Long on-state hold toggles once. */
    hold_mode();
    assert(dimmer_state()->breath == (BREATHE_MODE | 1u));
    tap(BUTTON_POWER); drain(); (void)boot();
    state_is(50, 0);
    assert(dimmer_state()->breath == (BREATHE_MODE | 1u));
    tap(BUTTON_UP);
    state_is(50, 1);
    assert(dimmer_state()->breath == (BREATHE_MODE | 2u));
    assert(dimmer_output() == dimmer_duty(DIMMER_MIN_PERCENT));
    /* Power still cancels overlapping period gestures. */
    tap(BUTTON_POWER | BUTTON_UP); drain(); (void)boot();
    state_is(50, 0);
    assert(dimmer_state()->breath == (BREATHE_MODE | 2u));
    assert(!dimmer_status_led());
}
static void test_breathing_legacy_and_validation(void)
{
    unsigned version, flags;
    fresh();
    for (version = 1; version <= 2; ++version) {
        seed(31, 0xffff, legacy_duty(70), (uint8_t)(version == 1 ? 1 : 3));
        eeprom[31 * 8 + 1] = (uint8_t)version;
        eeprom[31 * 8 + 7] = reference_crc(eeprom + 31 * 8 + 1, 6);
        (void)boot();
        state_is(70, 1);
        assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD);
        hold_mode(); drain();
        assert(eeprom[1] == 4 && eeprom[2] == 0 && eeprom[3] == 0);
        (void)boot();
        assert(dimmer_state()->breath == (BREATHE_MODE | BREATHE_DEFAULT_PERIOD));
        assert(!dimmer_status_led());
        fresh();
    }
    /* Reject every reserved period encoding, even with a valid CRC. */
    for (flags = 20; flags < 32; ++flags) {
        seed(0, 1, legacy_duty(80), (uint8_t)((flags << 2) | 129u));
        eeprom[1] = 3; eeprom[7] = reference_crc(eeprom + 1, 6);
        (void)boot(); state_is(50, 0);
        assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD);
    }
}
static void test_obsolete_indicator_preference(void)
{
    dimmer_state_t s = {70, 0, BREATHE_MODE | 20u, 23, 87};
    unsigned flag, before;
    for (flag = 0; flag < 2; ++flag) {
        fresh(); seed_v4(0, 1, &s);
        eeprom[5] |= (uint8_t)(flag << 7); /* Obsolete indicator preference is ignored. */
        eeprom[8] = reference_crc(eeprom + 1, 7);
        (void)boot(); settings_are(&s); assert(!dimmer_status_led());
        before = writes; ui_ticks(0, STORAGE_DELAY_TICKS + 20);
        assert(writes == before);
        tap(BUTTON_POWER); drain();
        assert(eeprom[16 + 5] == s.minimum);
        (void)boot(); s.power_on = 1; settings_are(&s);
        assert(!dimmer_status_led()); s.power_on = 0;
    }
}
static void test_records_and_rotation(void)
{
    unsigned i, offset, bit;
    uint8_t valid[8];
    assert(reference_crc((const uint8_t *)"123456789", 9) == 0xf4);
    fresh();
    for (i = 0; i < 96; ++i) {
        save((uint8_t)(50 + (i & 1)), 1);
        assert(eeprom[(i % 16) * 16] == 0xa5);
        assert(eeprom[(i % 16) * 16 + 2] == i + 1);
        assert(eeprom[(i % 16) * 16 + 9] == 0);
    }
    for (i = 0; i < 16; ++i) assert(eeprom[i * 16] == 0xa5);
    (void)boot(); state_is(51, 1);
    fresh();
    seed(30, 0xfffe, 512, 1);
    seed(31, 0xffff, 614, 0);
    seed(0, 0, 716, 1);
    (void)boot(); state_is(70, 1);
    save(80, 0);
    assert(eeprom[16 + 2] == 1 && eeprom[16 + 3] == 0);
    (void)boot(); state_is(80, 0);

    fresh(); seed(0, 12, 512, 1);
    memcpy(valid, eeprom, 8);
    for (offset = 0; offset < 8; ++offset) {
        for (bit = 0; bit < 8; ++bit) {
            memcpy(eeprom, valid, 8);
            eeprom[offset] ^= (uint8_t)(1u << bit);
            (void)boot(); state_is(50, 0);
        }
    }
    fresh(); seed(0, 1, 512, 2); (void)boot(); state_is(50, 0);
    fresh(); seed(0, 1, 0, 1); (void)boot(); state_is(50, 0);
    fresh(); seed(0, 1, 1024, 1); (void)boot(); state_is(50, 0);
    fresh(); seed(0, 1, 512, 1);
    eeprom[1] = 5; eeprom[7] = reference_crc(eeprom + 1, 6);
    (void)boot(); state_is(50, 0);
}

static void interrupt_matrix(uint8_t baseline_level, uint8_t baseline_on)
{
    unsigned phase, torn, preceding;
    uint8_t baseline[256];
    dimmer_state_t desired = {90, 0, BREATHE_MODE | 20u, 23, 87};
    memcpy(baseline, eeprom, sizeof baseline);
    /* Eleven writes: invalidate, nine payload bytes, final marker. For every
       write, test every possible torn byte, including the intended value. */
    for (phase = 0; phase < 11; ++phase) {
        for (torn = 0; torn < 256; ++torn) {
            memcpy(eeprom, baseline, sizeof baseline);
            (void)boot();
            storage_request(&desired, 1);
            for (preceding = 0; preceding < phase; ++preceding) {
                storage_service(); assert(pending); finish_write();
            }
            storage_service(); assert(pending);
            eeprom[pending_address] = (uint8_t)torn;
            (void)boot();
            if (phase == 10 && torn == 0xa5) {
                state_is(90, 0);
                assert(dimmer_state()->breath == desired.breath);
                assert(dimmer_state()->minimum == 23 && dimmer_state()->maximum == 87);
            } else {
                state_is(baseline_level, baseline_on);
                assert(dimmer_state()->breath == BREATHE_DEFAULT_PERIOD);
                assert(dimmer_state()->minimum == 10 && dimmer_state()->maximum == 100);
            }
        }
    }
    memcpy(eeprom, baseline, sizeof baseline);
    (void)boot();
    storage_request(&desired, 1);
    for (phase = 0; phase < 11; ++phase) {
        storage_service(); assert(pending); finish_write();
    }
    /* Loss after commit completion, before RAM bookkeeping, also recovers. */
    (void)boot(); state_is(90, 0);
    assert(dimmer_state()->breath == desired.breath);
    assert(dimmer_state()->minimum == 23 && dimmer_state()->maximum == 87);
}
static void test_power_interruption(void)
{
    unsigned i;
    fresh();
    interrupt_matrix(50, 0);
    fresh();
    for (i = 0; i < 32; ++i) save((uint8_t)(40 + (i & 1)), 1);
    interrupt_matrix(41, 1);
}
static void test_reset_interruption(void)
{
    const dimmer_state_t baseline = {80, 0, BREATHE_MODE | 17u, 23, 87};
    const dimmer_state_t defaults = {50, 0, 6, 10, 100};
    uint8_t image[256];
    unsigned phase, torn, preceding;
    fresh(); storage_request(&baseline, 1); drain(); memcpy(image, eeprom, sizeof image);
    for (phase = 0; phase < 11; ++phase) {
        for (torn = 0; torn < 256; ++torn) {
            memcpy(eeprom, image, sizeof image); (void)boot();
            storage_request(&defaults, 1);
            for (preceding = 0; preceding < phase; ++preceding) {
                storage_service(); assert(pending); finish_write();
            }
            storage_service(); assert(pending);
            eeprom[pending_address] = (uint8_t)torn;
            (void)boot();
            settings_are(phase == 10 && torn == 0xa5 ? &defaults : &baseline);
        }
    }
}
static void test_failed_writes(void)
{
    unsigned fail_index, i, before;
    uint8_t baseline[256];
    dimmer_state_t s = {60, 0, BREATHE_DEFAULT_PERIOD, 12, 96};
    fresh(); save(50, 1);
    memcpy(baseline, eeprom, sizeof baseline);
    for (fail_index = 0; fail_index < 11; ++fail_index) {
        memcpy(eeprom, baseline, sizeof baseline);
        (void)boot();
        storage_request(&s, 1);
        for (i = 0; i < fail_index; ++i) {
            storage_service(); assert(pending); finish_write();
        }
        storage_service(); assert(pending);
        eeprom[pending_address] = (uint8_t)(pending_value ^ 1u);
        pending = 0;
        storage_service();
        assert(storage_fault());
        before = writes;
        for (i = 0; i < STORAGE_RETRY_TICKS - 1; ++i) {
            storage_tick(); storage_service();
        }
        assert(writes == before); /* A failure cannot cause a tight wear loop. */
        drain();
        assert(!storage_fault());
        (void)boot(); state_is(60, 0);
        assert(dimmer_state()->minimum == s.minimum && dimmer_state()->maximum == s.maximum);
    }
}

int main(void)
{
    test_pwm_scaling();
    test_sine_curve_and_levels();
    test_sine_timing_and_entry();
    test_breathing_duty_coverage();
    test_buttons_and_dimming();
    test_chords();
    test_power_long_hold_event();
    test_power_overlap();
    test_settings_reset();
    test_editor_gestures_and_save();
    test_editor_status_blink();
    test_editor_chord_rearming();
    test_editor_bounds_and_ramping();
    test_custom_bounds_mode_entry();
    test_all_editor_ranges();
    test_v4_validation();
    test_legacy_migration_interruption();
    test_breathing_gesture();
    test_mode_change_flash();
    test_power_mode_overlap();
    test_breathing_periods();
    test_breathing_legacy_and_validation();
    test_obsolete_indicator_preference();
    test_save_policy();
    test_inflight_changes();
    test_records_and_rotation();
    test_power_interruption();
    test_reset_interruption();
    test_failed_writes();
    puts("PASS: PWM scaling, breathing, editor/status LED, all 5,050 valid min/max pairs, six-second reset, power overlaps, save policy, CRC, and rotation.");
    printf("      5,632 journal, %u migration, and 2,816 reset torn-write cases; EEPROM verification failures.\n", migration_cases);
    return 0;
}
