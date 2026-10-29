#ifndef ZERO_DRIFT_H
#define ZERO_DRIFT_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Thermal drift of the pressure sensor zero (docs/zero-drift.md): with the fermenter open to the atmosphere
// the real gauge pressure is zero, so any change of the reading is the sensor. The test logs the INA226 current
// and the temperatures once a minute, in the PSRAM, until it is stopped or the memory is full (7 days), and
// fits the reading against the ambient temperature (least squares). The fermenter gas temperature is only in the CSV.

struct ZeroDriftFit {
  bool valid;         // enough samples and temperature span
  uint16_t samples;
  float tMin, tMax;   // C, range of the temperature in the fit
  float slope;        // mbar/C: change of the reading per degree
  float slopeError;   // mbar/C, standard error of the slope
  float zeroTemp;     // C at which the fitted reading is zero (the zero calibration temperature)
  float r2;
  float residual;     // mbar, standard deviation of the residuals
  float spread;       // mbar, change of the fitted reading over the range of the temperature
};

struct ZeroDriftResult {
  bool running;
  bool available;               // at least one sample
  uint16_t samples;
  uint16_t capacity;
  uint32_t elapsedSeconds;      // of the last sample
  uint16_t intervalSeconds;
  float ambient;                // C, of the last sample (NAN when missing)
  float current, pressure;      // mA and mbar (not clipped), of the last sample
  float ambientMin, ambientMax; // C, whole run
  ZeroDriftFit byAmbient;
};

bool startZeroDrift(char *reason, size_t reasonSize);
void stopZeroDrift();
bool zeroDriftActive();
void processZeroDrift(); // pressureControl(), main loop
String zeroDriftStatusText();
ZeroDriftResult getZeroDriftResult();
bool zeroDriftHasData();
void handleZeroDriftCSV(AsyncWebServerRequest *request); // /calibration/zerodrift.csv

#endif
