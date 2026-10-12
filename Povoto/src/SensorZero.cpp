#include <Arduino.h>
#include <math.h>
#include "SensorZero.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoTasks.h"
#include "CalibrationEvents.h"

// docs/pressure-sensor-zero.md
static constexpr float SENSOR_ZERO_MIN_MINUTES = 1.0f;
static constexpr float SENSOR_ZERO_MAX_MINUTES = 15.0f;
static constexpr unsigned long SENSOR_ZERO_SAMPLE_MS = 1000UL;
static constexpr unsigned long SENSOR_ZERO_BLOCK_MS = 10000UL;
// Stability limits, as pressure near zero.
static constexpr float SENSOR_ZERO_MAX_SD_MBAR = 1.0f;
static constexpr float SENSOR_ZERO_MAX_BLOCK_RANGE_MBAR = 1.0f;
static constexpr float SENSOR_ZERO_MAX_TREND_MBAR = 0.5f;

static bool active = false;
// Set by the web handler; the loop aborts (the samples are freed in the loop only).
static volatile bool abortRequested = false;
static unsigned long startMillis = 0, durationMs = 0, lastSampleMillis = 0, lastAcquisition = 0;
static bool startedWarm = false;
static float *sampleCurrent = nullptr;
static uint32_t *sampleMillis = nullptr; // since the start
static uint16_t sampleCapacity = 0, sampleCount = 0, rejectedSamples = 0;
static SensorZeroResult result = {};

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
  char line1[48], line2[64], line3[64];
  if (active) {
    snprintf(line1, sizeof(line1), "Sensor zero: RUN %.1f/%.1f min", (now - startMillis) / 60000.0f, durationMs / 60000.0f);
    snprintf(line2, sizeof(line2), "Current %.3f mA (zero %.3f mA)", currentReading, FMTData.pressure0Current);
    snprintf(line3, sizeof(line3), "Keep the fermenter open to the atmosphere");
  } else if (result.available) {
    snprintf(line1, sizeof(line1), "Sensor zero: %s", result.stable ? "END" : "UNSTABLE READING");
    snprintf(line2, sizeof(line2), "Difference %+.3f mA = %+.2f mbar", result.difference,
             result.difference * result.barPerMilliamp * 1000.0f);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  } else {
    snprintf(line1, sizeof(line1), "Sensor zero: ABORT");
    snprintf(line2, sizeof(line2), "%u valid samples", (unsigned)sampleCount);
    snprintf(line3, sizeof(line3), "See the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

bool startSensorZero(float minutes, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive()) blocked = "Process in progress";
  else if (!pressureSensorConnected) blocked = "Pressure sensor (INA226) not connected";
  else if (!isfinite(minutes) || minutes < SENSOR_ZERO_MIN_MINUTES || minutes > SENSOR_ZERO_MAX_MINUTES)
    blocked = "Duration must be from 1 to 15 min";
  else if (!isfinite(barPerMilliamp())) blocked = "Invalid pressure calibration (point B current must be above point A)";
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
  startMillis = millis();
  durationMs = (unsigned long)(minutes * 60000.0f);
  lastSampleMillis = 0;
  lastAcquisition = pressureAcquisitionMillis();
  startedWarm = startMillis >= SENSOR_ZERO_WARMUP_MS;
  sampleCount = rejectedSamples = 0;
  result = {};
  abortRequested = false;
  active = true;
  showProgress(true);
  return true;
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

float sensorZeroProgressMinutes() {
  return active ? (millis() - startMillis) / 60000.0f : 0.0f;
}

SensorZeroResult getSensorZeroResult() {
  return result;
}

static void finishSensorZero() {
  active = false;
  result = {};
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
    result.storedZero = FMTData.pressure0Current;
    result.difference = result.meanCurrent - result.storedZero;
    result.barPerMilliamp = barPerMilliamp();
    const float mbar = result.barPerMilliamp * 1000.0f;
    result.stable = rejectedSamples <= sampleCount / 20 &&
        result.sdCurrent * mbar <= SENSOR_ZERO_MAX_SD_MBAR &&
        result.blockAmplitude * mbar <= SENSOR_ZERO_MAX_BLOCK_RANGE_MBAR &&
        fabsf(result.trend) * mbar <= SENSOR_ZERO_MAX_TREND_MBAR;
    result.warmedUp = startedWarm;
    result.available = true;
    result.canSave = result.stable && result.warmedUp;
    Serial.printf("[SENSOR ZERO] mean %.4f mA (sd %.4f, blocks %.4f, trend %+.4f), zero %.4f, difference %+.4f mA "
                  "= %+.2f mbar, %s%s\n", result.meanCurrent, result.sdCurrent, result.blockAmplitude, result.trend,
                  result.storedZero, result.difference, result.difference * mbar,
                  result.stable ? "stable" : "UNSTABLE", result.warmedUp ? "" : ", board on < 30 min");
  }
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
    Serial.println("[SENSOR ZERO] aborted: mode, task or sensor changed");
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
    } else {
      ++rejectedSamples;
    }
  }
  if (now - startMillis >= durationMs) finishSensorZero();
  else showProgress(false);
}

bool saveSensorZero(char *reason, size_t reasonSize) {
  if (active || !result.canSave || result.saved) {
    snprintf(reason, reasonSize, "%s", result.saved ? "This zero was already saved" : "No stable zero measurement to save");
    return false;
  }
  if (SetPointData.mode != MODE_OFF) {
    snprintf(reason, reasonSize, "Mode must be OFF");
    return false;
  }
  // An offset of the transmitter or of the shunt is additive: every
  // calibration current moves by the same amount and the gain is kept.
  const float shift = result.meanCurrent - FMTData.pressure0Current;
  FMTData.pressure0Current += shift;
  FMTData.pressure1Current += shift;
  if (FMTData.pressure2Bar != 0.0f && FMTData.pressure2Current != 0.0f) FMTData.pressure2Current += shift;
  if (!writeFMTDataToNIV()) {
    snprintf(reason, reasonSize, "Could not save to NVS");
    return false;
  }
  result.saved = true;
  result.canSave = false;
  char details[160];
  snprintf(details, sizeof(details), "shift %+.4f mA (%+.2f mbar); sd %.2f mbar; trend %+.2f mbar; %.1f min",
           shift, shift * result.barPerMilliamp * 1000.0f, result.sdCurrent * result.barPerMilliamp * 1000.0f,
           result.trend * result.barPerMilliamp * 1000.0f, result.minutes);
  calibrationEventAppend("sensor-zero", details);
  return true;
}
