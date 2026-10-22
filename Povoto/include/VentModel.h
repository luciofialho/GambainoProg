#pragma once
#include <math.h>
#include <new>
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
constexpr int SIMPSON_INTERVALS = 8; // even; 16 changes the fit by < 1e-4 relative

// Flow curve of the valve and its outlet ("K-beta model"), measured with air on the empty
// fermenter by the venting test: tau(P) = k / psi_eff(P) (s) is the time constant of the absolute
// pressure of V_REF litres of air at T_REF, P in gauge bar. psi_eff is the flow function of the
// pair in series, see seriesPsi(): k is the capacity of the valve nozzle and beta the resistance
// that holds the flow after it. There is no t0: the opening delay of the valve is taken equal to
// its closing delay (the real t0, probably within +-20 ms, is +-1% of the moles of a 2 s vent).
// pMin..pMax only say where the test measured it: the model is physical, with no range.
struct Curve {
  float k;        // s
  float beta;     // outlet resistance, (pm^2 - patm^2) / patm = beta n^2 (n: flow in units of p_up psi)
  float pMin, pMax;
  uint32_t epoch; // local NTP time of the test it came from (0 = default curve)
} __attribute__((packed));

// Ideal-nozzle flow function for the downstream/upstream pressure ratio r.
inline double nozzlePsi(double gamma, double r) {
  const double critical = pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
  if (r <= critical)
    return sqrt(gamma) * pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
  r = fmin(r, 1.0);
  return sqrt(fmax(0.0, 2.0 * gamma / (gamma - 1.0) * (pow(r, 2.0 / gamma) - pow(r, (gamma + 1.0) / gamma))));
}

// Effective flow function of the valve and its outlet (silencer) in series.
// The valve is an ideal nozzle, molar flow ~ pUp psi(r) with r = pm / pUp, pm the pressure between
// the valve and the outlet. The outlet is a dissipative restriction without choking,
// (pm^2 - patm^2) / patm = beta n^2 (n in the units of pUp psi). The same flow goes through both:
// pUp^2 patm beta psi(r)^2 = pUp^2 r^2 - patm^2, one root in (patm / pUp, 1); found by Newton with
// bisection fallback (about 5 iterations). Returns psi(r); NAN when pUp <= patm or beta <= 0.
inline double seriesPsi(double pUp, double patm, double beta, double gamma) {
  if (!(pUp > patm) || !(beta > 0.0)) return NAN;
  const double crit = pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
  const double psiC2 = gamma * pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (gamma - 1.0));
  const double c = 2.0 * gamma / (gamma - 1.0);
  const double pu2 = pUp * pUp, pa2 = patm * patm;
  double lo = patm / pUp, hi = 1.0;
  double r = fmin(fmax(sqrt(pa2 + patm * beta * pu2 * psiC2) / pUp, lo), hi);
  for (int i = 0; i < 60; ++i) {
    double psi2 = psiC2, dpsi2 = 0.0;
    if (r > crit) {
      const double u = pow(r, 1.0 / gamma);
      psi2 = c * (u * u - r * u);
      dpsi2 = c * (2.0 * u * u / (gamma * r) - (1.0 + 1.0 / gamma) * u);
    }
    const double f = patm * beta * pu2 * psi2 - (pu2 * r * r - pa2);
    const double df = patm * beta * pu2 * dpsi2 - 2.0 * pu2 * r;
    if (f > 0.0) lo = r; else hi = r;
    double next = df != 0.0 ? r - f / df : 0.5 * (lo + hi);
    if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
    const bool done = fabs(next - r) < 1e-12;
    r = next;
    if (done) break;
  }
  double psi2 = psiC2;
  if (r > crit) {
    const double u = pow(r, 1.0 / gamma);
    psi2 = c * (u * u - r * u);
  }
  return sqrt(fmax(0.0, psi2));
}

constexpr double TAU_MIN_GAUGE = 0.005; // bar: lowest gauge pressure tau is evaluated at

// tau at a gauge pressure: k / psi_eff(P), air, V_REF, T_REF.
inline double tauRef(const Curve &c, double pGauge, double patm) {
  const double psi = seriesPsi(fmax(pGauge, TAU_MIN_GAUGE) + patm, patm, (double)c.beta, GAMMA_AIR);
  return psi > 0.0 ? (double)c.k / psi : NAN;
}

// C_air(P), mol*K^0.5/(s*bar): molar air flow = C_air * P_abs / sqrt(T).
inline double flowConstantAir(const Curve &c, double pGauge, double patm) {
  const double tau = tauRef(c, pGauge, patm);
  return tau > 0.0 ? V_REF / (R * sqrt(T_REF) * tau) : NAN;
}

// CO2/air molar flow at the same pressures and temperature: sqrt(M_air / M_CO2) (the valve and the
// outlet both go with 1/sqrt(M)) times the ratio of the nozzle functions of the pair (gamma 1.29 / 1.40).
// 0.80-0.81 with the silencer; 0.788 (choked) to 0.796 (0.5 bar) for the valve alone.
inline double co2AirRatio(double beta, double pGauge, double patm) {
  const double pUp = pGauge + patm;
  const double psiAir = seriesPsi(pUp, patm, beta, GAMMA_AIR);
  if (!(psiAir > 0.0)) return sqrt(M_AIR / M_CO2);
  return sqrt(M_AIR / M_CO2) * seriesPsi(pUp, patm, beta, GAMMA_CO2) / psiAir;
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
  return flowConstantAir(c, pGauge, patm) * co2AirRatio((double)c.beta, pGauge, patm) /
      sqrt(co2Z(pGauge + patm, tKelvin)) * level;
}

struct VentCount {
  double moles;      // total moles out of the fermenter (CO2 + vapor)
  double pMeanGauge; // log mean of the absolute pressures at opening and closing
  double cooling;    // sqrt(T0 / T) at the mean pressure
  double flowConstant;
};

// CO2 moles out of one vent: C(P_mean) * P_mean / sqrt(T) * cooling * t.
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
  const double seconds = fmax(0.0, openSeconds);
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
  return rate > 0.0 ? moles / rate : NAN;
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

// S so that t = k S for one release from the settled P1 to the settled P2 (Simpson over the
// settled pressure): the time is the integral of tau(P) g dP, tau = k / psi_eff.
inline double releaseIntegral(double volume, double t0Kelvin, double p1Gauge, double p2Gauge, double patm,
                              double exponent, bool co2, double beta) {
  const double p1 = p1Gauge + patm, p2 = p2Gauge + patm;
  const double scale = volume / V_REF * sqrt(T_REF / t0Kelvin);
  const int n = SIMPSON_INTERVALS;
  const double h = (p1 - p2) / n;
  double total = 0.0;
  for (int i = 0; i <= n; ++i) {
    const double pIso = p2 + i * h;
    const double x = pIso / p1;
    const double pAbs = p1 * pow(x, exponent);
    double g = sqrt(pow(x, exponent - 1.0)) / pAbs;
    if (co2) g /= sqrt(co2Z(pIso, t0Kelvin)) * co2AirRatio(beta, pAbs - patm, patm);
    const double w = (i == 0 || i == n) ? 1.0 : ((i % 2) ? 4.0 : 2.0);
    total += w * g / seriesPsi(pAbs, patm, beta, GAMMA_AIR);
  }
  return scale * total * h / 3.0;
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
    g2 /= sqrt(co2Z(p2, t0Kelvin)) * co2AirRatio((double)c.beta, pAbs2 - patm, patm);
    g1 /= sqrt(co2Z(p1, t0Kelvin)) * co2AirRatio((double)c.beta, p1Gauge, patm);
  }
  s1 = scale * g1 * tauRef(c, p1Gauge, patm);
  s2 = scale * g2 * tauRef(c, pAbs2 - patm, patm);
}

// Reduced chi-square of a fit: sum r_i^2 / (s1^2 n1^2 + s2^2 n2^2 + st^2) / (n - 2),
// with n1, n2 the noise of each settled reading (bar, at least floorBar) and st
// the timing noise (s). ~1: the model follows the data within the noise.
constexpr int FIT_PARAMETERS = 2; // k, beta
inline double reducedChiSquare(const Curve &c, const Release *rel, const float *noise1, const float *noise2,
                               const float *residuals, const float *exponents, int count, double volume,
                               double patm, bool co2, double floorBar, double timingSeconds) {
  if (count <= FIT_PARAMETERS) return NAN;
  double sum = 0.0;
  for (int i = 0; i < count; ++i) {
    double s1, s2;
    releaseSensitivities(c, volume, rel[i].tempK, rel[i].p1, rel[i].p2, patm, exponents[i], co2, s1, s2);
    const double n1 = fmax(isfinite(noise1[i]) ? (double)noise1[i] : 0.0, floorBar);
    const double n2 = fmax(isfinite(noise2[i]) ? (double)noise2[i] : 0.0, floorBar);
    const double variance = s1 * s1 * n1 * n1 + s2 * s2 * n2 * n2 + timingSeconds * timingSeconds;
    sum += (double)residuals[i] * residuals[i] / variance;
  }
  return sum / (count - FIT_PARAMETERS);
}

constexpr double BETA_MIN = 0.01, BETA_MAX = 30.0; // search range of the outlet resistance
constexpr int BETA_ITERATIONS = 20;                    // golden-section steps on ln(beta): 5e-4 in ln(beta)

// Weighted least squares of t = k S for a fixed beta (closed form). False when singular.
struct ProfileFit { double sse, k; };
inline bool profileFit(const double *rows, const double *ts, const double *ws, int n, ProfileFit &out) {
  double sxx = 0.0, sxy = 0.0;
  for (int i = 0; i < n; ++i) {
    sxx += ws[i] * rows[i] * rows[i];
    sxy += ws[i] * rows[i] * ts[i];
  }
  if (!(sxx > 0.0)) return false;
  out.k = sxy / sxx;
  out.sse = 0.0;
  for (int i = 0; i < n; ++i) {
    const double e = ts[i] - out.k * rows[i];
    out.sse += ws[i] * e * e;
  }
  return isfinite(out.sse);
}

// Fit of the flow curve: t_i = k S_i(beta), S_i the release integral. For each beta the weighted
// least squares gives k in closed form; beta is found by golden-section search on ln(beta).
// A pressure reading error becomes an almost constant error in time, ~tau*sigma_P/P_abs: the weights
// are P_abs^2 (mean of P1 and P2). Fills curve.k and curve.beta, the per-release residuals (s) and
// exponents when given. False with too few releases, no memory or a singular system.
// yieldFn (optional) is called after each evaluation of beta (the fit takes seconds on the board).
inline bool fitVentingTest(const Release *rel, int count, double volume, double patm, bool co2,
                           double defaultExponent, Curve &curve, float *residuals, float *exponents,
                           void (*yieldFn)() = nullptr) {
  if (count < FIT_PARAMETERS + 2) return false;
  double *work = new (std::nothrow) double[4 * count];
  if (!work) return false;
  double *rows = work, *ts = work + count, *ws = work + 2 * count, *exps = work + 3 * count;
  for (int i = 0; i < count; ++i) {
    const Release &r = rel[i];
    exps[i] = isfinite(r.pClose) ? releaseExponent(r.p1, r.pClose, r.p2, patm, defaultExponent) : defaultExponent;
    ts[i] = r.seconds;
    const double pAbs = 0.5 * (r.p1 + r.p2) + patm;
    ws[i] = pAbs * pAbs;
  }
  bool ok = true;
  auto evaluate = [&](double beta, ProfileFit &out) {
    for (int i = 0; i < count; ++i)
      rows[i] = releaseIntegral(volume, rel[i].tempK, rel[i].p1, rel[i].p2, patm, exps[i], co2, beta);
    if (yieldFn) yieldFn();
    if (!profileFit(rows, ts, ws, count, out)) ok = false;
  };
  const double golden = (sqrt(5.0) - 1.0) / 2.0;
  double lo = log(BETA_MIN), hi = log(BETA_MAX);
  double x1 = hi - golden * (hi - lo), x2 = lo + golden * (hi - lo);
  ProfileFit f1 = {}, f2 = {};
  evaluate(exp(x1), f1);
  evaluate(exp(x2), f2);
  for (int i = 0; i < BETA_ITERATIONS && ok; ++i) {
    if (f1.sse < f2.sse) {
      hi = x2; x2 = x1; f2 = f1;
      x1 = hi - golden * (hi - lo);
      evaluate(exp(x1), f1);
    } else {
      lo = x1; x1 = x2; f1 = f2;
      x2 = lo + golden * (hi - lo);
      evaluate(exp(x2), f2);
    }
  }
  ProfileFit best = {};
  const double beta = exp(0.5 * (lo + hi));
  if (ok) evaluate(beta, best);
  if (ok) {
    curve.k = (float)best.k;
    curve.beta = (float)beta;
    for (int i = 0; i < count; ++i) {
      if (residuals) residuals[i] = (float)(ts[i] - best.k * rows[i]);
      if (exponents) exponents[i] = (float)exps[i];
    }
  }
  delete[] work;
  return ok;
}

// Default curve: the three venting tests with the silencer of 20-21/10/2026 (130.9 L, air, Patm
// 0.9286 bar, 0.14-2.08 bar) fitted together without t0 and without the first release of each test,
// each test with its own scale (1.000, 0.975, 0.975); k is the shape fit times the mean scale.
inline Curve defaultCurve() {
  return {81.6f, 0.88f, 0.14f, 2.08f, 0};
}

inline bool validCurve(const Curve &c) {
  if (!(c.pMax > c.pMin) || !(c.pMin >= 0.0f) || !(c.pMax <= 4.0f)) return false;
  if (!(c.k > 1.0f && c.k < 10000.0f) || !(c.beta > 1e-3f && c.beta < 1000.0f)) return false;
  for (int i = 0; i <= 10; ++i) {
    const double tau = tauRef(c, c.pMin + (c.pMax - c.pMin) * i / 10.0, 0.93);
    if (!(tau > 1.0 && tau < 100000.0)) return false;
  }
  return true;
}

// Vapor in the vented gas (docs/vent.md): the gas that leaves is saturated with
// the vapor of the liquid at its temperature, so only the fraction 1 - y of the
// counted moles is CO2. No parameter; y = (p_water + p_ethanol) / P_abs.
constexpr double WATER_ACTIVITY = 0.98;            // beer
constexpr double ETHANOL_ACTIVITY_COEFFICIENT = 5.0; // infinite dilution in water

// Saturation pressure of water, bar (Buck), liquid temperature in C.
inline double waterSaturationPressure(double tCelsius) {
  return 0.0061121 * exp((18.678 - tCelsius / 234.5) * (tCelsius / (257.14 + tCelsius)));
}

// Saturation pressure of ethanol, bar (Antoine, 1-78 C).
inline double ethanolSaturationPressure(double tCelsius) {
  return 0.00133322 * pow(10.0, 8.20417 - 1642.89 / (230.300 + tCelsius));
}

// Mole fraction of ethanol in the liquid from the ABV (% v/v): ethanol 0.789 g/mL
// (46.07 g/mol) in the rest of the volume taken as water (0.998 g/mL, 18.015 g/mol).
inline double ethanolMoleFraction(double abvPercent) {
  const double abv = fmin(fmax(abvPercent, 0.0), 30.0);
  const double ethanol = 0.789 * abv / 46.07;
  const double water = 0.998 * (100.0 - abv) / 18.015;
  return ethanol / (ethanol + water);
}

// Vapor fraction of the vented gas at the absolute pressure pAbs (bar, the mean
// pressure of the vent), 0 when the inputs are unusable.
inline double ventVaporFraction(double liquidCelsius, double abvPercent, double pAbs) {
  if (!isfinite(liquidCelsius) || liquidCelsius < -5.0 || liquidCelsius > 60.0 || !(pAbs > 0.1)) return 0.0;
  const double abv = isfinite(abvPercent) ? abvPercent : 0.0;
  const double vapor = WATER_ACTIVITY * waterSaturationPressure(liquidCelsius) +
      ethanolMoleFraction(abv) * ETHANOL_ACTIVITY_COEFFICIENT * ethanolSaturationPressure(liquidCelsius);
  return fmin(fmax(vapor / pAbs, 0.0), 0.5);
}

} // namespace VentModel
