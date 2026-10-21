/* v6 journal: CRC-covered retirement maps prevent skipped, read-only records
   from participating in sequence ordering after subsequent rotations. */
#include "board.h"
#include "hal.h"
#include "storage.h"

#define VERSION 6u
#define COMMIT 0xa5u
#define RECORD_SIZE 11u
#define IDLE 0u
#define INVALIDATE 1u
#define PAYLOAD 2u
#define VERIFY_COMMIT 3u
#define STOPPED 4u
#define FIRST_FAILURE 1u
#define MAP_DIRTY 64u
#define FAULT 128u

static dimmer_state_t saved, requested, snapshot;
static uint16_t sequence, delay_ticks;
/* Bit 0 retries a transaction once; bit 6 requires a durable retirement save;
   bit 7 requests the warning until that save verifies or capacity is exhausted. */
static uint8_t last_address, failures, phase, position, target_address;
static uint8_t record[RECORD_SIZE];
/* The marker byte is read only at boot. Runtime writes use constants, leaving
   this byte free for a 131.072 ms on/off clock outside the CRC-covered payload. */
#define fault_clock record[0]
#define address(offset) ((uint8_t)(target_address + (offset)))
/* After boot scanning, the packet owns the live map. No additional byte copy
   is needed: only a failed/aborted transaction can add a newly retired slot. */
#define retired_value() ((uint16_t)(record[8] | ((uint16_t)record[9] << 8)))

static uint16_t slot_bit(uint8_t address)
{
    return (uint16_t)((uint16_t)1u << (address >> 4));
}
static uint8_t crc8(void)
{
    uint8_t crc = 0, bit, n = 9;
    const uint8_t *p = record + 1;
    do {
        crc ^= *p++; bit = 8;
        do {
            if (crc & 128u) { crc <<= 1; crc ^= 7u; }
            else crc <<= 1;
        } while (--bit);
    } while (--n);
    return crc;
}
static uint8_t read_record(uint8_t slot)
{
    uint8_t i;
    for (i = 0; i < RECORD_SIZE; ++i)
        record[i] = hal_eeprom_read((uint8_t)(slot + i));
    return record[0] == COMMIT && record[1] == VERSION && record[10] == crc8() &&
        (record[4] & 127u) <= 100u &&
        (record[5] <= 100u || record[5] == DIMMER_LOWEST) &&
        (record[5] & 127u) < (record[6] & 127u) && (record[6] & 127u) <= 100u &&
        (uint8_t)(record[7] - 1u) < BREATHE_MAX_PERIOD;
}
static uint8_t advance_target(void)
{
    uint8_t n = 16;
    /* Bound the scan when every destination is retired or protected. */
    do {
        target_address += 16u;
        if (target_address != last_address && !(retired_value() & slot_bit(target_address))) return 1;
    } while (--n);
    phase = STOPPED; failures |= FAULT;
    return 0;
}
void storage_init(dimmer_state_t *restored)
{
    uint8_t slot = 0;
    uint16_t difference, retired = 0;
    phase = IDLE; failures = 0;
    sequence = delay_ticks = 0;
    last_address = 0xffu;
    dimmer_defaults(&saved);
    /* OR all CRC-verified maps before ordering records. Old maps cannot
       unretire a slot, even when a read-only record survives sequence wrap. */
    do {
        if (read_record(slot))
            retired |= (uint16_t)(record[8] | ((uint16_t)record[9] << 8));
    } while ((slot += 16u) != 0);
    do {
        if ((retired & slot_bit(slot)) || !read_record(slot)) continue;
        difference = (uint16_t)((uint16_t)(record[2] | ((uint16_t)record[3] << 8)) - sequence);
        if (last_address == 0xffu || (difference && difference < 0x8000u)) {
            sequence += difference; last_address = slot;
            saved.brightness = record[4] & 127u;
            saved.power_on = record[4] >> 7;
            saved.minimum = record[5];
            saved.maximum = record[6] & 127u;
            saved.breath = (uint8_t)((record[6] & BREATHE_MODE) | record[7]);
        }
    } while ((slot += 16u) != 0);
    record[8] = (uint8_t)retired; record[9] = (uint8_t)(retired >> 8);
    target_address = last_address & 240u;
    (void)advance_target();
    DIMMER_COPY(&requested, &saved); DIMMER_COPY(restored, &saved);
}
static uint8_t pending_changes(void)
{
    const uint8_t *x = (const uint8_t *)&requested, *y = (const uint8_t *)&saved;
    uint8_t n = sizeof requested;
    if (failures & MAP_DIRTY) return 1;
    do { if (*x++ != *y++) return 1; } while (--n);
    return 0;
}
void storage_request(const dimmer_state_t *s, uint8_t immediate)
{
    DIMMER_COPY(&requested, s);
    if (!immediate) {
        /* A new adjustment extends quiet time without shortening fault backoff. */
        if (delay_ticks < STORAGE_DELAY_TICKS) delay_ticks = STORAGE_DELAY_TICKS;
    } else if (!failures) delay_ticks = 0;
}
void storage_tick(void)
{
    ++fault_clock;
    if (delay_ticks) --delay_ticks;
}
uint8_t storage_status_led(uint8_t normal)
{
    if (failures & FAULT) return fault_clock & 128u;
    return normal;
}
#ifdef FIRMWARE_TEST
uint8_t storage_fault(void) { return (failures & FAULT) != 0; }
uint8_t storage_busy(void)
{
    return phase != STOPPED && (phase != IDLE || hal_eeprom_busy() || pending_changes());
}
uint16_t storage_retired(void) { return retired_value(); }
uint8_t storage_stopped(void) { return phase == STOPPED; }
#endif
static void fail(void)
{
    uint16_t bit;
    phase = IDLE; delay_ticks = STORAGE_RETRY_TICKS;
    failures ^= FIRST_FAILURE; /* First failure retries; second retires the slot. */
    if (!(failures & FIRST_FAILURE)) {
        bit = slot_bit(target_address);
        record[8] |= (uint8_t)bit; record[9] |= (uint8_t)(bit >> 8);
        failures |= MAP_DIRTY | FAULT;
        (void)advance_target();
    }
}
void storage_service(void)
{
    uint16_t next;
    if (phase == STOPPED || hal_eeprom_busy()) return;
    if (phase == IDLE) {
        if (delay_ticks || !pending_changes()) return;
        DIMMER_COPY(&snapshot, &requested);
        next = (uint16_t)(sequence + 1u);
        record[1] = VERSION; record[2] = (uint8_t)next; record[3] = (uint8_t)(next >> 8);
        record[4] = (uint8_t)(snapshot.brightness | (snapshot.power_on << 7));
        record[5] = snapshot.minimum;
        record[6] = (uint8_t)(snapshot.maximum | (snapshot.breath & BREATHE_MODE));
        record[7] = snapshot.breath & BREATHE_PERIOD_MASK;
        record[10] = crc8();
        hal_eeprom_start_write(address(0), 0); phase = INVALIDATE; return;
    }
    if (phase == INVALIDATE) {
        if (hal_eeprom_read(address(0)) != 0) { fail(); return; }
        position = 1; phase = PAYLOAD;
    } else if (phase == PAYLOAD) {
        if (hal_eeprom_read(address(position)) != record[position]) { fail(); return; }
        if (++position == RECORD_SIZE) {
            hal_eeprom_start_write(address(0), COMMIT); phase = VERIFY_COMMIT; return;
        }
    } else if (phase == VERIFY_COMMIT) {
        if (hal_eeprom_read(address(0)) != COMMIT) { fail(); return; }
        DIMMER_COPY(&saved, &snapshot);
        sequence = (uint16_t)(record[2] | ((uint16_t)record[3] << 8));
        last_address = target_address;
        phase = IDLE; failures = 0; (void)advance_target(); return;
    }
    hal_eeprom_start_write(address(position), record[position]);
}
