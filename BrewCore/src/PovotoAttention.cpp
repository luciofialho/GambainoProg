#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "GambainoCommon.h"
#include "Variables.h"
#include "BrewCoreCommon.h"
#include "PovotoAttention.h"

namespace {
std::atomic<unsigned> remoteMask{0};
std::atomic<uint32_t> lastRemote{0};
std::atomic<bool> received{false};
portMUX_TYPE localMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t localUntil = 0;
bool localActive = false;
uint32_t remoteBoot = 0, remoteSeq = 0;
std::atomic<bool> stored{true};
}
void povotoAttentionBegin() {
  Preferences prefs;
  if (prefs.begin("pvt_attention", false)) {
    remoteMask = prefs.getUInt("mask", 0);
    prefs.end();
  } else stored = false;
}
bool povotoAttentionReceive(const char *payload) {
  if (strncmp(payload, "HEALTH1,", 8)) return false;
  unsigned long boot, seq; unsigned mask; int end = 0;
  if (sscanf(payload, "HEALTH1,%lu,%lu,%u%n", &boot, &seq, &mask, &end) != 3 ||
      !end || payload[end] || !boot || !seq || mask >= (1U << MAXFMTS)) return true;
  if (boot == remoteBoot && seq < remoteSeq) return true;
  if (mask != remoteMask.load() || !stored) {
    Preferences prefs;
    stored = prefs.begin("pvt_attention", false) && prefs.putUInt("mask", mask) == sizeof(uint32_t);
    prefs.end();
  }
  remoteMask = mask; remoteBoot = boot; remoteSeq = seq;
  lastRemote = millis(); received = true;
  if (stored) sendSerial2(RCCOMMANDPACKET, "HEALTHACK1,%lu,%lu", boot, seq);
  return true;
}
void requestAttentionLed(uint32_t durationMs, bool onlyIfIdle) {
  const uint32_t until = millis() + durationMs;
  portENTER_CRITICAL(&localMux);
  if (onlyIfIdle && localActive && (int32_t)(localUntil - millis()) > 0) {
    portEXIT_CRITICAL(&localMux);
    return;
  }
  if (!localActive || (int32_t)(until - localUntil) > 0) localUntil = until;
  localActive = true;
  portEXIT_CRITICAL(&localMux);
}
void attentionLedProcess() {
  portENTER_CRITICAL(&localMux);
  if (localActive && (int32_t)(millis() - localUntil) >= 0) localActive = false;
  const bool local = localActive;
  portEXIT_CRITICAL(&localMux);
  // Only this function writes Led2: RF timers can no longer clear a remote alarm.
  Led2 = (local || remoteMask.load()) ? ON : OFF;
}
void povotoAttentionStatus(char *out, size_t size) {
  char line[180];
  snprintf(line, sizeof(line), "<br>Povoto attention: %s (slot mask %u); SideKick %s; storage %s<br>",
      remoteMask.load() ? "ACTIVE" : "inactive", remoteMask.load(),
      !received.load() ? "not yet received" : uint32_t(millis() - lastRemote.load()) >= 300000 ? "STALE" : "connected",
      stored ? "OK" : "FAILED");
  strlcat(out, line, size);
}
