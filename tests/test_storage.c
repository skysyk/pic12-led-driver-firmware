#ifdef NDEBUG
#error Tests require assertions
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "storage.h"

static uint8_t eeprom[256], stuck[256], pending, pending_address, pending_value;
static unsigned writes, writes_at[256], torn_cases;
static const dimmer_state_t baseline = {50, 1, BREATHE_DEFAULT_PERIOD, 10, 100};

uint8_t hal_eeprom_read(uint8_t address) { assert(!pending); return eeprom[address]; }
uint8_t hal_eeprom_busy(void) { return pending; }
void hal_eeprom_start_write(uint8_t address, uint8_t value)
{
    assert(!pending); pending = 1; pending_address = address; pending_value = value;
    ++writes; ++writes_at[address];
}
static void finish(void)
{
    assert(pending);
    if (!stuck[pending_address]) eeprom[pending_address] = pending_value;
    pending = 0;
}
static dimmer_state_t boot(void)
{
    dimmer_state_t s;
    pending = 0; storage_init(&s); dimmer_init(&s);
    return s;
}
static void same(const dimmer_state_t *a, const dimmer_state_t *b)
{
    assert(a->brightness == b->brightness && a->power_on == b->power_on &&
        a->breath == b->breath && a->minimum == b->minimum && a->maximum == b->maximum);
}
static void fresh(void)
{
    memset(eeprom, 255, sizeof eeprom); memset(stuck, 0, sizeof stuck);
    memset(writes_at, 0, sizeof writes_at); writes = pending = 0; (void)boot();
}
static uint8_t crc(const uint8_t *p, unsigned n)
{
    uint8_t c = 0;
    unsigned bit;
    while (n--) {
        c ^= *p++;
        for (bit = 0; bit < 8; ++bit) c = (uint8_t)((c << 1) ^ (c & 128u ? 7u : 0));
    }
    return c;
}
static void seed(unsigned slot, uint16_t sequence, const dimmer_state_t *s, uint16_t map)
{
    uint8_t *r = eeprom + slot * 16;
    r[0] = 0xa5; r[1] = 6; r[2] = (uint8_t)sequence; r[3] = (uint8_t)(sequence >> 8);
    r[4] = (uint8_t)(s->brightness | (s->power_on << 7)); r[5] = s->minimum;
    r[6] = (uint8_t)(s->maximum | (s->breath & BREATHE_MODE));
    r[7] = s->breath & BREATHE_PERIOD_MASK;
    r[8] = (uint8_t)map; r[9] = (uint8_t)(map >> 8); r[10] = crc(r + 1, 9);
}
static void ring(unsigned newest)
{
    unsigned slot;
    fresh();
    for (slot = 0; slot < 16; ++slot)
        seed(slot, (uint16_t)(0xffffu - ((newest + 16u - slot) % 16u)), &baseline, 0);
    (void)boot();
}
static void wait_ticks(unsigned ticks)
{
    unsigned before = writes, i;
    for (i = 1; i < ticks; ++i) {
        storage_tick(); storage_service(); assert(!pending && writes == before);
    }
    storage_tick();
}
static unsigned attempt(unsigned slot)
{
    unsigned n = 0;
    storage_service(); assert(pending);
    do {
        assert(++n <= 12 && pending_address / 16 == slot);
        finish(); storage_service();
    } while (pending);
    return n;
}
static void drain(void)
{
    unsigned n = 0;
    while (storage_busy()) {
        assert(++n < 40000);
        storage_tick(); storage_service(); if (pending) finish();
    }
    assert(!storage_stopped());
}
static void test_long_rotation(void)
{
    dimmer_state_t desired = baseline, restored;
    unsigned i;
    ring(15); stuck[0] = 1;
    for (i = 0; i < 70000; ++i) {
        desired.brightness = (uint8_t)(60 + (i & 1u));
        storage_request(&desired, 1); drain();
        assert(storage_retired() == 1);
        if (i % 257u == 0 || (i >= 32752u && i < 32784u) ||
            (i >= 65520u && i < 65552u)) {
            restored = boot(); same(&restored, &desired); assert(storage_retired() == 1);
        }
    }
    restored = boot(); same(&restored, &desired);
    assert(eeprom[0] == 0xa5 && eeprom[1] == 6 && writes_at[0] == 2);
}
static void test_multiple_failures(void)
{
    dimmer_state_t desired = {7, 0, BREATHE_MODE | 20u, DIMMER_LOWEST, 100}, restored;
    unsigned i;
    uint8_t protected_bytes[16];
    ring(15); memcpy(protected_bytes, eeprom + 240, 16);
    stuck[0] = 1; stuck[16 + 8] = 1; /* Marker and bitmap-byte failures. */
    storage_request(&desired, 1); drain();
    assert(storage_retired() == 3 && !memcmp(protected_bytes, eeprom + 240, 16));
    restored = boot(); same(&restored, &desired); assert(storage_retired() == 3);
    memset(stuck, 0, sizeof stuck);
    for (i = 0; i < 40; ++i) {
        desired.brightness = (uint8_t)(70 + (i & 1u)); storage_request(&desired, 1); drain();
        restored = boot(); same(&restored, &desired); assert(storage_retired() == 3);
    }
    assert(writes_at[0] == 2 && writes_at[16] == 2); /* Permanently retired. */
}
static void test_metadata_only(void)
{
    dimmer_state_t desired = baseline, restored;
    ring(15); stuck[0] = 1;
    desired.brightness = 60; storage_request(&desired, 1);
    assert(attempt(0) == 1); wait_ticks(STORAGE_RETRY_TICKS);
    assert(attempt(0) == 1); assert(storage_retired() == 1);
    storage_request(&baseline, 1); /* Settings reverted; retirement still needs saving. */
    drain(); restored = boot(); same(&restored, &baseline);
    assert(storage_retired() == 1 && eeprom[16 + 8] == 1);
}
static void test_boot_map_union(void)
{
    dimmer_state_t desired = baseline, restored;
    fresh();
    /* Maps remain authoritative even when their own slots become retired. */
    seed(0, 0, &baseline, 4); seed(2, 0x8000, &baseline, 1);
    desired.brightness = 70; seed(3, 0xffff, &desired, 4);
    restored = boot(); same(&restored, &desired); assert(storage_retired() == 5);
    desired.brightness = 71; storage_request(&desired, 1); drain();
    restored = boot(); same(&restored, &desired); assert(storage_retired() == 5);
    assert(writes_at[0] == 0 && writes_at[32] == 0 && eeprom[4 * 16 + 8] == 5);
    /* A damaged CRC must never contribute retirement bits. */
    fresh(); seed(0, 1, &baseline, 4); eeprom[10] ^= 1;
    seed(15, 2, &desired, 0); restored = boot();
    same(&restored, &desired); assert(!storage_retired());
}
static void test_two_or_one_slots(void)
{
    dimmer_state_t desired = baseline, restored;
    unsigned i, before;
    fresh(); seed(0, 0xfffe, &baseline, 0x7ffe); seed(15, 0xffff, &baseline, 0x7ffe);
    (void)boot();
    for (i = 0; i < 100; ++i) {
        desired.brightness = (uint8_t)(60 + (i & 1u)); storage_request(&desired, 1); drain();
        restored = boot(); same(&restored, &desired); assert(storage_retired() == 0x7ffe);
        assert(!storage_stopped());
    }
    for (i = 16; i < 240; ++i) assert(writes_at[i] == 0);
    fresh(); seed(15, 20, &baseline, 0x7fff); restored = boot(); same(&restored, &baseline);
    assert(storage_stopped() && storage_fault()); before = writes;
    desired.brightness = 80; storage_request(&desired, 1);
    for (i = 0; i < 20000; ++i) { storage_tick(); storage_service(); }
    assert(writes == before && !storage_busy());
    fresh(); memset(stuck, 1, sizeof stuck); storage_request(&desired, 1);
    for (i = 0; i < 40000 && !storage_stopped(); ++i) {
        storage_tick(); storage_service(); if (pending) finish();
    }
    assert(storage_stopped() && storage_retired() == 0xffff && storage_fault() && writes == 32);
}
static void test_torn_retirement(void)
{
    const dimmer_state_t desired = {7, 0, BREATHE_MODE | 20u, DIMMER_LOWEST, 100};
    uint8_t image[256];
    dimmer_state_t restored;
    unsigned bad, latest, target, stage, torn, preceding;
    for (bad = 0; bad <= 8; bad += 8) {
        latest = (bad + 15) % 16; target = (bad + 1) % 16;
        ring(latest); memcpy(image, eeprom, sizeof image); stuck[bad * 16] = 1;
        for (stage = 0; stage < 12; ++stage) for (torn = 0; torn < 256; ++torn) {
            memcpy(eeprom, image, sizeof image); (void)boot(); storage_request(&desired, 1);
            assert(attempt(bad) == 1); wait_ticks(STORAGE_RETRY_TICKS);
            assert(attempt(bad) == 1); wait_ticks(STORAGE_RETRY_TICKS);
            for (preceding = 0; preceding < stage; ++preceding) {
                storage_service(); assert(pending && pending_address / 16 == target); finish();
            }
            storage_service(); assert(pending && pending_address / 16 == target);
            eeprom[pending_address] = (uint8_t)torn;
            restored = boot();
            if (stage == 11 && torn == 0xa5) {
                same(&restored, &desired); assert(storage_retired() == (1u << bad));
            } else { same(&restored, &baseline); assert(storage_retired() == 0); }
            ++torn_cases;
        }
    }
}
static void test_format_and_corruption(void)
{
    unsigned version, byte, bit;
    uint8_t image[256];
    dimmer_state_t restored;
    fresh(); seed(0, 1, &baseline, 0); memcpy(image, eeprom, sizeof image);
    for (byte = 0; byte < 11; ++byte) for (bit = 0; bit < 8; ++bit) {
        memcpy(eeprom, image, sizeof image); eeprom[byte] ^= (uint8_t)(1u << bit);
        restored = boot(); assert(!restored.power_on && restored.brightness == 50);
    }
    for (version = 1; version < 6; ++version) {
        memcpy(eeprom, image, sizeof image); eeprom[1] = (uint8_t)version;
        eeprom[10] = crc(eeprom + 1, 9); restored = boot();
        assert(!restored.power_on && restored.brightness == 50);
    }
}
int main(void)
{
    test_format_and_corruption(); test_metadata_only(); test_boot_map_union(); test_multiple_failures();
    test_two_or_one_slots(); test_torn_retirement(); test_long_rotation();
    printf("PASS: 70000 saves across sequence wrap/reboots, durable retirement, healthy-slot reuse, capacity exhaustion, and %u torn metadata writes.\n", torn_cases);
    return 0;
}
