#pragma once

#include <stdint.h>
#include <math.h>

// Independent of Arduino: the exact controller is exercised by the host bench.
// Temperatures in C, trend in C/min; all clocks are wrap-safe uint32_t millis.
namespace AdaptiveTemperature {
enum State : uint8_t { Idle, Cooling, Heating };
inline float bound(float x, float lo, float hi) { return fmaxf(lo, fminf(hi, x)); }

class Controller {
 public:
  static constexpr float Quantum = 0.0625f;
  static constexpr uint32_t Minute = 60000;

  void reset(bool keepRecovery = false) {
    const bool known = targetKnown_;
    const float target = target_;
    const State state = state_;
    const bool guard = keepRecovery && guard_;
    const uint32_t guardSince = guardSince_;
    const uint32_t guardDuration = guardDuration_;
    *this = Controller();
    targetKnown_ = known; target_ = target; state_ = state;
    guard_ = guard; guardSince_ = guardSince; guardDuration_ = guardDuration;
  }

  void observe(uint32_t now, float temperature, float target, bool enabled,
               bool eligible, State state, bool timedPause, bool simulated = false) {
    if (simulated || !enabled) {
      // Debug and an unchecked Settings option are complete bypasses, including
      // pending recovery gates. Neither may affect the legacy controller.
      reset(); state_ = state; return;
    }
    if (!isfinite(target) || !isfinite(temperature)) {
      reset(true); available_ = false; return;
    }
    if (!targetKnown_ || target != target_) {
      reset(); targetKnown_ = true; target_ = target; state_ = state;
    }
    if (!eligible) {
      if (available_) reset(true);
      state_ = state; available_ = false; return;
    }
    available_ = true;
    // Approach pauses can teach residual drop before regulation is reached.
    // A pulse already on when entering this band may teach only at a timed stop.
    if (!tracking_) {
      state_ = state;
      ready_ = fabsf(temperature - target) <= 0.3f;
      if (ready_ || (temperature >= target && temperature <= target + 2.5f)) {
        tracking_ = true; lastSample_ = now; lastTemperature_ = temperature;
        pulseObserved_ = state == Cooling;
        approachPulse_ = pulseObserved_;
        lastTimedPause_ = timedPause;
        clearTrend(now, temperature);
      }
      return;
    }
    const uint32_t elapsed = now - lastSample_;
    if (elapsed > 5000 || fabsf(temperature - lastTemperature_) > 0.5f) {
      reset(true); state_ = state; return;
    }
    if (fabsf(temperature - target) <= 0.3f) ready_ = true;
    if (state != state_) {
      transition(now, temperature, state, timedPause);
      state_ = state;
      clearTrend(now, temperature);
    }
    if (elapsed < 1000) return;
    if (cycle_) {
      integral_ += double(0.5f * (lastTemperature_ + temperature)) * elapsed;
      duration_ += elapsed;
      if (state == Heating) cycleValid_ = false;
      if (duration_ > 24 * 60 * Minute) cycle_ = false;
    }
    lastSample_ = now; lastTemperature_ = temperature;
    if (binCount_ < 16) bin_[binCount_++] = temperature;
    if (now - binSince_ < 15000) return;
    float sorted[16];
    for (uint8_t i = 0; i < binCount_; ++i) {
      sorted[i] = bin_[i];
      for (uint8_t j = i; j && sorted[j] < sorted[j - 1]; --j) {
        const float v = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = v;
      }
    }
    if (!binCount_) return;
    const float filtered = binCount_ & 1 ? sorted[binCount_ / 2]
        : 0.5f * (sorted[binCount_ / 2 - 1] + sorted[binCount_ / 2]);
    binCount_ = 0; binSince_ = now;
    addTrend(now, filtered);
    if (peakPending_) {
      if (filtered > peak_) { peak_ = filtered; peakConfirm_ = 0; }
      if (filtered <= peak_ - Quantum + 0.001f) ++peakConfirm_;
      else peakConfirm_ = 0;
      if (peakConfirm_ >= 2) {
        const float rise = bound(peak_ - onTemperature_, 0, 0.15f);
        rise_ += bound(0.5f * (rise - rise_), -Quantum, Quantum);
        if (riseSamples_ < 255) ++riseSamples_;
        risePositive_ = rise >= Quantum - 0.001f ? uint8_t(risePositive_ < 3 ? risePositive_ + 1 : 3) : 0;
        peakPending_ = false;
      }
    }
    if (coast_) {
      if (filtered < valley_) { valley_ = filtered; valleySince_ = now; valleyConfirm_ = 0; }
      if (filtered >= valley_ + Quantum - 0.001f) ++valleyConfirm_;
      else valleyConfirm_ = 0;
      if (valleyConfirm_ >= 2) {
        learnDrop(offTemperature_ - valley_);
        recoveryMs_ = uint32_t(bound(float(now - offSince_), float(2 * Minute), float(58 * Minute)));
        coast_ = false;
      } else if (now - offSince_ >= 30 * Minute) {
        // A sustained environmental fall is not an ever-growing motor inertia.
        coast_ = false;
      }
    }
    if (peakPending_ && now - onSince_ > 30 * Minute) peakPending_ = false;
  }

  float onThreshold(float legacyUpper, float legacyLower) const {
    if (!active()) return legacyUpper;
    const float rise = riseSamples_ >= 3 && risePositive_ >= 2 ? rise_ : 0;
    // Never postpone a legacy start, and keep two thermometer steps of separation.
    return fminf(legacyUpper, fmaxf(legacyLower + 2 * Quantum,
                                 target_ + center_ + 0.3f - rise));
  }
  float offThreshold(float legacyLower, float legacyUpper) const {
    if (!active()) return legacyLower;
    const float upper = onThreshold(legacyUpper, legacyLower) - 2 * Quantum;
    return fmaxf(legacyLower, fminf(upper, target_ + center_ - 0.3f + drop_));
  }
  void coolingStopped(uint32_t now, bool anticipated) {
    if ((!active() || !ready_) && !anticipated) return;
    guard_ = true; guardSince_ = now;
    guardDuration_ = uint32_t(bound(float(recoveryMs_ + 2 * Minute), float(20 * Minute), float(60 * Minute)));
  }
  bool allowHeat(uint32_t now, float temperature, float target) const {
    if (!guard_ || target != target_ || now - guardSince_ >= 60 * Minute ||
        temperature < target - 0.9f) return true;
    const uint32_t elapsed = now - guardSince_;
    if (elapsed < guardDuration_) return false;
    if (active() && coast_ && elapsed < 30 * Minute) return false;
    // A warming recovery can proceed naturally for up to an hour; severe or
    // persistent cold is allowed to heat. Unknown/negative trend is not warming.
    return !(trendKnown_ && velocity_ > 0 && temperature >= target - 0.6f);
  }
  bool active() const { return available_ && tracking_; }
  bool learning() const { return active() && (!ready_ || !dropSamples_); }
  float drop() const { return drop_; }
  float rise() const { return rise_; }
  float center() const { return center_; }
  float velocity() const { return velocity_; }
  bool trendKnown() const { return trendKnown_; }
  uint16_t dropSamples() const { return dropSamples_; }
  uint8_t riseSamples() const { return riseSamples_; }

 private:
  void learnDrop(float value) {
    // A large drift/intervention is not a valid cycle observation.
    if (value < -Quantum || value > 0.6f) return;
    drop_ += bound(0.5f * (fmaxf(0, value) - drop_), -0.1f, 0.1f);
    if (dropSamples_ < 65535) ++dropSamples_;
  }
  void interruptCoast(uint32_t now) {
    // The observed drop is only a lower bound if another action interrupts it.
    // It can increase the estimate, never teach a smaller complete response.
    const float partial = offTemperature_ - valley_;
    if (coast_ && now - offSince_ <= 15 * Minute && partial > drop_) learnDrop(partial);
    coast_ = false;
  }
  void transition(uint32_t now, float temperature, State next, bool timedPause) {
    if (state_ == Cooling) {
      // Do not treat a timeout/restart as a complete demand for center/rise.
      lastTimedPause_ = timedPause;
      const bool approachPause = next == Idle && timedPause &&
          temperature >= target_ && temperature <= target_ + 2.5f;
      if (next == Idle && pulseObserved_ && (!approachPulse_ || approachPause)) {
        coast_ = true; offTemperature_ = valley_ = temperature;
        offSince_ = valleySince_ = now; valleyConfirm_ = 0;
      }
      pulseObserved_ = false;
    }
    if (next == Heating) {
      interruptCoast(now); peakPending_ = false; cycleValid_ = false;
    }
    if (next == Cooling) {
      interruptCoast(now);
      const bool fresh = ready_ && !lastTimedPause_;
      if (fresh) {
        if (cycle_ && cycleValid_ && duration_ >= 10 * Minute) {
          const float error = target_ - float(integral_ / duration_);
          center_ = bound(center_ + bound(0.25f * error, -0.05f, 0.05f), -0.2f, 0.2f);
        }
        cycle_ = cycleValid_ = true; integral_ = 0; duration_ = 0;
      }
      peakPending_ = fresh; peakConfirm_ = 0;
      peak_ = onTemperature_ = temperature; onSince_ = now;
      pulseObserved_ = true; approachPulse_ = !ready_; lastTimedPause_ = false;
    }
    if (state_ == Heating && next == Idle) cycleValid_ = false;
  }
  void clearTrend(uint32_t now, float temperature) {
    trendCount_ = binCount_ = 0; trendKnown_ = false; velocity_ = 0;
    binSince_ = now; addTrend(now, temperature);
  }
  void addTrend(uint32_t now, float temperature) {
    if (trendCount_ == 9) {
      for (uint8_t i = 1; i < 9; ++i) { times_[i - 1] = times_[i]; values_[i - 1] = values_[i]; }
      --trendCount_;
    }
    times_[trendCount_] = now; values_[trendCount_++] = temperature;
    trendKnown_ = false;
    if (trendCount_ < 9 || now - times_[0] < 120000) return;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint8_t i = 0; i < trendCount_; ++i) {
      const double x = -double(uint32_t(now - times_[i])) / Minute;
      sx += x; sy += values_[i]; sxx += x * x; sxy += x * values_[i];
    }
    const double denominator = trendCount_ * sxx - sx * sx;
    if (denominator <= 0) return;
    velocity_ = float((trendCount_ * sxy - sx * sy) / denominator);
    const float first = (values_[0] + values_[1] + values_[2]) / 3;
    const float last = (values_[6] + values_[7] + values_[8]) / 3;
    const float span = float(uint32_t(now - times_[0])) / Minute;
    trendKnown_ = fabsf(velocity_) * span >= Quantum &&
                  fabsf(last - first) >= Quantum * 0.5f && velocity_ * (last - first) > 0;
  }

  bool targetKnown_ = false, available_ = false, tracking_ = false, ready_ = false;
  float target_ = 0, lastTemperature_ = 0;
  State state_ = Idle;
  uint32_t lastSample_ = 0, binSince_ = 0;
  float bin_[16] = {}, values_[9] = {};
  uint32_t times_[9] = {};
  uint8_t binCount_ = 0, trendCount_ = 0;
  float velocity_ = 0;
  bool trendKnown_ = false;
  float drop_ = 0, rise_ = 0, center_ = 0;
  uint16_t dropSamples_ = 0;
  uint8_t riseSamples_ = 0, risePositive_ = 0;
  bool peakPending_ = false, pulseObserved_ = false, approachPulse_ = false, lastTimedPause_ = false;
  float peak_ = 0, onTemperature_ = 0;
  uint8_t peakConfirm_ = 0;
  uint32_t onSince_ = 0;
  bool coast_ = false;
  float offTemperature_ = 0, valley_ = 0;
  uint8_t valleyConfirm_ = 0;
  uint32_t offSince_ = 0, valleySince_ = 0, recoveryMs_ = 0;
  bool cycle_ = false, cycleValid_ = false;
  double integral_ = 0;
  uint32_t duration_ = 0;
  bool guard_ = false;
  uint32_t guardSince_ = 0, guardDuration_ = 0;
};
}  // namespace AdaptiveTemperature
