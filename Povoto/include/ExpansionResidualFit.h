#pragma once

#include <stddef.h>
#include <stdint.h>

// t is expressed in seconds. This fit describes only the tested time range;
// it does not validate extrapolations beyond those observations.
struct ExpansionResidualSample {
  double seconds;
  double residual;
};

enum class ExpansionResidualFitStatus : uint8_t {
  Success,
  InsufficientPoints,
  InsufficientDistinctTimes,
  NumericalFailure,
  ZeroCoefficient,
  BoundaryExponent
};

struct ExpansionResidualFitResult {
  double coefficient;
  double exponent;
  double sse;
  double rmse;
  size_t usedPoints;
  size_t discardedPoints;
  size_t distinctTimes;
  ExpansionResidualFitStatus status;
};

ExpansionResidualFitResult fitExpansionResidualCurve(const ExpansionResidualSample *samples,
                                                      size_t count);
const char *expansionResidualFitStatusText(ExpansionResidualFitStatus status);
