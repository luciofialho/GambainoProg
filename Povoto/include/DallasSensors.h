#ifndef DALLAS_SENSORS_H
#define DALLAS_SENSORS_H

#include <Arduino.h>

// Several DS18B20 on the Dallas bus, each assigned to a role
// (DALLAS_ROLE_* in PovotoData.h; docs/thermometers.md). IOTK_Dallas reads one
// slot per role into its variable; dallasTemperature is the fermenter slot.

constexpr uint8_t DALLAS_SCAN_MAX = 8;

struct DallasScanEntry {
  uint8_t address[8];
  float temperature; // NOTaTEMP when it could not be read
};

void dallasSensorsSetup();   // after DallasSetup(): scans the bus and registers the slots
void dallasSensorsProcess(); // main loop: role changes, requested scans, hold of the readings
void requestDallasScan();    // the scan runs in the main loop
void applyDallasRoles();     // after FMTData.dallasAddress changes; applied in the main loop

// Copy of the last scan (devices on the bus); returns the count.
uint8_t getDallasScan(DallasScanEntry *out, uint8_t max);

bool dallasRoleAssigned(uint8_t role);
// Environment / headspace: last valid reading, held for 60 s, else NOTaTEMP.
float dallasRoleTemperature(uint8_t role);

// Fermenter thermometer faults for /getstatus: count, last and longest, and
// what the last retry saw (scratchpad, presence, VDD). Returns the length written.
size_t dallasFaultStatus(char *out, size_t outSize);

bool dallasOnBus(const uint8_t *address); // found by the last scan

constexpr size_t DALLAS_ADDRESS_TEXT_SIZE = 24; // "XX-XX-XX-XX-XX-XX-XX-XX"
bool dallasAddressEmpty(const uint8_t *address);
void formatDallasAddress(const uint8_t *address, char *out, size_t outSize); // XX-...-XX, "" if empty
bool parseDallasAddress(const char *text, uint8_t *address);                 // "" = empty; false if invalid
bool dallasRolesDistinct(const uint8_t (*addresses)[8]);                     // no sensor in two roles

#endif
