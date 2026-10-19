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
    dimmer_state_t s = {brightness, power_on, 0};
    storage_request(&s, 1);
    drain();
}
static void ui_tick(uint8_t raw)
{
    uint8_t changed;
    storage_tick();
    buttons_tick(raw, &events);
    changed = dimmer_tick(&events);
    if (changed & DIMMER_CHANGED) storage_request(dimmer_state(), changed & DIMMER_IMMEDIATE_SAVE);
    storage_service();
    if (pending) finish_write();
}
static void ui_ticks(uint8_t raw, unsigned n)
{
    while (n--) ui_tick(raw);
}
static void tap(uint8_t mask)
{
    ui_ticks(mask, BUTTON_DEBOUNCE_TICKS + 30);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
}
static void state_is(uint8_t brightness, uint8_t power_on)
{
    assert(dimmer_state()->brightness == brightness);
    assert(dimmer_state()->power_on == power_on);
}
static void test_buttons_and_dimming(void)
{
    unsigned i;
    fresh();
    state_is(50, 0);
    assert(dimmer_output() == 0);
    assert(dimmer_duty(10) == 102 && dimmer_duty(50) == 512);
    assert(dimmer_duty(100) == 1023);
    for (i = 0; i < 30; ++i) {
        ui_tick(BUTTON_POWER);
        ui_tick(0);
    }
    state_is(50, 0); /* Bounce never reaches stable down. */
    assert(writes == 0);
    tap(BUTTON_POWER);
    state_is(50, 1);
    drain();
    assert(dimmer_output() == 512);
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
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_HOLD_TICKS + 50);
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
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS - 1; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.long_held);
    }
    buttons_tick(BUTTON_POWER, &e);
    assert(e.long_held == BUTTON_POWER);
    for (i = 0; i < 70000; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.long_held); /* No repeat or 16-bit timer wraparound. */
    }
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS; ++i) buttons_tick(0, &e);
    assert((e.released & BUTTON_POWER) && !(e.short_press & BUTTON_POWER));

    buttons_init();
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS - 1; ++i)
        buttons_tick(BUTTON_POWER, &e);
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS; ++i) {
        buttons_tick(0, &e);
        assert(!e.long_held);
    }
    for (i = 0; i < BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS - 1; ++i) {
        buttons_tick(BUTTON_POWER, &e);
        assert(!e.long_held); /* Separate presses must not accumulate time. */
    }
    buttons_tick(BUTTON_POWER, &e);
    assert(e.long_held == BUTTON_POWER);
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
static void test_standby_led_disable(void)
{
    unsigned before;
    fresh();
    assert(dimmer_standby_led());
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS - 1);
    assert(dimmer_standby_led());
    ui_tick(BUTTON_POWER);
    assert(!dimmer_standby_led());
    state_is(50, 0);
    assert(dimmer_output() == 0 && writes == 1); /* Preference starts saving immediately. */
    drain();
    before = writes;
    ui_ticks(BUTTON_POWER, 70000);
    assert(!dimmer_standby_led());
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(50, 0);
    assert(!dimmer_standby_led() && writes == before);
    tap(BUTTON_POWER);
    state_is(50, 1);
    assert(!dimmer_standby_led());
    tap(BUTTON_POWER);
    state_is(50, 0);
    assert(!dimmer_standby_led()); /* Normal on/off cycles do not re-enable it. */
    drain();
    before = writes;
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS + 20);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(!dimmer_standby_led() && writes == before); /* Disable, never toggle. */
    (void)boot();
    assert(!dimmer_standby_led()); /* Preference survives power loss. */
    assert(dimmer_state()->standby_led_disabled);

    fresh();
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS - 1);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    assert(dimmer_standby_led()); /* Releasing before five seconds does nothing. */
    state_is(50, 0);
    tap(BUTTON_POWER);
    drain();
    before = writes;
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS + 20);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(50, 1);
    assert(dimmer_output() == 512 && writes == before);
    tap(BUTTON_POWER);
    state_is(50, 0);
    assert(dimmer_standby_led()); /* A five-second hold while on is ignored. */

    fresh();
    ui_ticks(BUTTON_POWER | BUTTON_UP, BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS);
    assert(!dimmer_standby_led());
    ui_ticks(BUTTON_UP, BUTTON_DEBOUNCE_TICKS + BUTTON_RAMP_TICKS * 4);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    state_is(50, 0); /* LED control retains power-overlap cancellation. */
    drain();
    (void)boot();
    assert(!dimmer_standby_led() && dimmer_state()->standby_led_disabled);
}
static void test_save_policy(void)
{
    unsigned before, i;
    dimmer_state_t s = {60, 1, 0};
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
    dimmer_state_t s = {60, 1, 0};
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
static void test_standby_preference_legacy_records(void)
{
    uint8_t original[8];
    unsigned before;
    fresh();
    seed(31, 0xffff, dimmer_duty(70), 0); /* Version 1 predates the preference. */
    memcpy(original, eeprom + 31 * 8, 8);
    (void)boot(); state_is(70, 0);
    assert(dimmer_standby_led() && !dimmer_state()->standby_led_disabled);
    ui_ticks(BUTTON_POWER, BUTTON_DEBOUNCE_TICKS + BUTTON_STANDBY_HOLD_TICKS);
    ui_ticks(0, BUTTON_DEBOUNCE_TICKS);
    drain();
    assert(eeprom[1] == 2 && eeprom[2] == 0 && eeprom[3] == 0 && eeprom[6] == 2);
    assert(memcmp(original, eeprom + 31 * 8, 8) == 0);
    before = writes;
    (void)boot(); state_is(70, 0);
    assert(!dimmer_standby_led() && dimmer_state()->standby_led_disabled);
    ui_ticks(0, STORAGE_DELAY_TICKS + 20);
    assert(writes == before); /* Boot migration does not write EEPROM. */
    tap(BUTTON_POWER); drain();
    assert(eeprom[8 + 1] == 2 && eeprom[8 + 6] == 3);
    (void)boot(); state_is(70, 1);
    assert(dimmer_state()->standby_led_disabled);
    /* Unknown v2 flag bits invalidate the record even with a matching CRC. */
    seed(2, 2, dimmer_duty(80), 4);
    eeprom[2 * 8 + 1] = 2;
    eeprom[2 * 8 + 7] = reference_crc(eeprom + 2 * 8 + 1, 6);
    (void)boot(); state_is(70, 1);
    assert(dimmer_state()->standby_led_disabled);
}
static void test_records_and_rotation(void)
{
    unsigned i, offset, bit;
    uint8_t valid[8];
    assert(reference_crc((const uint8_t *)"123456789", 9) == 0xf4);
    fresh();
    for (i = 0; i < 96; ++i) {
        save((uint8_t)(50 + (i & 1)), 1);
        assert(eeprom[(i % 32) * 8] == 0xa5);
        assert(eeprom[(i % 32) * 8 + 2] == i + 1);
    }
    for (i = 0; i < 32; ++i) assert(eeprom[i * 8] == 0xa5);
    (void)boot(); state_is(51, 1);
    fresh();
    seed(30, 0xfffe, 512, 1);
    seed(31, 0xffff, 614, 0);
    seed(0, 0, 716, 1);
    (void)boot(); state_is(70, 1);
    save(80, 0);
    assert(eeprom[8 + 2] == 1 && eeprom[8 + 3] == 0);
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
    eeprom[1] = 3; eeprom[7] = reference_crc(eeprom + 1, 6);
    (void)boot(); state_is(50, 0);
}

static void interrupt_matrix(uint8_t baseline_level, uint8_t baseline_on)
{
    unsigned phase, torn, preceding;
    uint8_t baseline[256];
    dimmer_state_t desired = {90, 0, 1};
    memcpy(baseline, eeprom, sizeof baseline);
    /* Nine writes: invalidate, seven payload bytes, final marker. For every
       write, test every possible torn byte, including the intended value. */
    for (phase = 0; phase < 9; ++phase) {
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
            if (phase == 8 && torn == 0xa5) {
                state_is(90, 0);
                assert(dimmer_state()->standby_led_disabled);
            } else {
                state_is(baseline_level, baseline_on);
                assert(!dimmer_state()->standby_led_disabled);
            }
        }
    }
    memcpy(eeprom, baseline, sizeof baseline);
    (void)boot();
    storage_request(&desired, 1);
    for (phase = 0; phase < 9; ++phase) {
        storage_service(); assert(pending); finish_write();
    }
    /* Loss after commit completion, before RAM bookkeeping, also recovers. */
    (void)boot(); state_is(90, 0);
    assert(dimmer_state()->standby_led_disabled);
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
static void test_failed_writes(void)
{
    unsigned fail_index, i, before;
    uint8_t baseline[256];
    dimmer_state_t s = {60, 0, 1};
    fresh(); save(50, 1);
    memcpy(baseline, eeprom, sizeof baseline);
    for (fail_index = 0; fail_index < 9; ++fail_index) {
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
        assert(dimmer_state()->standby_led_disabled);
    }
}

int main(void)
{
    test_buttons_and_dimming();
    test_chords();
    test_power_long_hold_event();
    test_power_overlap();
    test_standby_led_disable();
    test_standby_preference_legacy_records();
    test_save_policy();
    test_inflight_changes();
    test_records_and_rotation();
    test_power_interruption();
    test_failed_writes();
    puts("PASS: buttons, dimming, power overlaps, five-second standby LED disable, save policy, async writes, CRC, rotation, sequence rollover,");
    puts("      4,608 torn-write recovery cases, and EEPROM verification failures.");
    return 0;
}
