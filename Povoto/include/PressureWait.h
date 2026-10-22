#pragma once
#include <Arduino.h>
#include <math.h>

// Waits of the calibration routines (venting test, expansion tank volume): one fermenter
// pressure per second, only fresh acquisitions, in a ring of the last
// PRESSURE_WAIT_SAMPLES seconds (320: the longest wait of the venting test, 300 s, is kept whole). The first sample of the wait is kept apart
// (the pressure right after closing a valve).
constexpr uint16_t PRESSURE_WAIT_SAMPLES = 320;

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

  // Standard error of mean(windowMs): sample sd / sqrt(n) (the noise of that reading).
  float meanStandardError(uint32_t windowMs) const {
    double sum = 0.0, squares = 0.0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < count; ++i) {
      if (lastElapsed - newestElapsed(i) > windowMs) break;
      sum += newest(i);
      squares += (double)newest(i) * newest(i);
      ++n;
    }
    if (n < 3) return NAN;
    const double variance = fmax(0.0, (squares - sum * sum / n) / (n - 1));
    return float(sqrt(variance / n));
  }

  // Pressure change over the last windowMs (newest minus the sample windowMs
  // before it); NAN without a sample that old.
  float change(uint32_t windowMs) const {
    for (uint16_t i = 0; i < count; ++i)
      if (lastElapsed - newestElapsed(i) >= windowMs) return newest(0) - newest(i);
    return NAN;
  }

  // Exponential recovery of the wait, P(t) = pInf - amp exp(-t / tau): the pressure rises after closing a
  // valve while the gas warms up (tau 35-65 s in the 130 L fermenter). Fitted to all the samples kept: for a
  // tau, pInf and amp are linear least squares; tau by golden-section search on ln(tau). pInf in the unit of
  // the samples, tau in s. False with fewer than 20 samples, less than 40 s of wait, or a failed fit; tau at the
  // edge of its range (8-400 s) is a failed fit (the time constant is not determined). tests/vent_model.py mirrors it.
  bool fitRecovery(float &pInf, float &tauSeconds, float &amp) const {
    if (count < 20 || lastElapsed < 40000UL) return false;
    const double lo0 = log(8.0), hi0 = log(400.0);
    auto sse = [&](double tau, double &a, double &m) -> double {
      double su = 0, sp = 0, suu = 0, sup = 0;
      for (uint16_t i = 0; i < count; ++i) {
        const double u = exp(-((double)newestElapsed(i) / 1000.0) / tau), y = newest(i);
        su += u; sp += y; suu += u * u; sup += u * y;
      }
      const double det = count * suu - su * su;
      if (!(det > 1e-12)) { a = m = NAN; return (double)INFINITY; }
      m = (count * sup - su * sp) / det;      // = -amp
      a = (sp - m * su) / count;               // = pInf
      double e = 0;
      for (uint16_t i = 0; i < count; ++i) {
        const double r = newest(i) - (a + m * exp(-((double)newestElapsed(i) / 1000.0) / tau));
        e += r * r;
      }
      return e;
    };
    const double golden = (sqrt(5.0) - 1.0) / 2.0;
    double lo = lo0, hi = hi0, x1 = hi - golden * (hi - lo), x2 = lo + golden * (hi - lo), a1, m1, a2, m2;
    double f1 = sse(exp(x1), a1, m1), f2 = sse(exp(x2), a2, m2);
    for (int it = 0; it < 24; ++it) {
      if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - golden * (hi - lo); f1 = sse(exp(x1), a1, m1); }
      else { lo = x1; x1 = x2; f1 = f2; x2 = lo + golden * (hi - lo); f2 = sse(exp(x2), a2, m2); }
    }
    const double lnTau = 0.5 * (lo + hi);
    double a, m;
    if (!isfinite(sse(exp(lnTau), a, m))) return false;
    if (lnTau - lo0 < 0.02 || hi0 - lnTau < 0.02) return false;
    pInf = (float)a;
    amp = (float)(-m);
    tauSeconds = (float)exp(lnTau);
    return isfinite(pInf) && isfinite(amp);
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
