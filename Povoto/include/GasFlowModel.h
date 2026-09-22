#pragma once
#include <math.h>

// Times are real seconds; volumes are litres, pressures bar, temperatures K.
namespace GasFlow {
inline double expansionTime(double pressure, double targetResidual, double a, double b) {
  const double rate = a - b * pressure;
  if (!isfinite(pressure) || !(targetResidual > 0.0 && targetResidual < 1.0) ||
      !isfinite(rate) || rate <= 0.0) return INFINITY;
  return -log(targetResidual) / rate;
}
inline bool validExpansionParameters(double a, double b, double maximumPressure) {
  return isfinite(a) && isfinite(b) && isfinite(maximumPressure) &&
      a - b * fmax(0.0, maximumPressure + 0.2) > 0.0;
}
inline bool validVentingResidualFactor(double factor) {
  return isfinite(factor) && factor > 0.0 && factor < 1.0;
}
inline bool validVentingResidualCoefficients(double a, double b, double c) {
  return isfinite(a) && isfinite(b) && isfinite(c);
}
// F(P) = c*P^2 + d*P + e is the 20-second residual fraction at gauge pressure
// P in bar. The curve is deliberately extrapolated outside tested pressures.
inline double ventingResidualFactorAtPressure(double gaugePressure,
                                               double a, double b, double c) {
  if (!validVentingResidualCoefficients(a, b, c) || !isfinite(gaugePressure))
    return NAN;
  return a * gaugePressure * gaugePressure + b * gaugePressure + c;
}
// Validate the requested operating envelope at both pressure extremes and at
// the parabola vertex. The curve is valid only when a 20-second residual is a
// physically meaningful fraction at every requested check point.
inline bool validVentingResidualCurve(double a, double b, double c,
                                      double maximumPressure) {
  if (!validVentingResidualCoefficients(a, b, c) || !isfinite(maximumPressure)) return false;
  const double upperPressure = maximumPressure + 0.2;
  if (upperPressure < 0) return false;
  const double atZero = ventingResidualFactorAtPressure(0.0, a, b, c);
  const double atUpper = ventingResidualFactorAtPressure(upperPressure, a, b, c);
  if (!validVentingResidualFactor(atZero) || !validVentingResidualFactor(atUpper)) return false;
  if (fabs(a) <= 1e-12) return true;
  const double vertexPressure = -b / (2.0 * a);
  const double atVertex = ventingResidualFactorAtPressure(vertexPressure, a, b, c);
  return validVentingResidualFactor(atVertex);
}
inline double residual(double seconds, double a, double b) {
  if (seconds <= 0) return 1;
  return fmin(1.0, fmax(0.0, a * pow(seconds, -b)));
}
inline double ventingResidual(double seconds, double fermenterVolume, double expansionVolume,
                              double factor) {
  if (!validVentingResidualFactor(factor) || fermenterVolume <= 0 || expansionVolume <= 0)
    return 1.0;
  return pow(factor, (seconds * fermenterVolume) / (20.0 * expansionVolume));
}
inline double ventingSecondsForResidual(double target, double fermenterVolume,
                                        double expansionVolume, double factor) {
  if (!(target > 0 && target < 1) || !validVentingResidualFactor(factor) ||
      fermenterVolume <= 0 || expansionVolume <= 0) return INFINITY;
  return 20.0 * expansionVolume * log(target) / (fermenterVolume * log(factor));
}
inline double expansionSeconds(double available, double expansionOptimal, double ventingOptimal) {
  return fmax(0.0, fmin(expansionOptimal, available * expansionOptimal /
                                         (expansionOptimal + ventingOptimal)));
}
inline double transferredMoles(double pressure, double headspace, double expansion,
                               double temperature, double initialMoles, double residualFraction) {
  if (headspace <= 0 || expansion <= 0 || temperature <= 0) return 0;
  constexpr double gasConstant = 0.083144626;
  const double initialPressure = initialMoles * gasConstant * temperature / expansion;
  return fmax(0.0, (pressure - initialPressure) * headspace * expansion /
      ((headspace + expansion) * gasConstant * temperature)) * (1.0 - residualFraction);
}
struct ExpansionPressureProjection {
  double fermenter;
  double expansion;
  double difference;
};
// Infer headspace from an independently measured, temperature-corrected drop.
// Do not use projected pressures: those already depend on the old headspace.
inline double headspaceFromMeasuredDrop(double fermenterBefore, double fermenterAfter,
    double expansionBefore, double expansionVolume, double residualFraction) {
  if (!isfinite(fermenterBefore) || !isfinite(fermenterAfter) ||
      !isfinite(expansionBefore) || !isfinite(expansionVolume) ||
      !isfinite(residualFraction) || expansionVolume <= 0 ||
      residualFraction < 0 || residualFraction >= 1) return NAN;
  const double drop = fermenterBefore - fermenterAfter;
  const double initialDifference = fermenterBefore - expansionBefore;
  if (drop <= 0 || initialDifference <= 0) return NAN;
  const double completeDrop = drop / (1.0 - residualFraction);
  const double volume = expansionVolume * (initialDifference / completeDrop - 1.0);
  return isfinite(volume) && volume > 0 ? volume : NAN;
}
// Gauge pressures, using the same isothermal mole balance as transferredMoles.
// The initial expansion-tank moles represent only pressure above atmosphere.
inline ExpansionPressureProjection projectExpansionPressures(double initialFermenterPressure,
    double headspace, double expansionVolume, double temperature,
    double initialExpansionMoles, double transferred) {
  if (!(headspace > 0 && expansionVolume > 0 && temperature > 0))
    return {NAN, NAN, NAN};
  const double rt = 0.083144626 * temperature;
  const double pf = initialFermenterPressure - transferred * rt / headspace;
  const double pe = (initialExpansionMoles + transferred) * rt / expansionVolume;
  return {pf, pe, pf - pe};
}
}
