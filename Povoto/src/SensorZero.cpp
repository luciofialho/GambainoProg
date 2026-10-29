#include <Arduino.h>
#include <math.h>
#include "SensorZero.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoTasks.h"
#include "CalibrationEvents.h"

// docs/pressure-sensor-zero.md
static constexpr unsigned long SENSOR_ZERO_SAMPLE_MS = 1000UL;
static constexpr unsigned long SENSOR_ZERO_BLOCK_MS = 10000UL;
// Stability limits, as pressure (the same for the zero and for point B).
static constexpr float SENSOR_ZERO_MAX_SD_MBAR = 1.0f;
static constexpr float SENSOR_ZERO_MAX_BLOCK_RANGE_MBAR = 3.0f;
static constexpr float SENSOR_ZERO_MAX_TREND_MBAR = 2.0f;

enum RoutineKind : uint8_t { KIND_ZERO, KIND_POINT_B };

static bool active = false;
static RoutineKind kind = KIND_ZERO;   // the routine running, or the last one
static float referenceBar = NAN;       // point B: the reference manometer
// Set by the web handler; the loop aborts (the samples are freed in the loop only).
static volatile bool abortRequested = false;
static unsigned long startMillis = 0, durationMs = 0, lastSampleMillis = 0, lastAcquisition = 0;
static float *sampleCurrent = nullptr;
static uint32_t *sampleMillis = nullptr; // since the start
static uint16_t sampleCapacity = 0, sampleCount = 0, rejectedSamples = 0;
static double celsiusSum = 0.0;  // ambient temperature of the samples that have it
static uint16_t celsiusCount = 0;
static SensorZeroResult resultZero = {};
static SensorZeroResult resultB = {};

static const char *routineName() { return kind == KIND_POINT_B ? "Point B" : "Sensor zero"; }

static float barPerMilliamp() {
  const float span = FMTData.pressure1Current - FMTData.pressure0Current;
  return span > 0.0f ? FMTData.pressure1Bar / span : NAN;
}

static void releaseSamples() {
  delete[] sampleCurrent;
  delete[] sampleMillis;
  sampleCurrent = nullptr;
  sampleMillis = nullptr;
  sampleCapacity = 0;
}

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  const SensorZeroResult &result = kind == KIND_POINT_B ? resultB : resultZero;
  char line1[48], line2[64], line3[64];
  if (active) {
    snprintf(line1, sizeof(line1), "%s: RUN %.1f/%.1f min", routineName(), (now - startMillis) / 60000.0f, durationMs / 60000.0f);
    if (kind == KIND_POINT_B) {
      snprintf(line2, sizeof(line2), "Current %.3f mA (point B %.3f mA)", currentReading, FMTData.pressure1Current);
      snprintf(line3, sizeof(line3), "Keep %.3f bar steady", referenceBar);
    } else {
      snprintf(line2, sizeof(line2), "Current %.3f mA (zero %.3f mA)", currentReading, FMTData.pressure0Current);
      snprintf(line3, sizeof(line3), "Keep the fermenter open to the atmosphere");
    }
  } else if (result.available) {
    snprintf(line1, sizeof(line1), "%s: %s", routineName(), result.stable ? "END" : "UNSTABLE READING");
    snprintf(line2, sizeof(line2), "Difference %+.3f mA = %+.2f mbar", result.difference,
             result.difference * result.barPerMilliamp * 1000.0f);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  } else {
    snprintf(line1, sizeof(line1), "%s: ABORT", routineName());
    snprintf(line2, sizeof(line2), "%u valid samples", (unsigned)sampleCount);
    snprintf(line3, sizeof(line3), "See the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

static bool startRoutine(RoutineKind routine, float minutes, float reference, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive()) blocked = "Process in progress";
  else if (!pressureSensorConnected) blocked = "Pressure sensor (INA226) not connected";
  else if (!isfinite(barPerMilliamp())) blocked = "Invalid pressure calibration (point B current must be above point A)";
  else if (routine == KIND_POINT_B && !(reference > 0.5f && reference < FMTData.maximumPressure))
    blocked = "The reference pressure must be above 0.5 bar and below the maximum security pressure";
  if (!blocked) {
    releaseSamples();
    sampleCapacity = (uint16_t)(minutes * 60.0f) + 5;
    sampleCurrent = new (std::nothrow) float[sampleCapacity];
    sampleMillis = new (std::nothrow) uint32_t[sampleCapacity];
    if (!sampleCurrent || !sampleMillis) {
      releaseSamples();
      blocked = "Out of memory";
    }
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  kind = routine;
  referenceBar = reference;
  startMillis = millis();
  durationMs = (unsigned long)(minutes * 60000.0f);
  lastSampleMillis = 0;
  lastAcquisition = pressureAcquisitionMillis();
  sampleCount = rejectedSamples = 0;
  celsiusSum = 0.0;
  celsiusCount = 0;
  (routine == KIND_POINT_B ? resultB : resultZero) = {};
  abortRequested = false;
  active = true;
  showProgress(true);
  return true;
}

// Fixed at 5 min: the stability criteria were designed for it.
bool startSensorZero(char *reason, size_t reasonSize) {
  return startRoutine(KIND_ZERO, SENSOR_ZERO_DEFAULT_MINUTES, NAN, reason, reasonSize);
}

// Fixed at 2 min (the operator waits 5 min at the pressure before starting).
bool startSensorPointB(float reference, char *reason, size_t reasonSize) {
  return startRoutine(KIND_POINT_B, SENSOR_POINT_B_MINUTES, reference, reason, reasonSize);
}

// Web task: only requests it; processSensorZero() aborts in the main loop.
void abortSensorZero() {
  if (active) abortRequested = true;
}

static void abortInLoop() {
  abortRequested = false;
  if (!active) return;
  active = false;
  releaseSamples();
  showProgress(true);
}

bool sensorZeroActive() {
  return active;
}

bool sensorPointBActive() {
  return active && kind == KIND_POINT_B;
}

float sensorZeroProgressMinutes() {
  return active ? (millis() - startMillis) / 60000.0f : 0.0f;
}

SensorZeroResult getSensorZeroResult() {
  return resultZero;
}

SensorZeroResult getSensorPointBResult() {
  return resultB;
}

static void finishSensorZero() {
  active = false;
  SensorZeroResult result = {};
  result.samples = sampleCount;
  if (sampleCount >= 10) {
    double sum = 0.0, sumSquares = 0.0, st = 0.0, stt = 0.0, sti = 0.0;
    for (uint16_t i = 0; i < sampleCount; ++i) {
      const double t = sampleMillis[i] / 60000.0, current = sampleCurrent[i];
      sum += current;
      sumSquares += current * current;
      st += t;
      stt += t * t;
      sti += t * current;
    }
    const double n = sampleCount;
    const double mean = sum / n;
    // Range of the 10-s block means: the drift and the slow noise, without
    // the reading noise of single samples.
    float blockMin = INFINITY, blockMax = -INFINITY;
    double blockSum = 0.0;
    uint16_t blockSamples = 0;
    uint32_t block = 0;
    for (uint16_t i = 0; i <= sampleCount; ++i) {
      const bool last = i == sampleCount;
      const uint32_t thisBlock = last ? UINT32_MAX : sampleMillis[i] / SENSOR_ZERO_BLOCK_MS;
      if (blockSamples > 0 && (last || thisBlock != block)) {
        const float blockMean = float(blockSum / blockSamples);
        blockMin = fminf(blockMin, blockMean);
        blockMax = fmaxf(blockMax, blockMean);
        blockSum = 0.0;
        blockSamples = 0;
      }
      if (last) break;
      block = thisBlock;
      blockSum += sampleCurrent[i];
      ++blockSamples;
    }
    const double den = n * stt - st * st;
    const double slopePerMinute = den > 0.0 ? (n * sti - st * sum) / den : 0.0;
    result.minutes = sampleMillis[sampleCount - 1] / 60000.0f;
    result.meanCurrent = float(mean);
    result.sdCurrent = float(sqrt(fmax(0.0, (sumSquares - sum * mean) / (n - 1.0))));
    result.blockAmplitude = blockMax - blockMin;
    result.trend = float(slopePerMinute * result.minutes);
    // Ambient temperature: the mean, when at least half of the samples have it.
    result.celsius = celsiusCount * 2 >= sampleCount ? float(celsiusSum / celsiusCount) : NAN;
    // The stored point moved to that temperature by the drift compensation (as stored without it).
    result.storedCurrent = kind == KIND_POINT_B
        ? FMTData.pressure1Current + pressureThermalShiftMilliamp(FMTData.pressure1Celsius, result.celsius)
        : FMTData.pressure0Current + pressureThermalShiftMilliamp(FMTData.pressure0Celsius, result.celsius);
    result.difference = result.meanCurrent - result.storedCurrent;
    result.barPerMilliamp = barPerMilliamp();
    if (kind == KIND_POINT_B) {
      result.referenceBar = referenceBar;
      result.readingBar = convertCurrentToPressureAt(result.meanCurrent, result.celsius);
    }
    const float mbar = result.barPerMilliamp * 1000.0f;
    result.stable = rejectedSamples <= sampleCount / 20 &&
        result.sdCurrent * mbar <= SENSOR_ZERO_MAX_SD_MBAR &&
        result.blockAmplitude * mbar <= SENSOR_ZERO_MAX_BLOCK_RANGE_MBAR &&
        fabsf(result.trend) * mbar <= SENSOR_ZERO_MAX_TREND_MBAR;
    result.available = true;
    result.canSave = result.stable;
    Serial.printf("[%s] mean %.4f mA (sd %.4f, blocks %.4f, trend %+.4f), stored %.4f, difference %+.4f mA "
                  "= %+.2f mbar, %s\n", kind == KIND_POINT_B ? "POINT B" : "SENSOR ZERO", result.meanCurrent,
                  result.sdCurrent, result.blockAmplitude, result.trend, result.storedCurrent, result.difference,
                  result.difference * mbar, result.stable ? "stable" : "UNSTABLE");
  }
  (kind == KIND_POINT_B ? resultB : resultZero) = result;
  releaseSamples();
  showProgress(true);
}

void processSensorZero() {
  if (!active) return;
  if (abortRequested) {
    abortInLoop();
    return;
  }
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0 || !pressureSensorConnected) {
    Serial.printf("[%s] aborted: mode, task or sensor changed\n", kind == KIND_POINT_B ? "POINT B" : "SENSOR ZERO");
    abortInLoop();
    return;
  }
  const unsigned long now = millis();
  const unsigned long acquisition = pressureAcquisitionMillis();
  if (acquisition != lastAcquisition && (lastSampleMillis == 0 || now - lastSampleMillis >= SENSOR_ZERO_SAMPLE_MS) &&
      sampleCount < sampleCapacity) {
    lastAcquisition = acquisition;
    lastSampleMillis = now;
    // The median filter returns 0 when its central samples spread too much.
    if (currentReading > 0.0f && isfinite(currentReading)) {
      sampleCurrent[sampleCount] = currentReading;
      sampleMillis[sampleCount] = now - startMillis;
      ++sampleCount;
      const float t = pressureCompensationCelsius();
      if (isfinite(t)) {
        celsiusSum += t;
        ++celsiusCount;
      }
    } else {
      ++rejectedSamples;
    }
  }
  if (now - startMillis >= durationMs) finishSensorZero();
  else showProgress(false);
}

bool saveSensorZero(char *reason, size_t reasonSize) {
  SensorZeroResult &result = resultZero;
  if (active || !result.canSave || result.saved) {
    snprintf(reason, reasonSize, "%s", result.saved ? "This zero was already saved" : "No stable zero measurement to save");
    return false;
  }
  if (SetPointData.mode != MODE_OFF) {
    snprintf(reason, reasonSize, "Mode must be OFF");
    return false;
  }
  if (pressureCompensationConfigured() && !isfinite(result.celsius)) {
    snprintf(reason, reasonSize, "No ambient temperature during the test (the drift compensation needs it)");
    return false;
  }
  // An offset of the transmitter or of the shunt is additive: every calibration current moves by
  // the same amount and the gain is kept. The difference is that left after the drift compensation;
  // point A becomes the mean current at the temperature of the test.
  const float shift = result.difference;
  FMTData.pressure0Current = result.meanCurrent;
  FMTData.pressure0Celsius = result.celsius;
  FMTData.pressure1Current += shift;
  if (!writeFMTDataToNIV()) {
    snprintf(reason, reasonSize, "Could not save to NVS");
    return false;
  }
  result.saved = true;
  result.canSave = false;
  char details[160];
  snprintf(details, sizeof(details), "shift %+.4f mA (%+.2f mbar) at %.2f C; sd %.2f mbar; trend %+.2f mbar; %.1f min",
           shift, shift * result.barPerMilliamp * 1000.0f, result.celsius, result.sdCurrent * result.barPerMilliamp * 1000.0f,
           result.trend * result.barPerMilliamp * 1000.0f, result.minutes);
  calibrationEventAppend("sensor-zero", details);
  return true;
}

bool saveSensorPointB(char *reason, size_t reasonSize) {
  SensorZeroResult &result = resultB;
  if (active || !result.canSave || result.saved) {
    snprintf(reason, reasonSize, "%s", result.saved ? "This point B was already saved" : "No stable point B measurement to save");
    return false;
  }
  if (SetPointData.mode != MODE_OFF) {
    snprintf(reason, reasonSize, "Mode must be OFF");
    return false;
  }
  if (!(result.meanCurrent > FMTData.pressure0Current) || !(result.referenceBar > 0.5f) ||
      !(result.referenceBar < FMTData.maximumPressure)) {
    snprintf(reason, reasonSize, "Point B needs a current above point A and a reference above 0.5 bar and below the security pressure");
    return false;
  }
  if (pressureCompensationConfigured() && !isfinite(result.celsius)) {
    snprintf(reason, reasonSize, "No ambient temperature during the test (the drift compensation needs it)");
    return false;
  }
  // The reference manometer defines point B: its pressure, the mean current and the temperature.
  const float previousBar = FMTData.pressure1Bar, previousCurrent = FMTData.pressure1Current;
  const float previousCelsius = FMTData.pressure1Celsius;
  FMTData.pressure1Bar = result.referenceBar;
  FMTData.pressure1Current = result.meanCurrent;
  FMTData.pressure1Celsius = result.celsius;
  if (!writeFMTDataToNIV()) {
    FMTData.pressure1Bar = previousBar;
    FMTData.pressure1Current = previousCurrent;
    FMTData.pressure1Celsius = previousCelsius;
    snprintf(reason, reasonSize, "Could not save to NVS");
    return false;
  }
  result.saved = true;
  result.canSave = false;
  char details[200];
  snprintf(details, sizeof(details),
           "B %.4f mA at %.3f bar, %.2f C (was %.4f mA at %.3f bar; line read %+.2f mbar); sd %.2f mbar; trend %+.2f mbar; %.1f min",
           result.meanCurrent, result.referenceBar, result.celsius, previousCurrent, previousBar,
           (result.readingBar - result.referenceBar) * 1000.0f, result.sdCurrent * result.barPerMilliamp * 1000.0f,
           result.trend * result.barPerMilliamp * 1000.0f, result.minutes);
  calibrationEventAppend("sensor-point-b", details);
  return true;
}
