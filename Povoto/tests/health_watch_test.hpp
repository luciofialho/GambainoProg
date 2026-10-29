// Compile-time tests of the firmware's actual state machine (C++14).
// No host runtime or board is required; see docs/sidekick-health.md.
#include <initializer_list>
#include "HealthProtocol.h"
constexpr bool timeoutBoundary(unsigned mode) {
  HealthWatch w;
  w.receive(mode, 1234);
  w.tick(301233);
  if (w.incident) return false;
  w.tick(301234);
  return w.incident;
}
static_assert(timeoutBoundary(2), "Fermenting: alarm at exactly five minutes");
static_assert(timeoutBoundary(3), "Conditioning: alarm at exactly five minutes");
constexpr bool inactiveModes() {
  for (unsigned mode : {0U, 1U, 4U}) {
    HealthWatch w;
    w.receive(mode, 0);
    w.tick(999999);
    if (w.incident) return false;
  }
  HealthWatch unknown;
  unknown.tick(999999);
  return !unknown.incident;
}
static_assert(inactiveModes(), "Off, transfer, manual and unknown do not arm");
constexpr bool maintenance() {
  HealthWatch w;
  w.receive(2, 0);
  w.tick(299999);
  w.receive(2, 299999);
  w.tick(599998);
  return !w.incident;
}
static_assert(maintenance(), "Maintenance below five minutes has no incident");
constexpr bool recovery() {
  HealthWatch w;
  w.receive(2, 0); w.tick(300000);
  w.receive(2, 310000);
  if (!w.incident) return false;
  w.receive(2, 369999);
  if (!w.incident) return false;
  w.receive(2, 370000);
  return !w.incident;
}
static_assert(recovery(), "Recovery needs one minute of continuing heartbeat progress");
constexpr bool intermittent() {
  HealthWatch w;
  w.receive(2, 0); w.tick(300000);
  w.receive(2, 310000); w.receive(2, 500000);
  if (!w.incident) return false;
  w.receive(0, 500001);
  return !w.incident && !healthArmed(w.mode);
}
static_assert(intermittent(), "Isolated packets do not recover; confirmed Off disarms");
constexpr bool rollover() {
  HealthWatch w;
  w.receive(3, UINT32_MAX - 1000);
  w.tick(298998);
  if (w.incident) return false;
  w.tick(298999);
  return w.incident;
}
static_assert(rollover(), "Timeout survives millis wrap");
constexpr bool restart() {
  HealthWatch w;
  w.known = true; w.mode = 2; w.lastSeen = 2000;
  w.tick(301999);
  if (w.incident) return false;
  w.tick(302000);
  return w.incident;
}
static_assert(restart(), "Restored monitoring gets a full five-minute grace");
static_assert(!healthFresh(10, 20, 10, 20), "Duplicate is not a heartbeat");
static_assert(!healthFresh(10, 20, 10, 19), "Reordered sequence is not a heartbeat");
static_assert(!healthFresh(10, 20, 9, 99999), "Retired boot cannot change mode");
static_assert(healthFresh(10, 20, 11, 1), "New persisted boot starts a fresh sequence");
constexpr bool independent() {
  HealthWatch a, b;
  a.receive(2, 0); b.receive(3, 0);
  a.tick(300000); b.tick(300000);
  a.receive(0, 300001);
  return !a.incident && b.incident;
}
static_assert(independent(), "Recovering one fermenter does not clear another");

constexpr bool parser() {
  HealthPacket p;
  return parseHealth("1,99,3,65535,10,20,30", p) && p.number == 99 && p.mode == 3 &&
      p.batch == 65535 && p.boot == 10 && p.seq == 20 && p.challenge == 30 &&
      !parseHealth("1,99,3,65535,10,20,30garbage", p) &&
      !parseHealth("1,100,3,1,10,20,30", p) &&
      !parseHealth("1,1,5,1,10,20,30", p) &&
      !parseHealth("1,1,2,1,-1,20,30", p) &&
      !parseHealth("1,1,2,1,4294967296,20,30", p) &&
      !parseHealth("1,1,2,1,10,0,30", p) &&
      !parseHealth("1,1,2,1,10,20", p) && !parseHealth("", p);
}
static_assert(parser(), "Reject malformed, overflowing and out-of-range packets");
