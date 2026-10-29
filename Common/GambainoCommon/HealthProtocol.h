#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if __cplusplus >= 201402L
#define HEALTH_CONSTEXPR constexpr
#else
#define HEALTH_CONSTEXPR
#endif


constexpr uint32_t HEALTH_PERIOD_MS = 60000;
constexpr uint32_t HEALTH_TIMEOUT_MS = 300000;
constexpr uint32_t HEALTH_REMINDER_MS = 900000;
constexpr char HEALTH_PACKET = 'J';
constexpr char HEALTH_ACK_PACKET = 'K';
// Serial2 uses the existing CRC-protected command envelope, with distinct verbs.
constexpr char HEALTH_LED_PREFIX[] = "HEALTH1,";
struct HealthPacket {
  unsigned long boot = 0, seq = 0, challenge = 0;
  unsigned number = 0, mode = 0, batch = 0;
};
HEALTH_CONSTEXPR inline bool parseHealth(const char *s, HealthPacket &p) {
  if (!s) return false;
  uint32_t fields[7] = {};
  for (unsigned i = 0; i < 7; ++i) {
    if (*s < '0' || *s > '9') return false;
    do {
      const unsigned digit = unsigned(*s++ - '0');
      if (fields[i] > (UINT32_MAX - digit) / 10) return false;
      fields[i] = fields[i] * 10 + digit;
    } while (*s >= '0' && *s <= '9');
    if (i < 6 && *s++ != ',') return false;
  }
  if (*s || fields[0] != 1 || fields[1] > 99 || fields[2] > 4 ||
      fields[3] > 65535 || !fields[4] || !fields[5]) return false;
  p.number = fields[1]; p.mode = fields[2]; p.batch = fields[3];
  p.boot = fields[4]; p.seq = fields[5]; p.challenge = fields[6];
  return true;
}
inline void formatHealth(char *s, size_t n, const HealthPacket &p) {
  snprintf(s, n, "1,%u,%u,%u,%lu,%lu,%lu", p.number, p.mode, p.batch,
           p.boot, p.seq, p.challenge);
}
constexpr bool healthArmed(unsigned mode) { return mode == 2 || mode == 3; }
constexpr bool healthFresh(uint32_t oldBoot, uint32_t oldSeq, uint32_t boot, uint32_t seq) {
  return boot > oldBoot || (boot == oldBoot && seq > oldSeq);
}
// Pure state machine; all deadlines use unsigned elapsed times (millis rollover).
struct HealthWatch {
  uint32_t lastSeen = 0, recoveryStart = 0;
  bool known = false, incident = false, recovering = false;
  unsigned mode = 0;
  HEALTH_CONSTEXPR void receive(unsigned nextMode, uint32_t now) {
    if (incident && healthArmed(nextMode)) {
      if (!recovering || uint32_t(now - lastSeen) > 90000) {
        recovering = true;
        recoveryStart = now;
      } else if (uint32_t(now - recoveryStart) >= HEALTH_PERIOD_MS) {
        incident = false;
        recovering = false;
      }
    } else if (!healthArmed(nextMode)) {
      incident = false;
      recovering = false;
    }
    mode = nextMode;
    known = true;
    lastSeen = now;
  }
  HEALTH_CONSTEXPR void tick(uint32_t now) {
    if (known && healthArmed(mode) && uint32_t(now - lastSeen) >= HEALTH_TIMEOUT_MS) {
      incident = true;
      recovering = false;
    }
  }
};

#undef HEALTH_CONSTEXPR
