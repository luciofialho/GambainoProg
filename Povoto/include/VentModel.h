#pragma once
#include <math.h>
#include <stdint.h>

// Direct venting model (docs/vent.md). Pure functions; tests/vent_model.py is
// a line-by-line Python mirror used by tests/vent_bench.py and
// tests/venting_fit.py: keep both in step.
// Units: pressures in bar (gauge or absolute as named), temperatures in K,
// volumes in L, times in s, moles in mol.
namespace VentModel {

constexpr double R = 0.0831446;    // bar*L/(mol*K)
constexpr double V_REF = 100.0;    // L: the stored curve is tau for 100 L of air at 20 C
constexpr double T_REF = 293.15;   // K
constexpr double GAMMA_AIR = 1.40;
constexpr double GAMMA_CO2 = 1.29;
constexpr double M_AIR = 28.96;
constexpr double M_CO2 = 44.01;
constexpr int SIMPSON_INTERVALS = 16; // even

// Flow curve of the valve, measured with air on the empty fermenter by the
// venting test: tau(P) = a0 + a1 P + a2 P^2 + a3 P^3 (s) is the time constant
// of the absolute pressure of V_REF litres of air at T_REF, P in gauge bar;
// t0 is the mechanical delay of the valve. The cubic is used only inside the
// pressure range of the test (pMin..pMax): outside it, the end value.
struct Curve {
  float a[4];
  float t0;
  float pMin, pMax;
  uint32_t epoch; // local NTP time of the test it came from (0 = default curve)
} __attribute__((packed));

inline double tauRef(const Curve &c, double pGauge) {
  const double p = fmin(fmax(pGauge, (double)c.pMin), (double)c.pMax);
  return ((c.a[3] * p + c.a[2]) * p + c.a[1]) * p + c.a[0];
}

// C_air(P), mol*K^0.5/(s*bar): molar air flow = C_air * P_abs / sqrt(T).
inline double flowConstantAir(const Curve &c, double pGauge) {
  const double tau = tauRef(c, pGauge);
  return tau > 0.0 ? V_REF / (R * sqrt(T_REF) * tau) : NAN;
}

// Ideal-nozzle flow function for the downstream/upstream pressure ratio r.
inline double nozzlePsi(double gamma, double r) {
  const double critical = pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
  if (r <= critical)
    return sqrt(gamma) * pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
  r = fmin(r, 1.0);
  return sqrt(fmax(0.0, 2.0 * gamma / (gamma - 1.0) * (pow(r, 2.0 / gamma) - pow(r, (gamma + 1.0) / gamma))));
}

// CO2/air molar flow of an ideal nozzle at the same pressures and temperature:
// 0.788 when choked (>= ~0.85 bar gauge), 0.790 at 0.8, 0.796 at 0.5 bar.
inline double co2AirRatio(double pGauge, double patm) {
  const double r = patm / (pGauge + patm);
  const double psiAir = nozzlePsi(GAMMA_AIR, r);
  if (!(psiAir > 0.0)) return sqrt(M_AIR / M_CO2);
  return sqrt(M_AIR / M_CO2) * nozzlePsi(GAMMA_CO2, r) / psiAir;
}

// Second virial coefficient of CO2 (L/mol), Tsonopoulos correlation
// (-0.129 L/mol at 20 C).
inline double co2SecondVirial(double tKelvin) {
  const double tc = 304.13, pc = 73.77, omega = 0.224;
  const double tr = tKelvin / tc;
  const double f0 = 0.1445 - 0.330 / tr - 0.1385 / (tr * tr) - 0.0121 / (tr * tr * tr) - 0.000607 / pow(tr, 8);
  const double f1 = 0.0637 + 0.331 / (tr * tr) - 0.423 / (tr * tr * tr) - 0.008 / pow(tr, 8);
  return (f0 + omega * f1) * R * tc / pc;
}

// Compressibility of CO2, Z = 1 + B P / (R T): 0.992 at 0.5 bar gauge, 0.985 at 1.9 bar.
inline double co2Z(double pAbs, double tKelvin) {
  return 1.0 + co2SecondVirial(tKelvin) * pAbs / (R * tKelvin);
}

inline double logMean(double a, double b) {
  if (!(a > 0.0) || !(b > 0.0)) return NAN;
  if (fabs(a - b) < 1e-9 * a) return 0.5 * (a + b);
  return (a - b) / log(a / b);
}

// sqrt(T0 / T) at the mean pressure of a polytropic expansion from pOn.
inline double coolingFactor(double pOnAbs, double pMeanAbs, double exponent) {
  if (!(exponent >= 1.0) || !(pMeanAbs > 0.0)) return 1.0;
  return pow(pOnAbs / pMeanAbs, (exponent - 1.0) / (2.0 * exponent));
}

// Effective CO2 flow constant: C_air * ratio / sqrt(Z) * level.
inline double co2FlowConstant(const Curve &c, double level, double pGauge, double patm, double tKelvin) {
  return flowConstantAir(c, pGauge) * co2AirRatio(pGauge, patm) / sqrt(co2Z(pGauge + patm, tKelvin)) * level;
}

struct VentCount {
  double moles;      // CO2 out of the fermenter (before the liquid-mass correction)
  double pMeanGauge; // log mean of the absolute pressures at opening and closing
  double cooling;    // sqrt(T0 / T) at the mean pressure
  double flowConstant;
};

// CO2 moles out of one vent: C(P_mean) * P_mean / sqrt(T) * cooling * (t - t0).
// pCloseGauge is the pressure read right after closing (gas still cold).
inline VentCount ventMoles(const Curve &c, double level, double openSeconds, double pOnGauge,
                           double pCloseGauge, double patm, double tGasKelvin, double exponent) {
  VentCount out = {0.0, NAN, NAN, NAN};
  const double pOn = pOnGauge + patm, pClose = pCloseGauge + patm;
  const double pMean = logMean(pOn, pClose);
  if (!(pMean > patm) || !(tGasKelvin > 0.0)) return out;
  out.pMeanGauge = pMean - patm;
  out.cooling = coolingFactor(pOn, pMean, exponent);
  out.flowConstant = co2FlowConstant(c, level, out.pMeanGauge, patm, tGasKelvin);
  const double seconds = fmax(0.0, openSeconds - c.t0);
  out.moles = out.flowConstant * pMean / sqrt(tGasKelvin) * out.cooling * seconds;
  if (!isfinite(out.moles) || out.moles < 0.0) out.moles = 0.0;
  return out;
}

// Opening time for a CO2 headspace to settle from pOn to pTarget (gauge).
inline double ventSeconds(const Curve &c, double level, double headspace, double pOnGauge,
                          double pTargetGauge, double patm, double tGasKelvin, double exponent) {
  const double pOn = pOnGauge + patm, pTarget = pTargetGauge + patm;
  if (!(headspace > 0.0) || !(pTarget < pOn) || !(pTarget > 0.5 * patm) || !(tGasKelvin > 0.0)) return NAN;
  const double moles = headspace / (R * tGasKelvin) *
      (pOn / co2Z(pOn, tGasKelvin) - pTarget / co2Z(pTarget, tGasKelvin));
  const double pMean = logMean(pOn, pTarget);
  const double rate = co2FlowConstant(c, level, pMean - patm, patm, tGasKelvin) * pMean /
      sqrt(tGasKelvin) * coolingFactor(pOn, pMean, exponent);
  return rate > 0.0 ? moles / rate + c.t0 : NAN;
}

// Settled pressure after a polytropic drop (same as adjustedPressureForPostRelief).
inline double isothermalAfter(double pOnGauge, double pReadGauge, double patm, double exponent) {
  const double pOn = pOnGauge + patm, pRead = pReadGauge + patm;
  return pOn * pow(pRead / pOn, 1.0 / exponent) - patm;
}

// Headspace that loses `moles` of CO2 between two settled pressures.
inline double apparentHeadspace(double moles, double pOnGauge, double pAfterIsoGauge, double patm,
                                double tGasKelvin) {
  const double pOn = pOnGauge + patm, pAfter = pAfterIsoGauge + patm;
  const double delta = pOn / co2Z(pOn, tGasKelvin) - pAfter / co2Z(pAfter, tGasKelvin);
  return delta > 0.0 ? moles * R * tGasKelvin / delta : NAN;
}

// ----- Venting test fit -----

// Polytropic exponent of one release, ln(Pclose/P1) / ln(P2/P1) (absolute);
// fallback outside 1.0-1.67.
inline double releaseExponent(double p1Gauge, double pCloseGauge, double p2Gauge, double patm, double fallback) {
  const double num = log((pCloseGauge + patm) / (p1Gauge + patm));
  const double den = log((p2Gauge + patm) / (p1Gauge + patm));
  if (!isfinite(num) || !isfinite(den) || den == 0.0) return fallback;
  const double m = num / den;
  return (m >= 1.0 && m <= 1.67) ? m : fallback;
}

// J_k, k = 0..3, so that (t - t0) = sum a_k J_k for one release from the
// settled P1 to the settled P2 (Simpson over the settled pressure).
inline void releaseIntegrals(double volume, double t0Kelvin, double p1Gauge, double p2Gauge, double patm,
                             double exponent, bool co2, double j[4]) {
  const double p1 = p1Gauge + patm, p2 = p2Gauge + patm;
  const double scale = volume / V_REF * sqrt(T_REF / t0Kelvin);
  const int n = SIMPSON_INTERVALS;
  const double h = (p1 - p2) / n;
  for (int k = 0; k < 4; ++k) j[k] = 0.0;
  for (int i = 0; i <= n; ++i) {
    const double pIso = p2 + i * h;
    const double x = pIso / p1;
    const double pAbs = p1 * pow(x, exponent);
    double g = sqrt(pow(x, exponent - 1.0)) / pAbs;
    if (co2) g /= sqrt(co2Z(pIso, t0Kelvin)) * co2AirRatio(pAbs - patm, patm);
    const double w = (i == 0 || i == n) ? 1.0 : ((i % 2) ? 4.0 : 2.0);
    const double pg = pAbs - patm;
    double power = 1.0;
    for (int k = 0; k < 4; ++k) {
      j[k] += w * g * power;
      power *= pg;
    }
  }
  for (int k = 0; k < 4; ++k) j[k] *= scale * h / 3.0;
}

struct Release {
  float seconds;   // real opening time
  float p1, pClose, p2; // gauge bar; pClose NAN = unknown
  float tempK;     // fermenter at P1
};

// Sensitivities of the release time to the settled pressures (s/bar, both
// positive): dt/dP1 and -dt/dP2 of the model above, for the curve c. They turn
// a pressure reading error into a time error (reduced chi-square of the fit).
inline void releaseSensitivities(const Curve &c, double volume, double t0Kelvin, double p1Gauge, double p2Gauge,
                                 double patm, double exponent, bool co2, double &s1, double &s2) {
  const double p1 = p1Gauge + patm, p2 = p2Gauge + patm;
  const double scale = volume / V_REF * sqrt(T_REF / t0Kelvin);
  const double x = p2 / p1;
  const double pAbs2 = p1 * pow(x, exponent);         // actual pressure when the settled one is P2
  double g2 = sqrt(pow(x, exponent - 1.0)) / pAbs2;
  double g1 = 1.0 / p1;
  if (co2) {
    g2 /= sqrt(co2Z(p2, t0Kelvin)) * co2AirRatio(pAbs2 - patm, patm);
    g1 /= sqrt(co2Z(p1, t0Kelvin)) * co2AirRatio(p1Gauge, patm);
  }
  s1 = scale * g1 * tauRef(c, p1Gauge);
  s2 = scale * g2 * tauRef(c, pAbs2 - patm);
}

// Reduced chi-square of a fit: sum r_i^2 / (s1^2 n1^2 + s2^2 n2^2 + st^2) / (n - 5),
// with n1, n2 the noise of each settled reading (bar, at least floorBar) and st
// the timing noise (s). ~1: the cubic follows the data within the noise.
inline double reducedChiSquare(const Curve &c, const Release *rel, const float *noise1, const float *noise2,
                               const float *residuals, const float *exponents, int count, double volume,
                               double patm, bool co2, double floorBar, double timingSeconds) {
  if (count <= 5) return NAN;
  double sum = 0.0;
  for (int i = 0; i < count; ++i) {
    double s1, s2;
    releaseSensitivities(c, volume, rel[i].tempK, rel[i].p1, rel[i].p2, patm, exponents[i], co2, s1, s2);
    const double n1 = fmax(isfinite(noise1[i]) ? (double)noise1[i] : 0.0, floorBar);
    const double n2 = fmax(isfinite(noise2[i]) ? (double)noise2[i] : 0.0, floorBar);
    const double variance = s1 * s1 * n1 * n1 + s2 * s2 * n2 * n2 + timingSeconds * timingSeconds;
    sum += (double)residuals[i] * residuals[i] / variance;
  }
  return sum / (count - 5);
}

// Gaussian elimination with partial pivoting of an n x n system (n <= 5).
inline bool solveLinear(double m[5][6], int n, double x[5]) {
  for (int col = 0; col < n; ++col) {
    int pivot = col;
    for (int r = col + 1; r < n; ++r) if (fabs(m[r][col]) > fabs(m[pivot][col])) pivot = r;
    if (!(fabs(m[pivot][col]) > 1e-300)) return false;
    for (int k = 0; k <= n; ++k) { const double t = m[col][k]; m[col][k] = m[pivot][k]; m[pivot][k] = t; }
    for (int r = 0; r < n; ++r) {
      if (r == col) continue;
      const double f = m[r][col] / m[col][col];
      for (int k = col; k <= n; ++k) m[r][k] -= f * m[col][k];
    }
  }
  for (int i = 0; i < n; ++i) x[i] = m[i][n] / m[i][i];
  return true;
}


// Weighted least squares t_i = t0 + sum a_k J_ik. A reading error of the
// pressure becomes an almost constant error in time, ~tau*sigma_P/P_abs: the
// weights are P_abs^2 (mean of P1 and P2). On a synthetic test with 0.15 mbar of
// noise this gives the curve within 0.1-0.2% and t0 within ~4 ms; weights
// 1/t^2 (relative residuals) gave 0.7-0.9% and 11 ms. Fills curve.a and curve.t0 (0 when !fitT0), the per-release
// residuals (s) and exponents when given. False with too few releases or a
// singular system.
// t0StandardError (s, optional): from the covariance of the fit, s^2 (A'WA)^-1.
inline bool fitVentingTest(const Release *rel, int count, double volume, double patm, bool co2, bool fitT0,
                           double defaultExponent, Curve &curve, float *residuals, float *exponents,
                           double *t0StandardError = nullptr) {
  const int n = fitT0 ? 5 : 4;
  if (count < n + 2) return false;
  double m[5][6] = {};
  for (int i = 0; i < count; ++i) {
    const Release &r = rel[i];
    const double e = isfinite(r.pClose) ? releaseExponent(r.p1, r.pClose, r.p2, patm, defaultExponent)
                                        : defaultExponent;
    if (exponents) exponents[i] = (float)e;
    double j[4];
    releaseIntegrals(volume, r.tempK, r.p1, r.p2, patm, e, co2, j);
    double row[5];
    int c = 0;
    if (fitT0) row[c++] = 1.0;
    for (int k = 0; k < 4; ++k) row[c++] = j[k];
    const double pAbs = 0.5 * (r.p1 + r.p2) + patm;
    const double w = pAbs * pAbs;
    for (int a = 0; a < n; ++a) {
      for (int b = 0; b < n; ++b) m[a][b] += w * row[a] * row[b];
      m[a][n] += w * row[a] * r.seconds;
    }
  }
  double normal[5][6];
  for (int a = 0; a < n; ++a)
    for (int b = 0; b <= n; ++b) normal[a][b] = b < n ? m[a][b] : (a == 0 ? 1.0 : 0.0);
  double x[5], inverse0[5];
  if (!solveLinear(m, n, x)) return false;
  const bool covariance = fitT0 && solveLinear(normal, n, inverse0); // first column of (A'WA)^-1
  curve.t0 = fitT0 ? (float)x[0] : 0.0f;
  for (int k = 0; k < 4; ++k) curve.a[k] = (float)x[(fitT0 ? 1 : 0) + k];
  double weightedSquares = 0.0;
  for (int i = 0; i < count; ++i) {
    const Release &r = rel[i];
    const double e = exponents ? exponents[i] : (isfinite(r.pClose) ? releaseExponent(r.p1, r.pClose, r.p2, patm,
                                                                                   defaultExponent) : defaultExponent);
    double j[4];
    releaseIntegrals(volume, r.tempK, r.p1, r.p2, patm, e, co2, j);
    double model = fitT0 ? x[0] : 0.0;
    for (int k = 0; k < 4; ++k) model += x[(fitT0 ? 1 : 0) + k] * j[k];
    const double residual = r.seconds - model;
    if (residuals) residuals[i] = (float)residual;
    const double pAbs = 0.5 * (r.p1 + r.p2) + patm;
    weightedSquares += pAbs * pAbs * residual * residual;
  }
  if (t0StandardError)
    *t0StandardError = covariance ? sqrt(fmax(0.0, weightedSquares / (count - n) * inverse0[0])) : NAN;
  return true;
}

// Default curve: the venting test of 18/09 (129.6 L, air, Patm 0.9286 bar,
// 10 releases of 20 s from 1.98 to 0.36 bar; t0 not measurable there).
inline Curve defaultCurve() {
  return {{318.6846f, -296.7812f, 218.9469f, -55.9985f}, 0.0f, 0.36f, 1.98f, 0};
}

inline bool validCurve(const Curve &c) {
  if (!(c.pMax > c.pMin) || !(c.pMin >= 0.0f) || !(c.pMax <= 4.0f) || !isfinite(c.t0) || fabs(c.t0) > 1.0f)
    return false;
  for (int i = 0; i <= 10; ++i) {
    const double tau = tauRef(c, c.pMin + (c.pMax - c.pMin) * i / 10.0);
    if (!(tau > 1.0 && tau < 100000.0)) return false;
  }
  return true;
}

} // namespace VentModel
