#pragma once
#include <math.h>
#include <stdint.h>
#include "VentModel.h"

// Calibration Feedback tab (docs/feedback.md). Pure functions; tests/feedback_bench.py
// is a line-by-line Python mirror: keep both in step.
namespace FeedbackMath {

constexpr double AIR_FRACTION_LIMIT = 0.05;   // the measured exponent counts below it
constexpr double EXPONENT_RMSE_LIMIT = 0.002; // bar: rms residual of the recovery fit
constexpr uint8_t EXPONENT_MIN_COUNT = 30;    // valid vents before "Use" is offered
constexpr uint8_t RING = 32;                  // last values kept (exponent, ratio)
constexpr double LEAK_SLOPE_LIMIT = -0.05;    // mbar/min
constexpr double LEAK_SIGMAS = 3.0;
constexpr uint8_t LEAK_WINDOWS = 3;           // consecutive windows for "possible leak"

// Fraction of the moles kept by one vent, (P/Z) after over (P/Z) before, settled gauge pressures
// in bar. 1 (nothing removed) when unusable: the air estimate never falls without a measurement.
inline double ventKeptFraction(double pOnGauge, double pAfterGauge, double patm, double tKelvin) {
  const double pOn = pOnGauge + patm, pAfter = pAfterGauge + patm;
  if (!(pOn > 0.0) || !(pAfter > 0.0) || !(pAfter < pOn) || !(tKelvin > 0.0)) return 1.0;
  const double kept = (pAfter / VentModel::co2Z(pAfter, tKelvin)) / (pOn / VentModel::co2Z(pOn, tKelvin));
  return (kept > 0.0 && kept < 1.0) ? kept : 1.0;
}

// Air left in the headspace: the batch starts with air only, at startAbs (bar) and startKelvin;
// each vent keeps the fraction `kept` of the mixture and no task lets air in. The headspace volume
// cancels: air moles ~ product * startAbs / startK, all moles ~ nowAbs / (Z nowK).
inline double airFraction(double keptProduct, double startAbs, double startKelvin, double nowAbs, double nowKelvin) {
  if (!(keptProduct > 0.0) || !(startAbs > 0.0) || !(startKelvin > 0.0) || !(nowAbs > 0.0) || !(nowKelvin > 0.0))
    return 1.0;
  const double nowMoles = nowAbs / (VentModel::co2Z(nowAbs, nowKelvin) * nowKelvin);
  return fmin(1.0, keptProduct * (startAbs / startKelvin) / nowMoles);
}

// Median of up to RING values (NAN when empty).
inline float median(const float *values, uint8_t count) {
  if (count == 0) return NAN;
  if (count > RING) count = RING;
  float sorted[RING];
  for (uint8_t i = 0; i < count; ++i) {
    const float v = values[i];
    uint8_t j = i;
    for (; j > 0 && sorted[j - 1] > v; --j) sorted[j] = sorted[j - 1];
    sorted[j] = v;
  }
  return (count & 1) ? sorted[count / 2] : 0.5f * (sorted[count / 2 - 1] + sorted[count / 2]);
}

// Least squares of y = (P + Patm) / T against minutes, accumulated sample by sample. y is taken
// relative to the first sample (the variations are ~1e-6 of y).
struct LeakFit {
  double n, sx, sy, sxx, sxy, syy, st, y0;
  void clear() { n = sx = sy = sxx = sxy = syy = st = y0 = 0.0; }
  void add(double minutes, double pAbs, double tKelvin) {
    const double yAbs = pAbs / tKelvin;
    if (n == 0.0) y0 = yAbs;
    const double y = yAbs - y0;
    n += 1.0; sx += minutes; sy += y; sxx += minutes * minutes; sxy += minutes * y; syy += y * y; st += tKelvin;
  }
  // Slope and its standard error in mbar/min at the mean temperature; false with < 3 samples.
  bool slope(double &mbarPerMinute, double &sigma) const {
    const double den = n * sxx - sx * sx;
    if (n < 3.0 || !(den > 0.0)) return false;
    const double b = (n * sxy - sx * sy) / den;
    const double a = (sy - b * sx) / n;
    const double sse = fmax(0.0, syy - a * sy - b * sxy);
    const double scale = 1000.0 * st / n;
    mbarPerMinute = b * scale;
    sigma = sqrt(sse / (n - 2.0) * n / den) * scale;
    return isfinite(mbarPerMinute) && isfinite(sigma);
  }
};

// A window that looks like gas loss: below the limit and beyond LEAK_SIGMAS standard errors.
inline bool leakLike(double mbarPerMinute, double sigma) {
  return mbarPerMinute < LEAK_SLOPE_LIMIT && mbarPerMinute < -LEAK_SIGMAS * sigma;
}

} // namespace FeedbackMath
