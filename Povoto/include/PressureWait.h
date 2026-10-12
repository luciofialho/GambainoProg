#pragma once
#include <Arduino.h>
#include <math.h>

// Waits of the calibration routines (venting test, keg volume): one fermenter
// pressure per second, only fresh acquisitions, in a ring of the last
// PRESSURE_WAIT_SAMPLES seconds. The first sample of the wait is kept apart
// (the pressure right after closing a valve).
constexpr uint16_t PRESSURE_WAIT_SAMPLES = 128;

struct PressureWait {
  unsigned long startMillis = 0;
  unsigned long lastAcquisition = 0;
  uint32_t lastElapsed = 0;
  float first = NAN;           // first sample (>= firstDelayMs after the start)
  uint32_t firstElapsed = 0;
  float value[PRESSURE_WAIT_SAMPLES];
  uint32_t elapsed[PRESSURE_WAIT_SAMPLES]; // ms since the start
  uint16_t next = 0, count = 0;

  void reset(unsigned long now) {
    startMillis = now;
    lastAcquisition = 0;
    lastElapsed = 0;
    first = NAN;
    firstElapsed = 0;
    next = count = 0;
  }

  // One sample per second from firstDelayMs on; acquisition is the time of
  // the pressure (0 accepts any: debug simulation).
  void sample(unsigned long now, float pressure, unsigned long acquisition, uint32_t firstDelayMs) {
    if (!isfinite(pressure)) return;
    const uint32_t e = now - startMillis;
    if (e < firstDelayMs) return;
    if (acquisition != 0 && (acquisition == lastAcquisition || (int32_t)(acquisition - startMillis) < 0)) return;
    if (count > 0 && e - lastElapsed < 1000UL) return;
    lastAcquisition = acquisition;
    lastElapsed = e;
    if (!isfinite(first)) {
      first = pressure;
      firstElapsed = e;
    }
    value[next] = pressure;
    elapsed[next] = e;
    next = (next + 1) % PRESSURE_WAIT_SAMPLES;
    if (count < PRESSURE_WAIT_SAMPLES) ++count;
  }

  uint32_t lastSampleElapsed() const { return count ? lastElapsed : 0; }

  // i = 0 is the newest sample.
  float newest(uint16_t i) const { return value[(next + PRESSURE_WAIT_SAMPLES - 1 - i) % PRESSURE_WAIT_SAMPLES]; }
  uint32_t newestElapsed(uint16_t i) const { return elapsed[(next + PRESSURE_WAIT_SAMPLES - 1 - i) % PRESSURE_WAIT_SAMPLES]; }

  // Mean of the samples of the last windowMs.
  float mean(uint32_t windowMs) const {
    if (count == 0) return NAN;
    double sum = 0.0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < count; ++i) {
      if (lastElapsed - newestElapsed(i) > windowMs) break;
      sum += newest(i);
      ++n;
    }
    return n ? float(sum / n) : NAN;
  }

  // Pressure change over the last windowMs (newest minus the sample windowMs
  // before it); NAN without a sample that old.
  float change(uint32_t windowMs) const {
    for (uint16_t i = 0; i < count; ++i)
      if (lastElapsed - newestElapsed(i) >= windowMs) return newest(0) - newest(i);
    return NAN;
  }

  // Least-squares slope (bar/min) of the last windowMs.
  float slopePerMinute(uint32_t windowMs) const {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < count; ++i) {
      if (lastElapsed - newestElapsed(i) > windowMs) break;
      const double x = (double)newestElapsed(i) / 60000.0, y = newest(i);
      sx += x; sy += y; sxx += x * x; sxy += x * y;
      ++n;
    }
    const double den = n * sxx - sx * sx;
    return (n >= 5 && den > 0.0) ? float((n * sxy - sx * sy) / den) : NAN;
  }
};
