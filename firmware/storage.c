#include "board.h"
#include "hal.h"
#include "storage.h"

/* Boot scans 32 legacy eight-byte half-slots; new records use even half-slots. */
#define SLOT_COUNT 32u
#define RECORD_SIZE 10u
#define COMMIT 0xa5u
#define LEGACY_VERSION 1u
#define VERSION 5u
#define LEGACY_MIN_DUTY ((uint16_t)((DIMMER_MIN_PERCENT * 1023UL + 50u) / 100u))
#define LEGACY_MAX_DUTY ((uint16_t)((DIMMER_MAX_PERCENT * 1023UL + 50u) / 100u))
#define IDLE 0u
#define INVALIDATE 1u
#define PAYLOAD 2u
#define COMMITTING 3u
#define VERIFY_COMMIT 4u

static dimmer_state_t saved, requested, snapshot;
static uint16_t sequence, delay_ticks;
static uint8_t last_address, have_saved, phase, position, target_address;
#ifdef FIRMWARE_TEST
static uint8_t fault;
#endif
static uint8_t record[RECORD_SIZE];

static uint8_t pending_changes(void)
{
    const uint8_t *x = (const uint8_t *)&requested, *y = (const uint8_t *)&saved;
    uint8_t n = sizeof requested;
    do { if (*x++ != *y++) return 1; } while (--n);
    return 0;
}

static uint8_t crc8(uint8_t last)
{
    uint8_t crc = 0, bit;
    const uint8_t *data = record + 1;
    /* CRC-8/SMBUS: polynomial 0x07, init 0, no reflection/xorout; last is 6 or 7. */
    do {
        crc ^= *data++;
        bit = 8;
        do {
            if (crc & 0x80u) { crc <<= 1; crc ^= 0x07u; }
            else crc <<= 1;
        } while (--bit);
    } while (--last);
    return crc;
}

static uint8_t address(uint8_t offset)
{
    return (uint8_t)(target_address + offset);
}

void storage_init(dimmer_state_t *restored)
{
    uint8_t slot, i;
    uint16_t candidate, difference, duty;
    phase = IDLE;
    have_saved = 0;
#ifdef FIRMWARE_TEST
    fault = 0;
#endif
    sequence = delay_ticks = 0;
    last_address = (SLOT_COUNT - 1u) * 8u;
    dimmer_defaults(&saved);
    slot = 0;
    /* Walk EEPROM byte addresses directly; the byte wraps after all 32 half-slots. */
    do {
        for (i = 0; i < 8u; ++i)
            record[i] = hal_eeprom_read((uint8_t)(slot + i));
        if (record[0] != COMMIT || record[1] < LEGACY_VERSION || record[1] > VERSION) continue;
        if (record[1] >= 4u) {
            if (slot & 8u) continue;
            record[8] = hal_eeprom_read((uint8_t)(slot + 8u));
            record[9] = hal_eeprom_read((uint8_t)(slot + 9u));
            i = record[4] & 127u;
            if (record[9] || record[8] != crc8(7) ||
                i > DIMMER_MAX_PERCENT || (record[1] == 4u && i < DIMMER_MIN_PERCENT) ||
                (record[5] & 127u) >= (record[6] & 127u) || (record[6] & 127u) > 100u ||
                (record[1] == VERSION && record[5] > 100u && record[5] != DIMMER_LOWEST) ||
                (uint8_t)(record[7] - 1u) >= BREATHE_MAX_PERIOD) continue;
        } else {
            duty = (uint16_t)(record[4] | ((uint16_t)record[5] << 8));
            if ((record[1] != 3u && record[6] > (record[1] == LEGACY_VERSION ? 1u : 3u)) ||
                record[7] != crc8(6)) continue;
            if ((uint16_t)(duty - LEGACY_MIN_DUTY) > LEGACY_MAX_DUTY - LEGACY_MIN_DUTY ||
                (record[1] == 3u && (record[6] & 124u) >= BREATHE_MAX_PERIOD * 4u)) continue;
        }
        candidate = (uint16_t)(record[2] | ((uint16_t)record[3] << 8));
        difference = (uint16_t)(candidate - sequence);
        if (!have_saved || (difference != 0 && difference < 0x8000u)) {
            sequence = candidate;
            last_address = slot;
            have_saved = 1;
            if (record[1] >= 4u) {
                saved.brightness = record[4] & 127u;
                saved.power_on = record[4] >> 7;
                saved.minimum = record[1] == VERSION ? record[5] : record[5] & 127u;
                saved.maximum = record[6] & 127u;
                saved.breath = (uint8_t)((record[6] & BREATHE_MODE) | record[7]);
            } else {
                saved.brightness = (uint8_t)((duty * 25u + 128u) / 256u);
                saved.power_on = record[6] & 1u;
                saved.breath = BREATHE_DEFAULT_PERIOD;
                if (record[1] == 3u)
                    saved.breath = (uint8_t)((record[6] & BREATHE_MODE) |
                        (((record[6] >> 2) & BREATHE_PERIOD_MASK) + 1u));
                saved.minimum = DIMMER_MIN_PERCENT;
                saved.maximum = DIMMER_MAX_PERCENT;
            }
        }
    } while ((slot += 8u) != 0);
    DIMMER_COPY(&requested, &saved);
    DIMMER_COPY(restored, &saved);
}

void storage_request(const dimmer_state_t *s, uint8_t immediate)
{
    DIMMER_COPY(&requested, s);
    delay_ticks = immediate ? 0 : STORAGE_DELAY_TICKS;
}

void storage_tick(void) { if (delay_ticks) --delay_ticks; }
#ifdef FIRMWARE_TEST
uint8_t storage_busy(void)
{
    return phase != IDLE || hal_eeprom_busy() || pending_changes();
}
uint8_t storage_fault(void) { return fault; }
#endif

static void fail(void)
{
    /* Do not advance saved/sequence/slot. A later attempt reuses this slot. */
    phase = IDLE;
#ifdef FIRMWARE_TEST
    fault = 1;
#endif
    delay_ticks = STORAGE_RETRY_TICKS;
}

void storage_service(void)
{
    uint16_t next;
    if (hal_eeprom_busy()) return;
    if (phase == IDLE) {
        if (delay_ticks || !pending_changes()) return;
        DIMMER_COPY(&snapshot, &requested);
        /* Even half-slots are 16-byte records. Advancing past the last
           half-slot also protects a legacy eight-byte record during migration. */
        target_address = (uint8_t)((last_address + 16u) & 0xf0u);
        next = (uint16_t)(sequence + 1u);
        record[0] = 0;
        record[1] = VERSION;
        record[2] = (uint8_t)next;
        record[3] = (uint8_t)(next >> 8);
        record[4] = (uint8_t)(snapshot.brightness | (snapshot.power_on << 7));
        record[5] = snapshot.minimum; /* v5: 128 is the one-count minimum. */
        record[6] = (uint8_t)(snapshot.maximum | (snapshot.breath & BREATHE_MODE));
        record[7] = snapshot.breath & BREATHE_PERIOD_MASK;
        record[8] = crc8(7);
        /* Kill the overlapping legacy odd-half-slot version byte. Otherwise
           CRC==0xA5 could revive an ancient record after sequence rollover. */
        record[9] = 0;
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
        DIMMER_COPY(&saved, &snapshot);
        sequence = (uint16_t)(record[2] | ((uint16_t)record[3] << 8));
        last_address = target_address;
        phase = IDLE;
#ifdef FIRMWARE_TEST
        fault = 0;
#endif
        return;
    }
    if (phase == PAYLOAD) {
        hal_eeprom_start_write(address(position), record[position]);
    } else if (phase == COMMITTING) {
        hal_eeprom_start_write(address(0), COMMIT);
        phase = VERIFY_COMMIT;
    }
}
