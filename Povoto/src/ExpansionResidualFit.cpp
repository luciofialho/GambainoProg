#include "ExpansionResidualFit.h"

#include <math.h>

namespace {
constexpr double kMinExponent = 0.05;
constexpr double kMaxExponent = 5.0;
constexpr unsigned kGridSubdivisions = 200;
constexpr double kExponentTolerance = 1e-6;
constexpr unsigned kGoldenMaxIterations = 128;

struct Evaluation {
  double coefficient;
  double sse;
  bool valid;
};

bool usable(const ExpansionResidualSample &sample) {
  return isfinite(sample.seconds) && isfinite(sample.residual) && sample.seconds > 0.0;
}

Evaluation evaluate(const ExpansionResidualSample *samples, size_t count, double exponent) {
  // Measured residuals intentionally remain signed here. Negative and zero
  // observations are noise-bearing data points, not values to clamp away.
  double numerator = 0.0;
  double denominator = 0.0;
  for (size_t i = 0; i < count; ++i) {
    if (!usable(samples[i])) continue;
    const double x = pow(samples[i].seconds, -exponent);
    if (!isfinite(x)) return {NAN, NAN, false};
    numerator += x * samples[i].residual;
    denominator += x * x;
  }
  if (!isfinite(numerator) || !isfinite(denominator) || denominator <= 0.0)
    return {NAN, NAN, false};
  const double coefficient = fmax(0.0, numerator / denominator);
  double sse = 0.0;
  for (size_t i = 0; i < count; ++i) {
    if (!usable(samples[i])) continue;
    const double predicted = coefficient * pow(samples[i].seconds, -exponent);
    const double error = predicted - samples[i].residual;
    if (!isfinite(predicted) || !isfinite(error)) return {NAN, NAN, false};
    sse += error * error;
  }
  return isfinite(sse) ? Evaluation{coefficient, sse, true} : Evaluation{NAN, NAN, false};
}

double goldenMinimum(const ExpansionResidualSample *samples, size_t count, double left, double right) {
  constexpr double phi = 1.6180339887498948482;
  double c = right - (right - left) / phi;
  double d = left + (right - left) / phi;
  double fc = evaluate(samples, count, c).sse;
  double fd = evaluate(samples, count, d).sse;
  for (unsigned iteration = 0; iteration < kGoldenMaxIterations && right - left > kExponentTolerance;
       ++iteration) {
    if (!isfinite(fc) || !isfinite(fd)) return NAN;
    if (fc <= fd) {
      right = d;
      d = c;
      fd = fc;
      c = right - (right - left) / phi;
      fc = evaluate(samples, count, c).sse;
    } else {
      left = c;
      c = d;
      fc = fd;
      d = left + (right - left) / phi;
      fd = evaluate(samples, count, d).sse;
    }
  }
  return (left + right) * 0.5;
}
}

ExpansionResidualFitResult fitExpansionResidualCurve(const ExpansionResidualSample *samples, size_t count) {
  ExpansionResidualFitResult result = {NAN, NAN, NAN, NAN, 0, 0, 0,
                                       ExpansionResidualFitStatus::NumericalFailure};
  if (!samples || count == 0) {
    result.status = ExpansionResidualFitStatus::InsufficientPoints;
    return result;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!usable(samples[i])) {
      ++result.discardedPoints;
      continue;
    }
    ++result.usedPoints;
    bool newTime = true;
    for (size_t j = 0; j < i; ++j) {
      if (usable(samples[j]) && samples[j].seconds == samples[i].seconds) {
        newTime = false;
        break;
      }
    }
    if (newTime) ++result.distinctTimes;
  }
  if (result.usedPoints < 3) {
    result.status = ExpansionResidualFitStatus::InsufficientPoints;
    return result;
  }
  if (result.distinctTimes < 3) {
    result.status = ExpansionResidualFitStatus::InsufficientDistinctTimes;
    return result;
  }

  double gridSse[kGridSubdivisions + 1];
  for (unsigned i = 0; i <= kGridSubdivisions; ++i) {
    const double exponent = kMinExponent + (kMaxExponent - kMinExponent) * i / kGridSubdivisions;
    const Evaluation evaluation = evaluate(samples, count, exponent);
    if (!evaluation.valid) return result;
    gridSse[i] = evaluation.sse;
  }

  double bestExponent = kMinExponent;
  double bestSse = gridSse[0];
  for (unsigned i = 1; i <= kGridSubdivisions; ++i) {
    if (gridSse[i] < bestSse) {
      bestSse = gridSse[i];
      bestExponent = kMinExponent + (kMaxExponent - kMinExponent) * i / kGridSubdivisions;
    }
  }
  for (unsigned i = 1; i < kGridSubdivisions; ++i) {
    if (gridSse[i] > gridSse[i - 1] || gridSse[i] > gridSse[i + 1]) continue;
    const double step = (kMaxExponent - kMinExponent) / kGridSubdivisions;
    const double candidate = goldenMinimum(samples, count, kMinExponent + (i - 1) * step,
                                            kMinExponent + (i + 1) * step);
    const Evaluation evaluation = evaluate(samples, count, candidate);
    if (evaluation.valid && evaluation.sse < bestSse) {
      bestSse = evaluation.sse;
      bestExponent = candidate;
    }
  }
  const Evaluation best = evaluate(samples, count, bestExponent);
  if (!best.valid) return result;
  result.coefficient = best.coefficient;
  result.exponent = bestExponent;
  result.sse = best.sse;
  result.rmse = sqrt(best.sse / result.usedPoints);
  if (!isfinite(result.rmse)) return result;
  if (result.coefficient == 0.0) {
    result.status = ExpansionResidualFitStatus::ZeroCoefficient;
  } else if (fabs(result.exponent - kMinExponent) <= kExponentTolerance ||
             fabs(result.exponent - kMaxExponent) <= kExponentTolerance) {
    result.status = ExpansionResidualFitStatus::BoundaryExponent;
  } else {
    result.status = ExpansionResidualFitStatus::Success;
  }
  return result;
}

const char *expansionResidualFitStatusText(ExpansionResidualFitStatus status) {
  switch (status) {
    case ExpansionResidualFitStatus::Success: return "success";
    case ExpansionResidualFitStatus::InsufficientPoints: return "at least three valid points are required";
    case ExpansionResidualFitStatus::InsufficientDistinctTimes: return "at least three distinct valid times are required";
    case ExpansionResidualFitStatus::NumericalFailure: return "numerical failure";
    case ExpansionResidualFitStatus::ZeroCoefficient: return "inadequate fit: coefficient A is zero";
    case ExpansionResidualFitStatus::BoundaryExponent: return "inadequate fit: exponent B reached the search boundary";
  }
  return "unknown fit status";
}
