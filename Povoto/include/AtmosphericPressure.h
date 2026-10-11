#ifndef ATMOSPHERIC_PRESSURE_H
#define ATMOSPHERIC_PRESSURE_H

#include <Arduino.h>

// Atmospheric pressure (global Patm, bar) from the first valid source
// (docs/atmospheric-pressure.md):
//   1. BMP581 on the Povoto's I2C bus ("sensor"): invalid after 60 s without a reading;
//   2. ESP-NOW broadcast of the SideKick ("sidekick", packet 'A'): invalid after 10 min;
//   3. the configured altitude ("altitude").
// Each measured source is a moving average of the last minute and must be in
// 700-1100 hPa. A change of source only affects later calculations.
constexpr uint8_t PATM_SOURCE_SENSOR = 0;
constexpr uint8_t PATM_SOURCE_SIDEKICK = 1;
constexpr uint8_t PATM_SOURCE_ALTITUDE = 2;

void atmosphericPressureBegin();   // setup(), after Wire.begin()
void atmosphericPressureProcess(); // main loop
// ESP-NOW callback: payload "hPa".
void setAtmosphericPressureFromPacket(const char *payload);
uint8_t atmosphericPressureSource();
const char *atmosphericPressureSourceLabel(uint8_t source);
const char *atmosphericPressureSourceLabel(); // of the source in use
// Status lines (HTML) for /getstatus.
void appendAtmosphericPressureStatus(char *st, size_t size);

#endif
