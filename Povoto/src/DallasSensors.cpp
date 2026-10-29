#include <Arduino.h>
#include <math.h>
#include <IOTK_Dallas.h>
#include <IOTK_NTP.h>
#include "datalog.h"
#include "GambainoCommon.h"
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "DallasSensors.h"

// IOTK_Dallas keeps a pointer to each slot's address and reads it every
// second, so the role addresses live in fixed buffers whose content changes
// here, always in the main loop (the same task as processDallas()). The
// fermenter slot is always registered; the others the first time their role
// has an address: until then the bus has
// no extra traffic. An unassigned slot keeps all zeros and reads
// DEVICE_DISCONNECTED_C.

static constexpr unsigned long DALLAS_HOLD_MS = 60000UL; // same tolerance as the fermenter

static uint8_t slotAddress[DALLAS_ROLE_COUNT][8];
static float slotReading[DALLAS_ROLE_COUNT] = {NOTaTEMP, NOTaTEMP, NOTaTEMP}; // fermenter: dallasTemperature
static bool slotRegistered[DALLAS_ROLE_COUNT];

static float heldReading[DALLAS_ROLE_COUNT] = {NOTaTEMP, NOTaTEMP, NOTaTEMP};
static unsigned long heldMillis[DALLAS_ROLE_COUNT];
static bool heldValid[DALLAS_ROLE_COUNT];

static DallasScanEntry scanEntries[DALLAS_SCAN_MAX];
static uint8_t scanCount = 0;
static portMUX_TYPE dallasMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool scanRequested = false;
static volatile bool rolesChanged = false;

static bool validReading(float t) {
  return isfinite(t) && t != DEVICE_DISCONNECTED_C && t != 85.0f && t > -55.0f && t < 125.0f;
}

static bool isThermometerFamily(uint8_t family) {
  return family == 0x10 || family == 0x22 || family == 0x28 || family == 0x3B || family == 0x42;
}

bool dallasAddressEmpty(const uint8_t *address) {
  for (uint8_t i = 0; i < 8; ++i)
    if (address[i] != 0) return false;
  return true;
}

void formatDallasAddress(const uint8_t *address, char *out, size_t outSize) {
  if (!out || outSize == 0) return;
  if (dallasAddressEmpty(address)) {
    out[0] = '\0';
    return;
  }
  snprintf(out, outSize, "%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
           address[0], address[1], address[2], address[3],
           address[4], address[5], address[6], address[7]);
}

// Accepts the formatted text and, for older backups, 16 hex digits without '-'.
bool parseDallasAddress(const char *text, uint8_t *address) {
  memset(address, 0, 8);
  if (!text || text[0] == '\0') return true;
  const size_t length = strlen(text);
  const bool dashed = length == 23;
  if (!dashed && length != 16) return false;
  const uint8_t step = dashed ? 3 : 2;
  for (uint8_t i = 0; i < 8; ++i) {
    const char *p = text + step * i;
    if (dashed && i < 7 && p[2] != '-') return false;
    char pair[3] = {p[0], p[1], '\0'};
    if (!isxdigit((unsigned char)pair[0]) || !isxdigit((unsigned char)pair[1])) return false;
    address[i] = (uint8_t)strtoul(pair, nullptr, 16);
  }
  return OneWire::crc8(address, 7) == address[7] && isThermometerFamily(address[0]);
}

bool dallasRolesDistinct(const uint8_t (*addresses)[8]) {
  for (uint8_t a = 0; a < DALLAS_ROLE_COUNT; ++a)
    for (uint8_t b = a + 1; b < DALLAS_ROLE_COUNT; ++b)
      if (!dallasAddressEmpty(addresses[a]) && memcmp(addresses[a], addresses[b], 8) == 0) return false;
  return true;
}

static void scanBus() {
  DallasScanEntry found[DALLAS_SCAN_MAX];
  uint8_t count = 0;
  uint8_t address[8];
  oneWire.reset_search();
  while (count < DALLAS_SCAN_MAX && oneWire.search(address)) {
    if (OneWire::crc8(address, 7) != address[7] || !isThermometerFamily(address[0])) continue;
    memcpy(found[count].address, address, 8);
    // Last conversion requested by processDallas() (85 = not converted yet).
    const float t = Dallas.getTempC(address);
    found[count].temperature = validReading(t) ? t : NOTaTEMP;
    ++count;
  }
  portENTER_CRITICAL(&dallasMux);
  memcpy(scanEntries, found, sizeof(found[0]) * count);
  scanCount = count;
  portEXIT_CRITICAL(&dallasMux);
  Serial.printf("[DALLAS] %u thermometer(s) on the bus\n", count);
}

bool dallasOnBus(const uint8_t *address) {
  bool found = false;
  portENTER_CRITICAL(&dallasMux);
  for (uint8_t i = 0; i < scanCount && !found; ++i)
    found = memcmp(scanEntries[i].address, address, 8) == 0;
  portEXIT_CRITICAL(&dallasMux);
  return found;
}

// No automatic choice: a failed sensor must never be replaced by another one
// (the environment's would freeze the beer). Each slot reads only the address
// assigned on the Settings page.
static void loadSlots() {
  for (uint8_t role = 0; role < DALLAS_ROLE_COUNT; ++role) {
    const uint8_t *address = FMTData.dallasAddress[role];
    if (memcmp(address, slotAddress[role], 8) == 0) continue;

    portENTER_CRITICAL(&dallasMux);
    memcpy(slotAddress[role], address, 8);
    portEXIT_CRITICAL(&dallasMux);
    char text[DALLAS_ADDRESS_TEXT_SIZE];
    formatDallasAddress(address, text, sizeof(text));
    Serial.printf("[DALLAS] role %u -> %s\n", role, text[0] ? text : "(none)");
    // Another sensor: nothing of the previous one is kept.
    if (role == DALLAS_ROLE_FERMENTER) {
      dallasTemperature = NOTaTEMP;
    } else {
      slotReading[role] = NOTaTEMP;
      heldValid[role] = false;
      heldReading[role] = NOTaTEMP;
    }
    if (!slotRegistered[role] && !dallasAddressEmpty(address)) {
      DallasThermometer(role == DALLAS_ROLE_FERMENTER ? &dallasTemperature : &slotReading[role],
                        slotAddress[role]);
      slotRegistered[role] = true;
    }
  }
}

void dallasSensorsSetup() {
  scanBus();
  loadSlots();
  // Always read: without a sensor it shows DEVICE_DISCONNECTED_C, as before.
  if (!slotRegistered[DALLAS_ROLE_FERMENTER]) {
    DallasThermometer(&dallasTemperature, slotAddress[DALLAS_ROLE_FERMENTER]);
    slotRegistered[DALLAS_ROLE_FERMENTER] = true;
  }
}

void requestDallasScan() {
  scanRequested = true;
}

void applyDallasRoles() {
  rolesChanged = true;
}

// Fermenter thermometer faults (85 = power-on value, -127 = no answer). The
// control and the alarm already tolerate them (TermperatureControl.cpp); this
// counts them, records what the sensor and the bus looked like and, after a few
// seconds, retries the sensor without waiting for the alarm (docs/thermometers.md).
static constexpr unsigned long FAULT_RECOVER_AFTER_MS = 4000UL;
static constexpr unsigned long FAULT_RECOVER_EVERY_MS = 5000UL;
static constexpr unsigned long FAULT_LOG_MIN_MS = 2000UL;

static struct {
  bool seenValid;      // ignores the boot, before the first reading
  bool active;
  unsigned long since; // millis
  bool attempted;      // lastAttempt is set (a flag: "now | 1" wrapped the difference)
  unsigned long lastAttempt;
  uint16_t attempts;  // in the current fault
  uint32_t events;
  uint32_t startEpoch; // of the last fault (NTP, 0 = unknown)
  unsigned long lastMs;
  unsigned long longestMs;
  uint16_t lastAttempts;
  bool hasDiagnosis;
  bool present;
  bool crcOk;
  bool parasite;       // VDD low at the sensor: it runs on the data line
  uint8_t scratchpad[9];
} fault;

static void diagnoseFermenterThermometer(unsigned long now) {
  const uint8_t *address = slotAddress[DALLAS_ROLE_FERMENTER];
  fault.attempted = true;
  fault.lastAttempt = now;
  ++fault.attempts;
  memset(fault.scratchpad, 0, sizeof(fault.scratchpad));
  fault.present = oneWire.reset();
  fault.crcOk = fault.present && Dallas.readScratchPad(address, fault.scratchpad) &&
                OneWire::crc8(fault.scratchpad, 8) == fault.scratchpad[8];
  fault.parasite = fault.present && Dallas.readPowerSupply(address);
  fault.hasDiagnosis = true;
  // Rewrites the configuration register if the sensor lost it (12 bits).
  if (fault.present) Dallas.setResolution(address, 12);
  Serial.printf("[DALLAS] fermenter thermometer invalid for %lu s (attempt %u): presence=%d "
                "scratchpad=%02X %02X %02X %02X %02X %02X %02X %02X %02X crc=%s vdd=%s\n",
                (now - fault.since) / 1000UL, fault.attempts, fault.present,
                fault.scratchpad[0], fault.scratchpad[1], fault.scratchpad[2], fault.scratchpad[3],
                fault.scratchpad[4], fault.scratchpad[5], fault.scratchpad[6], fault.scratchpad[7],
                fault.scratchpad[8], fault.crcOk ? "ok" : "BAD", fault.parasite ? "LOW" : "ok");
}

static void fermenterFaultProcess(unsigned long now) {
  if (dallasAddressEmpty(slotAddress[DALLAS_ROLE_FERMENTER])) {
    fault.active = false;
    fault.seenValid = false;
    return;
  }
  if (validReading(dallasTemperature)) {
    fault.seenValid = true;
    if (!fault.active) return;
    fault.active = false;
    fault.lastMs = now - fault.since;
    fault.lastAttempts = fault.attempts;
    if (fault.lastMs > fault.longestMs) fault.longestMs = fault.lastMs;
    if (fault.lastMs >= FAULT_LOG_MIN_MS)
      Serial.printf("[DALLAS] fermenter thermometer back after %lu ms (%u attempt(s))\n",
                    fault.lastMs, fault.attempts);
    return;
  }
  if (!fault.seenValid) return;
  if (!fault.active) {
    fault.active = true;
    fault.since = now;
    fault.attempts = 0;
    fault.attempted = false;
    ++fault.events;
    fault.startEpoch = NTPEpoch();
  }
  if (now - fault.since >= FAULT_RECOVER_AFTER_MS &&
      (!fault.attempted || now - fault.lastAttempt >= FAULT_RECOVER_EVERY_MS))
    diagnoseFermenterThermometer(now);
}

size_t dallasFaultStatus(char *out, size_t outSize) {
  char when[20] = "";
  if (fault.startEpoch) formatLocalEpochISO(fault.startEpoch, when, sizeof(when));
  int n = snprintf(out, outSize,
                   "Dallas fermenter faults: %lu since boot%s; last %s%s (%.1f s, %u attempt(s)); longest %.1f s; now: %s<br>",
                   (unsigned long)fault.events, fault.events ? "" : " (none)",
                   fault.events ? "at " : "-", when,
                   (fault.active ? millis() - fault.since : fault.lastMs) / 1000.0f,
                   fault.active ? fault.attempts : fault.lastAttempts,
                   fault.longestMs / 1000.0f, fault.active ? "FAULT" : "ok");
  if (n > 0 && fault.hasDiagnosis && (size_t)n < outSize) {
    const uint8_t *s = fault.scratchpad;
    n += snprintf(out + n, outSize - n,
                  "&nbsp;&nbsp;&nbsp;&nbsp;last diagnosis: presence=%s scratchpad=%02X %02X %02X %02X %02X %02X %02X %02X %02X crc=%s vdd=%s<br>",
                  fault.present ? "yes" : "NO", s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8],
                  fault.crcOk ? "ok" : "BAD", fault.parasite ? "LOW" : "ok");
  }
  return n > 0 ? (size_t)n : 0;
}

void dallasSensorsProcess() {
  if (scanRequested) {
    scanRequested = false;
    rolesChanged = false;
    scanBus();
    loadSlots();
  }
  if (rolesChanged) {
    rolesChanged = false;
    loadSlots();
  }
  const unsigned long now = millis();
  fermenterFaultProcess(now);
  for (uint8_t role = DALLAS_ROLE_ENVIRONMENT; role < DALLAS_ROLE_COUNT; ++role) {
    if (validReading(slotReading[role])) {
      heldReading[role] = slotReading[role];
      heldMillis[role] = now;
      heldValid[role] = true;
    } else if (heldValid[role] && now - heldMillis[role] >= DALLAS_HOLD_MS) {
      heldValid[role] = false;
      heldReading[role] = NOTaTEMP;
    }
  }
}

uint8_t getDallasScan(DallasScanEntry *out, uint8_t max) {
  portENTER_CRITICAL(&dallasMux);
  const uint8_t count = scanCount < max ? scanCount : max;
  memcpy(out, scanEntries, sizeof(scanEntries[0]) * count);
  portEXIT_CRITICAL(&dallasMux);
  return count;
}

bool dallasRoleAssigned(uint8_t role) {
  return role < DALLAS_ROLE_COUNT && !dallasAddressEmpty(FMTData.dallasAddress[role]);
}

float dallasRoleTemperature(uint8_t role) {
  if (role == DALLAS_ROLE_FERMENTER) return dallasTemperature;
  if (role >= DALLAS_ROLE_COUNT || !heldValid[role]) return NOTaTEMP;
  return heldReading[role];
}
