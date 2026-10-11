#ifndef SENSOR_ZERO_H
#define SENSOR_ZERO_H

#include <Arduino.h>

// Zero of the pressure sensor (docs/pressure-sensor-zero.md). The fermenter is
// opened to the atmosphere by the operator (the routine opens no valve); the
// filtered loop current is averaged for some minutes and compared with the
// stored zero, without the clipping of convertCurrentToPressure().
struct SensorZeroResult {
  bool available;        // a measurement finished
  bool stable;           // within the stability limits
  bool warmedUp;         // started with the board on for >= 30 min
  bool canSave;          // available && stable && warmedUp && not saved yet
  bool saved;
  uint16_t samples;
  float minutes;         // duration of the measurement
  float meanCurrent;     // mA
  float sdCurrent;       // mA
  float blockAmplitude;  // mA, range of the 10-s block means
  float trend;           // mA over the measurement (least-squares slope x duration)
  float storedZero;      // FMTData.pressure0Current at the end
  float difference;      // mean - stored zero, mA
  float barPerMilliamp;  // calibration slope near zero
};

constexpr float SENSOR_ZERO_DEFAULT_MINUTES = 5.0f;
constexpr unsigned long SENSOR_ZERO_WARMUP_MS = 30UL * 60UL * 1000UL;

bool startSensorZero(float minutes, char *reason, size_t reasonSize);
void abortSensorZero();
bool sensorZeroActive();
void processSensorZero();           // main loop, from pressureControl()
float sensorZeroProgressMinutes();  // elapsed while running
SensorZeroResult getSensorZeroResult();
// Shifts the three calibration currents by the measured difference (gain kept).
bool saveSensorZero(char *reason, size_t reasonSize);

#endif
