#ifndef NUCLEATION_ESTIMATE_H
#define NUCLEATION_ESTIMATE_H

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// Estimate of the nucleation window after a dry or dynamic hopping task
// (docs/dissolved-co2.md, section 8). Information only: the firmware never
// changes nucleationWindow.
//
// Quantity: the gas-phase CO2 G = ejected + headspace (gasPhaseCO2Mols(), the
// one of GasCO2Rate), not the SG and not the dissolved-CO2 model.
//
// - Reference: when the task starts, a least-squares line of G over the existing
//   co2EvolutionHistory (71 samples of 60 s, ~70 min; no buffer of its own): slope
//   (reference rate) and sigma of the residuals. At least 60 samples, none with
//   externalStep; otherwise "no baseline".
// - Observation: from the end of the task, 3 h in blocks of 10 min. Excess of a
//   block = delta G of the block - slope x its duration. 18 floats and a few sums;
//   nothing is written to flash during the observation.
// - Noise of a block: sigma_block = sqrt(2) x sigma of the residuals.
// - Window: end of the first block, counted from the end of the task, from which 3
//   blocks in a row have an excess below max(10% of the largest block,
//   2 sigma_block) (and after the largest block). Resolution: 10 min.
// The pure part (this header) is mirrored by tests/nucleation_estimate.py.

namespace Nucleation {

constexpr uint8_t BLOCKS = 18;                     // 3 h of 10-min blocks
constexpr uint32_t BLOCK_MS = 600000UL;
constexpr uint8_t MIN_SAMPLES = 60;                // of the reference
constexpr uint8_t SETTLE_RUN = 3;
constexpr double SETTLE_PEAK_FRACTION = 0.10;
constexpr double SETTLE_SIGMAS = 2.0;
constexpr double NOISY_PEAK_SIGMAS = 4.0;          // largest block below this many sigma_block
constexpr float VENTED_PRESSURE_BAR = 0.1f;        // gauge: below it the fermenter is open
constexpr double BLOCK_SIGMA_FACTOR = 1.4142135623730951; // sqrt(2)

// One flag per record, in this priority order.
enum Quality : uint8_t { Q_OK, Q_NOT_SETTLED, Q_NOISY_BASELINE, Q_VENTED, Q_NO_BASELINE, Q_INTERRUPTED };
enum Stage : uint8_t { IDLE, TASK, OBSERVING };

inline const char *qualityLabel(uint8_t q) {
  switch (q) {
    case Q_OK: return "ok";
    case Q_NOT_SETTLED: return "not settled";
    case Q_NOISY_BASELINE: return "noisy baseline";
    case Q_VENTED: return "vented";
    case Q_NO_BASELINE: return "no baseline";
    default: return "interrupted";
  }
}

struct Reference {
  bool ok;
  double slope;      // mol/s
  double sigma;      // mol, residuals of G about the line
  char reason[32];   // why not ok
};

// Fit over n samples; get(i, ms, gas, externalStep) returns the i-th, oldest first.
template <typename Get>
Reference fitReference(uint16_t n, Get get) {
  Reference ref = {};
  if (n < MIN_SAMPLES) {
    snprintf(ref.reason, sizeof(ref.reason), "%u samples", (unsigned)n);
    return ref;
  }
  uint32_t firstMs = 0;
  double sx = 0.0, sy = 0.0;
  for (uint16_t i = 0; i < n; ++i) {
    uint32_t ms;
    double gas;
    bool external;
    get(i, ms, gas, external);
    if (external || !isfinite(gas)) {
      snprintf(ref.reason, sizeof(ref.reason), "external step");
      return ref;
    }
    if (i == 0) firstMs = ms;
    sx += (ms - firstMs) / 1000.0;
    sy += gas;
  }
  const double mx = sx / n, my = sy / n;
  double sxx = 0.0, sxy = 0.0;
  for (uint16_t i = 0; i < n; ++i) {
    uint32_t ms;
    double gas;
    bool external;
    get(i, ms, gas, external);
    const double x = (ms - firstMs) / 1000.0 - mx;
    sxx += x * x;
    sxy += x * (gas - my);
  }
  if (!(sxx > 0.0)) {
    snprintf(ref.reason, sizeof(ref.reason), "no time span");
    return ref;
  }
  ref.slope = sxy / sxx;
  double ss = 0.0;
  for (uint16_t i = 0; i < n; ++i) {
    uint32_t ms;
    double gas;
    bool external;
    get(i, ms, gas, external);
    const double x = (ms - firstMs) / 1000.0 - mx;
    const double r = (gas - my) - ref.slope * x;
    ss += r * r;
  }
  ref.sigma = sqrt(ss / (n - 2));
  ref.ok = isfinite(ref.slope) && isfinite(ref.sigma);
  if (!ref.ok) snprintf(ref.reason, sizeof(ref.reason), "unusable fit");
  return ref;
}

struct Result {
  bool ready;
  uint8_t quality;
  char reason[32];            // detail of interrupted / no baseline (empty otherwise)
  float taskMinutes;
  float configuredMinutes;
  float estimatedMinutes;     // NAN = no estimate; resolution 10 min
  float excessDuringTaskMol;
  float excessInsideMol;      // inside the configured window
  float excessAfterMol;       // after it, up to 3 h
  float baselineSlope;        // mol/s
  float blockSigma;           // mol, noise of one 10-min block
  float peakBlockExcess;      // mol, largest block
  float observedMinutes;
};

class Estimator {
 public:
  Estimator() { resetAll(); }

  void resetAll() {
    stage_ = IDLE;
    result_ = {};
  }

  Stage stage() const { return stage_; }
  float observedMinutes(uint32_t nowMs) const {
    return stage_ == OBSERVING ? (nowMs - endMs) / 60000.0f : 0.0f;
  }

  // A hopping task started (the reference is the caller's fit of the buffer). A
  // hopping task that had not ended is replaced; one being observed is interrupted.
  void taskStarted(uint32_t nowMs, double gas, const Reference &ref, float gaugePressure) {
    if (stage_ == OBSERVING) {
      interruptedReason("new task");
      finish(nowMs);
    }
    ref_ = ref;
    startMs = nowMs;
    startGas = gas;
    vented_ = gaugePressure < VENTED_PRESSURE_BAR;
    interrupted_ = false;
    interruptReason_[0] = 0;
    stage_ = TASK;
  }

  // Any other task: it interrupts an observation and drops a hopping task that had not ended.
  void otherTaskStarted(uint32_t nowMs) {
    if (stage_ == OBSERVING) {
      interruptedReason("other task");
      finish(nowMs);
    } else if (stage_ == TASK) {
      stage_ = IDLE;
    }
  }

  void taskCancelled() {
    if (stage_ == TASK) stage_ = IDLE;
  }

  // A change that makes the reference or the observation meaningless (setpoint, mode).
  void disturb(const char *reason, uint32_t nowMs) {
    if (stage_ == TASK) {
      interruptedReason(reason);
    } else if (stage_ == OBSERVING) {
      interruptedReason(reason);
      finish(nowMs);
    }
  }

  void taskEnded(uint32_t nowMs, double gas, double configuredMinutes, float gaugePressure) {
    if (stage_ != TASK) return;
    if (gaugePressure < VENTED_PRESSURE_BAR) vented_ = true;
    endMs = nowMs;
    configuredMin = configuredMinutes;
    taskMin = (nowMs - startMs) / 60000.0;
    duringTask = ref_.ok ? (gas - startGas) - ref_.slope * ((nowMs - startMs) / 1000.0) : 0.0;
    blockStartMs = nowMs;
    blockStartGas = gas;
    blockCount = 0;
    inside = after = 0.0;
    stage_ = OBSERVING;
    // Nothing to observe without a usable reference, in an open fermenter or after an interruption.
    if (!ref_.ok || vented_ || interrupted_) finish(nowMs);
  }

  // Once per CO2 sample (60 s).
  void sample(uint32_t nowMs, double gas, float gaugePressure, bool externalStep) {
    if (!isfinite(gas)) return;
    if (stage_ == TASK) {
      if (gaugePressure < VENTED_PRESSURE_BAR) vented_ = true;
    } else if (stage_ == OBSERVING) {
      if (externalStep) {
        interruptedReason("external step");
        finish(nowMs);
        return;
      }
      if (nowMs - blockStartMs >= BLOCK_MS) closeBlock(nowMs, gas);
    }
  }

  bool takeResult(Result &out) {
    if (!result_.ready) return false;
    out = result_;
    result_.ready = false;
    return true;
  }

 private:
  Reference ref_ = {};
  Stage stage_;
  uint32_t startMs = 0, endMs = 0, blockStartMs = 0;
  double startGas = 0.0, blockStartGas = 0.0;
  double configuredMin = 0.0, taskMin = 0.0, duringTask = 0.0;
  bool vented_ = false, interrupted_ = false;
  char interruptReason_[32] = "";
  float blocks[BLOCKS] = {};
  uint8_t blockCount = 0;
  double inside = 0.0, after = 0.0;
  Result result_;

  void interruptedReason(const char *reason) {
    if (interrupted_) return; // the first cause is kept
    interrupted_ = true;
    strncpy(interruptReason_, reason, sizeof(interruptReason_) - 1);
    interruptReason_[sizeof(interruptReason_) - 1] = 0;
  }

  void closeBlock(uint32_t nowMs, double gas) {
    const double seconds = (nowMs - blockStartMs) / 1000.0;
    const double excess = (gas - blockStartGas) - ref_.slope * seconds;
    // Inside / after the configured window (minutes from the end of the task).
    const double from = (blockStartMs - endMs) / 60000.0, to = (nowMs - endMs) / 60000.0;
    const double fraction = fmin(fmax((configuredMin - from) / (to - from), 0.0), 1.0);
    inside += excess * fraction;
    after += excess * (1.0 - fraction);
    blocks[blockCount++] = (float)excess;
    blockStartMs = nowMs;
    blockStartGas = gas;
    if (blockCount >= BLOCKS) finish(nowMs);
  }

  void finish(uint32_t nowMs) {
    result_ = {};
    result_.ready = true;
    result_.taskMinutes = (float)taskMin;
    result_.configuredMinutes = (float)configuredMin;
    result_.estimatedMinutes = NAN;
    result_.excessDuringTaskMol = (float)duringTask;
    result_.excessInsideMol = (float)inside;
    result_.excessAfterMol = (float)after;
    result_.baselineSlope = ref_.ok ? (float)ref_.slope : NAN;
    result_.blockSigma = ref_.ok ? (float)(BLOCK_SIGMA_FACTOR * ref_.sigma) : NAN;
    result_.observedMinutes = (nowMs - endMs) / 60000.0f;
    // Largest block and the window.
    float peak = 0.0f;
    uint8_t peakIndex = 0;
    for (uint8_t i = 0; i < blockCount; ++i) {
      if (blocks[i] > peak) {
        peak = blocks[i];
        peakIndex = i;
      }
    }
    result_.peakBlockExcess = peak;
    const double sigmaBlock = BLOCK_SIGMA_FACTOR * ref_.sigma;
    bool settled = false;
    double windowMin = NAN;
    if (ref_.ok && blockCount == BLOCKS && peak > 0.0f) {
      const double limit = fmax(SETTLE_PEAK_FRACTION * peak, SETTLE_SIGMAS * sigmaBlock);
      for (uint8_t j = peakIndex + 1; j + SETTLE_RUN <= blockCount && !settled; ++j) {
        bool below = true;
        for (uint8_t m = 0; m < SETTLE_RUN; ++m) below = below && blocks[j + m] < limit;
        if (below) {
          settled = true;
          windowMin = j * (BLOCK_MS / 60000.0);
        }
      }
    }
    if (interrupted_) {
      result_.quality = Q_INTERRUPTED;
      strncpy(result_.reason, interruptReason_, sizeof(result_.reason) - 1);
    } else if (!ref_.ok) {
      result_.quality = Q_NO_BASELINE;
      strncpy(result_.reason, ref_.reason, sizeof(result_.reason) - 1);
    } else if (vented_) {
      result_.quality = Q_VENTED;
    } else if (!(peak >= NOISY_PEAK_SIGMAS * sigmaBlock && peak > 0.0f)) {
      result_.quality = Q_NOISY_BASELINE;
    } else if (!settled) {
      result_.quality = Q_NOT_SETTLED;
    } else {
      result_.quality = Q_OK;
      result_.estimatedMinutes = (float)windowMin;
    }
    stage_ = IDLE;
  }
};

} // namespace Nucleation

#endif
