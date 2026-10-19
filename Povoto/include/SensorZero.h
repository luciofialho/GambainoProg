#ifndef SENSOR_ZERO_H
#define SENSOR_ZERO_H

#include <Arduino.h>

// Zero and point B of the pressure sensor (docs/pressure-sensor-zero.md). The
// operator sets the pressure (the routines open no valve); the filtered loop
// current is averaged and compared with the stored point, without the clipping
// of convertCurrentToPressure(). Zero: fermenter open to the atmosphere, 5 min.
// Point B: steady reference pressure read on the manometer, 2 min.
struct SensorZeroResult {
  bool available;        // a measurement finished
  bool stable;           // within the stability limits
  bool warmedUp;         // started with the board on for >= 30 min
  bool canSave;          // available && stable && warmedUp && not saved yet (point B: and a valid reference)
  bool saved;
  uint16_t samples;
  float minutes;         // duration of the measurement
  float meanCurrent;     // mA
  float sdCurrent;       // mA
  float blockAmplitude;  // mA, range of the 10-s block means
  float trend;           // mA over the measurement (least-squares slope x duration)
  float storedCurrent;   // FMTData.pressure0Current (zero) or pressure1Current (point B) at the end
  float difference;      // mean - stored current, mA
  float barPerMilliamp;  // slope of the calibration line
  float referenceBar;    // point B: pressure on the reference manometer
  float readingBar;      // point B: pressure of the stored line at the mean current, no clipping
};

constexpr float SENSOR_ZERO_DEFAULT_MINUTES = 5.0f;
constexpr float SENSOR_POINT_B_MINUTES = 2.0f;
constexpr unsigned long SENSOR_ZERO_WARMUP_MS = 30UL * 60UL * 1000UL;

bool startSensorZero(char *reason, size_t reasonSize);  // always SENSOR_ZERO_DEFAULT_MINUTES
// Point B: referenceBar is read on the manometer, above 0.5 bar and below the security pressure.
bool startSensorPointB(float referenceBar, char *reason, size_t reasonSize);  // always SENSOR_POINT_B_MINUTES
void abortSensorZero();             // aborts whichever routine runs
bool sensorZeroActive();            // zero or point B running
bool sensorPointBActive();          // point B running
void processSensorZero();           // main loop, from pressureControl()
float sensorZeroProgressMinutes();  // elapsed while running
SensorZeroResult getSensorZeroResult();
SensorZeroResult getSensorPointBResult();
// Shifts the two calibration currents by the measured difference (gain kept).
bool saveSensorZero(char *reason, size_t reasonSize);
// Point B := the reference pressure and the mean current.
bool saveSensorPointB(char *reason, size_t reasonSize);

#endif
