#include "board.h"
#include "hal.h"
#include "storage.h"

#define SLOT_COUNT 32u
#define RECORD_SIZE 8u
#define COMMIT 0xa5u
#define LEGACY_VERSION 1u
#define VERSION 2u
#define IDLE 0u
#define INVALIDATE 1u
#define PAYLOAD 2u
#define COMMITTING 3u
#define VERIFY_COMMIT 4u

static dimmer_state_t saved, requested, snapshot;
static uint16_t sequence, delay_ticks;
static uint8_t last_slot, have_saved, phase, position, target_slot, fault;
static uint8_t record[RECORD_SIZE];

static uint8_t equal(const dimmer_state_t *a, const dimmer_state_t *b)
{
    return a->brightness == b->brightness && a->power_on == b->power_on &&
           a->standby_led_disabled == b->standby_led_disabled;
}

static uint8_t crc8(void)
{
    uint8_t crc = 0, i, bit;
    /* CRC-8/SMBUS: polynomial 0x07, init 0, no reflection/xorout. */
    for (i = 1; i <= 6; ++i) {
        crc ^= record[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc & 0x80u) ? ((uint16_t)crc << 1) ^ 0x07u : (uint16_t)crc << 1);
    }
    return crc;
}

static uint8_t address(uint8_t offset)
{
    return (uint8_t)(target_slot * RECORD_SIZE + offset);
}

void storage_init(dimmer_state_t *restored)
{
    uint8_t slot, i;
    uint16_t candidate, difference, duty;
    phase = IDLE;
    fault = have_saved = 0;
    sequence = delay_ticks = 0;
    last_slot = SLOT_COUNT - 1;
    saved.brightness = DIMMER_DEFAULT_PERCENT;
    saved.power_on = 0;
    saved.standby_led_disabled = 0;
    for (slot = 0; slot < SLOT_COUNT; ++slot) {
        for (i = 0; i < RECORD_SIZE; ++i)
            record[i] = hal_eeprom_read((uint8_t)(slot * RECORD_SIZE + i));
        duty = (uint16_t)(record[4] | ((uint16_t)record[5] << 8));
        if (record[0] != COMMIT || (record[1] != VERSION && record[1] != LEGACY_VERSION) ||
            record[6] > (record[1] == LEGACY_VERSION ? 1u : 3u) || record[7] != crc8() ||
            duty < dimmer_duty(DIMMER_MIN_PERCENT) ||
            duty > dimmer_duty(DIMMER_MAX_PERCENT)) continue;
        candidate = (uint16_t)(record[2] | ((uint16_t)record[3] << 8));
        difference = (uint16_t)(candidate - sequence);
        if (!have_saved || (difference != 0 && difference < 0x8000u)) {
            sequence = candidate;
            last_slot = slot;
            have_saved = 1;
            /* Nearest integer percent; exact round trip for our own records. */
            saved.brightness = (uint8_t)((duty * 25u + 128u) / 256u);
            saved.power_on = record[6] & 1u;
            saved.standby_led_disabled = (record[6] >> 1) & 1u;
        }
    }
    requested = saved;
    *restored = saved;
}

void storage_request(const dimmer_state_t *s, uint8_t immediate)
{
    requested = *s;
    delay_ticks = immediate ? 0 : STORAGE_DELAY_TICKS;
}

void storage_tick(void) { if (delay_ticks) --delay_ticks; }
#ifdef FIRMWARE_TEST
uint8_t storage_busy(void)
{
    return phase != IDLE || hal_eeprom_busy() || !equal(&requested, &saved);
}
uint8_t storage_fault(void) { return fault; }
#endif

static void fail(void)
{
    /* Do not advance saved/sequence/slot. A later attempt reuses this slot. */
    phase = IDLE;
    fault = 1;
    delay_ticks = STORAGE_RETRY_TICKS;
}

void storage_service(void)
{
    uint16_t next, duty;
    if (hal_eeprom_busy()) return;
    if (phase == IDLE) {
        if (delay_ticks || equal(&requested, &saved)) return;
        snapshot = requested;
        target_slot = (uint8_t)((last_slot + 1u) & (SLOT_COUNT - 1u));
        next = (uint16_t)(sequence + 1u);
        duty = dimmer_duty(snapshot.brightness);
        record[0] = 0;
        record[1] = VERSION;
        record[2] = (uint8_t)next;
        record[3] = (uint8_t)(next >> 8);
        record[4] = (uint8_t)duty;
        record[5] = (uint8_t)(duty >> 8);
        record[6] = (uint8_t)(snapshot.power_on | (snapshot.standby_led_disabled << 1));
        record[7] = crc8();
        hal_eeprom_start_write(address(0), 0);
        phase = INVALIDATE;
        return;
    }
    if (phase == INVALIDATE) {
        if (hal_eeprom_read(address(0)) != 0) { fail(); return; }
        position = 1;
        phase = PAYLOAD;
    } else if (phase == PAYLOAD) {
        if (hal_eeprom_read(address(position)) != record[position]) {
            fail(); return;
        }
        ++position;
        if (position == RECORD_SIZE) phase = COMMITTING;
    } else if (phase == VERIFY_COMMIT) {
        if (hal_eeprom_read(address(0)) != COMMIT) { fail(); return; }
        saved = snapshot;
        sequence = (uint16_t)(record[2] | ((uint16_t)record[3] << 8));
        last_slot = target_slot;
        have_saved = 1;
        phase = IDLE;
        fault = 0;
        return;
    }
    if (phase == PAYLOAD) {
        hal_eeprom_start_write(address(position), record[position]);
    } else if (phase == COMMITTING) {
        hal_eeprom_start_write(address(0), COMMIT);
        phase = VERIFY_COMMIT;
    }
}
