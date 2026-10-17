#ifndef ATMOSPHERIC_PRESSURE_H
#define ATMOSPHERIC_PRESSURE_H

#include <Arduino.h>

// Atmospheric pressure (global Patm, bar) from the first valid source
// (docs/atmospheric-pressure.md):
//   1. ESP-NOW broadcast of the SideKick ("sidekick", packet 'A', its BMP581): invalid
//      after 10 min;
//   2. the configured altitude ("altitude").
// The Povoto has no barometer of its own. The measured source is a moving average of the
// last minute and must be in 700-1100 hPa. A change of source only affects later
// calculations. PATM_SOURCE_SENSOR (0) is retired but keeps its number: the saved
// venting-test records and CSV carry the source as 0 / 1 / 2.
constexpr uint8_t PATM_SOURCE_SENSOR = 0;
constexpr uint8_t PATM_SOURCE_SIDEKICK = 1;
constexpr uint8_t PATM_SOURCE_ALTITUDE = 2;

void atmosphericPressureBegin();   // setup()
void atmosphericPressureProcess(); // main loop
// ESP-NOW callback: payload "hPa".
void setAtmosphericPressureFromPacket(const char *payload);
uint8_t atmosphericPressureSource();
const char *atmosphericPressureSourceLabel(uint8_t source);
const char *atmosphericPressureSourceLabel(); // of the source in use
// Status lines (HTML) for /getstatus.
void appendAtmosphericPressureStatus(char *st, size_t size);

#endif
