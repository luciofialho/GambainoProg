#include "../include/AdaptiveTemperature.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

using namespace AdaptiveTemperature;

struct Rig {
  Controller c;
  uint32_t now = 1000;
  float temperature = 13, target = 13;
  State state = Idle;
  bool enabled = true, eligible = true, timed = false, simulated = false;
  void tick(float t) {
    now += 1000; temperature = t;
    c.observe(now, t, target, enabled, eligible, state, timed, simulated);
  }
  void hold(unsigned seconds, float t) { for (unsigned i = 0; i < seconds; ++i) tick(t); }
  void ramp(unsigned seconds, float to) {
    const float from = temperature;
    for (unsigned i = 1; i <= seconds; ++i) tick(from + (to - from) * i / seconds);
  }
  void cycle() {
    hold(30, 13); ramp(300, 13.25f);
    state = Cooling; tick(13.25f); ramp(600, 12.8f);
    state = Idle; tick(12.8f); c.coolingStopped(now, false);
    ramp(480, 12.45f); hold(120, 12.45f); ramp(120, 12.6f);
    assert(c.dropSamples() > 0);
    ramp(1800, 13.25f); state = Cooling; tick(13.25f);
  }
};

int main() {
  {
    // Capture a timed stop even when cooling was already on at band entry.
    Rig r; r.state = Cooling; r.tick(16); r.ramp(120, 15.5f);
    assert(r.c.active()); // Exactly target + 2.5 C is eligible.
    r.hold(30, 15.5f); r.state = Idle; r.timed = true; r.tick(15.5f);
    r.c.coolingStopped(r.now, false);
    assert(r.c.allowHeat(r.now, 12.6f, 13)); // Observation alone adds no recovery gate.
    r.ramp(180, 15.2f); r.hold(60, 15.2f); r.ramp(120, 15.35f);
    assert(r.c.dropSamples() == 1 && r.c.drop() > 0);
    assert(r.c.learning()); // A provisional drop is not yet a regulation cycle.
    assert(r.c.riseSamples() == 0 && r.c.center() == 0);
    assert(r.c.offThreshold(12.7f, 13.3f) > 12.7f);
    assert(r.c.offThreshold(12.8f, 13.3f) >= 12.8f);
    assert(r.c.onThreshold(13.3f, 12.8f) == 13.3f);
    // An incomplete second pause cannot teach a smaller drop.
    r.state = Cooling; r.timed = false; r.tick(15.35f); r.ramp(180, 15.0f);
    r.state = Idle; r.timed = true; r.tick(15.0f); r.ramp(60, 14.95f);
    const float learned = r.c.drop();
    r.state = Cooling; r.timed = false; r.tick(14.95f);
    assert(r.c.drop() == learned && r.c.riseSamples() == 0 && r.c.center() == 0);
    // Crossing the regulation band does not train the approach pulse's tail.
    r.ramp(600, 12.8f); r.state = Idle; r.tick(12.8f);
    r.ramp(180, 12.6f); r.hold(60, 12.6f); r.ramp(120, 12.75f);
    assert(r.c.drop() == learned && r.c.riseSamples() == 0 && r.c.center() == 0);
    // A new regulation demand starts rise/center learning normally.
    r.ramp(300, 13.25f); r.state = Cooling; r.tick(13.25f);
    r.ramp(120, 13.0f);
    assert(r.c.riseSamples() == 1 && r.c.center() == 0);
    r.target = 8; r.tick(13.0f);
    assert(!r.c.active() && r.c.dropSamples() == 0 && r.c.drop() == 0);
  }
  {
    Rig r; r.state = Cooling; r.tick(15); r.hold(30, 15);
    r.state = Idle; r.timed = true; r.tick(15); r.ramp(90, 14.75f);
    r.state = Cooling; r.timed = false; r.tick(14.75f);
    assert(r.c.dropSamples() == 1 && r.c.drop() > 0); // Partial drop can raise the estimate.
    assert(r.c.riseSamples() == 0 && r.c.center() == 0);
    r.simulated = true; r.tick(14.75f);
    assert(!r.c.active() && r.c.dropSamples() == 0); // Debug also clears approach learning.
    r.simulated = false; r.tick(14.75f);
    r.eligible = false; r.tick(14.75f);
    assert(!r.c.active() && r.c.dropSamples() == 0);
  }
  {
    // Merely being idle in the wider band does not supply a cooling event.
    Rig r; r.tick(15); r.ramp(180, 14.7f); r.ramp(120, 14.85f);
    assert(r.c.dropSamples() == 0 && r.c.riseSamples() == 0 && r.c.center() == 0);
    r.state = Cooling; r.tick(14.85f); r.ramp(120, 14.7f);
    r.state = Idle; r.timed = false; r.tick(14.7f); // Ordinary idle, not a timed pause.
    r.ramp(180, 14.4f); r.hold(60, 14.4f); r.ramp(120, 14.55f);
    assert(r.c.dropSamples() == 0 && r.c.riseSamples() == 0 && r.c.center() == 0);
  }
  {
    Rig r; r.state = Cooling; r.tick(15.5625f); r.hold(30, 15.5625f);
    assert(!r.c.active()); // Outside the approach band.
    r.state = Idle; r.timed = true; r.tick(15.5625f);
    r.ramp(180, 15.3f); r.ramp(120, 15.45f);
    assert(r.c.dropSamples() == 0); // Entering during a pause cannot reconstruct its stop.
  }
  {
    // A pulse observed in-band cannot train an out-of-band timed stop.
    Rig r; r.state = Cooling; r.tick(15.4f); r.ramp(180, 15.6f);
    r.state = Idle; r.timed = true; r.tick(15.6f);
    r.ramp(180, 15.3f); r.hold(60, 15.3f); r.ramp(120, 15.45f);
    assert(r.c.dropSamples() == 0 && r.c.riseSamples() == 0 && r.c.center() == 0);
  }
  {
    // Starting below the target's regulation band does not enable approach learning.
    Rig r; r.state = Cooling; r.tick(12); r.hold(30, 12);
    assert(!r.c.active());
  }
  {
    Rig r; r.cycle();
    assert(r.c.active() && r.c.drop() > 0 && r.c.drop() <= .101f);
    assert(r.c.center() > 0); // Time spent below target shifts the center up.
    // This is the firmware's composition: lower cut is never moved down,
    // upper start is never postponed, and every legacy stop remains a stop.
    for (float lo : {12.7f, 12.8f, 13.0f}) {
      const float cut = r.c.offThreshold(lo, 13.3f);
      const float on = r.c.onThreshold(13.3f, lo);
      assert(cut >= lo && cut <= on - 2 * Controller::Quantum + .0001f);
      assert(on <= 13.3f);
      for (float t = 12; t < 14; t += .001f)
        if (t <= lo) assert(t <= cut);
    }
    assert(r.c.offThreshold(12.7f, 13.3f) > 12.7f);
    r.target = 8; r.tick(13.25f);
    assert(!r.c.active() && r.c.drop() == 0 && r.c.rise() == 0 && r.c.center() == 0);
    assert(r.c.onThreshold(8.3f, 7.7f) == 8.3f);
    assert(r.c.offThreshold(7.7f, 8.3f) == 7.7f);
    r.ramp(900, 8.2f); assert(r.c.active());
    assert(r.c.dropSamples() == 0); // Approach did not train an old-target pulse.
  }
  {
    Rig r; r.cycle(); r.eligible = false; r.hold(300, 13);
    assert(!r.c.active() && r.c.drop() == 0 && r.c.center() == 0);
    r.eligible = true; r.tick(13);
    assert(r.c.dropSamples() == 0);
    r.enabled = false; r.tick(13);
    assert(r.c.offThreshold(12.8f, 13.3f) == 12.8f);
    assert(r.c.onThreshold(13.3f, 12.8f) == 13.3f);
  }
  {
    Rig r; r.hold(30, 13);
    for (unsigned i = 0; i < 300; ++i) r.tick(13 + ((i & 1) ? .0625f : 0));
    assert(!r.c.trendKnown()); // Adjacent-step flicker is not confirmed warming.
    r.ramp(180, 13.5f);
    assert(r.c.trendKnown() && r.c.velocity() > 0);
    r.state = Heating; r.tick(13.5f);
    assert(!r.c.trendKnown()); // Window never straddles a relay transition.
    r.ramp(180, 13.2f);
    assert(r.c.trendKnown() && r.c.velocity() < 0); // Cold ambient can dominate heat.
  }
  {
    Rig r; r.hold(30, 13); r.state = Cooling; r.tick(13);
    r.ramp(120, 12.9f); r.state = Idle; r.tick(12.9f);
    r.c.coolingStopped(r.now, true);
    const uint32_t stopped = r.now;
    r.ramp(90, 12.65f);
    r.state = Heating; r.tick(12.65f);
    assert(r.c.drop() > 0); // Interrupted observation supplies only a lower bound.
    const float partial = r.c.drop();
    r.hold(120, 12.65f); assert(r.c.drop() == partial); // Heat is not learned as recovery.
    assert(!r.c.allowHeat(stopped + 10 * Controller::Minute, 12.6f, 13));
    assert(r.c.allowHeat(stopped + 10 * Controller::Minute, 12.0f, 13));
    r.enabled = false; r.tick(12.65f);
    assert(r.c.allowHeat(stopped + 10 * Controller::Minute, 12.6f, 13));
    assert(r.c.onThreshold(13.3f, 12.8f) == 13.3f);
    assert(r.c.offThreshold(12.8f, 13.3f) == 12.8f);
    r.tick(NAN); // Even a missing sensor cannot retain a gate while disabled.
    assert(r.c.allowHeat(stopped + 10 * Controller::Minute, 12.6f, 13));
    assert(r.c.allowHeat(stopped + 61 * Controller::Minute, 12.6f, 13));
  }
  {
    Rig r; r.hold(30, 13); r.state = Cooling; r.tick(13);
    r.hold(120, 13); r.state = Idle; r.timed = true; r.tick(13);
    r.ramp(90, 12.85f); r.state = Cooling; r.timed = false; r.tick(12.85f);
    assert(r.c.drop() > 0 && r.c.riseSamples() == 1);
    r.hold(120, 12.85f);
    assert(r.c.riseSamples() == 1); // A restart does not contaminate startup-rise learning.
    assert(r.c.center() == 0); // A timed pulse is not a complete thermal cycle.
  }
  {
    Rig r; r.cycle(); r.tick(20);
    assert(!r.c.active() && r.c.drop() == 0); // Intervention discards confidence.
    r.tick(NAN); assert(!r.c.active());
  }
  {
    Rig r; r.hold(30, 13);
    // Repeated sustained post-start rise is needed to advance a start.
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
      r.ramp(180, 13.2f); r.state = Cooling; r.tick(13.2f);
      r.ramp(60, 13.2625f); r.hold(45, 13.2625f);
      r.ramp(60, 13.1375f); r.hold(45, 13.1375f);
      r.state = Idle; r.tick(13.1375f); r.hold(60, 13.1375f);
    }
    assert(r.c.riseSamples() == 3 && r.c.rise() > .03f);
    assert(r.c.onThreshold(13.3f, 12.7f) < 13.3f);
    assert(r.c.offThreshold(12.8f, 13.3f) >= 12.8f);
  }
  {
    Rig r; r.now = UINT32_MAX - 30000; r.hold(30, 13); r.ramp(180, 13.25f);
    assert(r.c.trendKnown() && r.c.velocity() > 0); // Clock rollover.
  }
  {
    Rig r; r.cycle(); r.c.coolingStopped(r.now, true);
    assert(r.c.drop() > 0 && !r.c.allowHeat(r.now, 12.6f, 13));
    r.simulated = true; r.tick(13);
    assert(!r.c.active() && r.c.drop() == 0 && r.c.center() == 0);
    assert(r.c.allowHeat(r.now, 12.6f, 13)); // Debug clears an old recovery gate.
    assert(r.c.onThreshold(13.3f, 12.7f) == 13.3f);
    assert(r.c.offThreshold(12.8f, 13.3f) == 12.8f);
    for (unsigned i = 0; i < 10; ++i) {
      r.state = i & 1 ? Cooling : Idle; r.hold(30, i & 1 ? 12.5f : 13.5f);
    }
    assert(r.c.dropSamples() == 0 && r.c.riseSamples() == 0);
    r.simulated = false; r.tick(13);
    assert(r.c.active() && r.c.dropSamples() == 0 && r.c.riseSamples() == 0);
  }
  puts("Adaptive temperature: cutoffs, learning, noise, state changes, recovery and rollover passed.");
}
