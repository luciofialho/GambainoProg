#include <Arduino.h>
#include <atomic>
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include <IOTK_ESPAsyncServer.h>
#include <IOTK_SimpleMail.h>
#include <freertos/semphr.h>
#include "GambainoCommon.h"
#include "HealthProtocol.h"
#include "Sidekick-health.h"

unsigned long NTPEpoch();
extern TaskHandle_t logSendTaskHandle;

namespace {
struct Stored {
  uint32_t magic = 0x48573101;
  uint8_t mac[6] = {};
  uint8_t number = 0, mode = 0;
  uint16_t batch = 0;
  uint32_t boot = 0;
  bool known = false, incident = false, notified = false;
};
struct Slot {
  Stored saved;
  HealthWatch watch;
  uint32_t seq = 0, generation = 0, lastMail = 0, lastAttempt = 0;
  uint32_t lastEpoch = 0, storeAttempt = 0;
  bool storeFailed = false;
  bool attempted = false, mailed = false, dirty = false;
};
Slot slots[MAXFMTS];
SemaphoreHandle_t mutex = nullptr;
uint32_t challenge = 0, ledSend = 0, clockEpoch = 0, clockAt = 0;
std::atomic<uint32_t> ledSeq{0}, ledAck{0};
unsigned ledMask = ~0U;
bool storageOk = true, storageLoaded = false;
const char *mailResult = "not attempted";
const char *modeName(unsigned mode) {
  static const char *names[] = {"Off", "Brewing/Transfering", "Fermenting", "Conditioning", "Manual"};
  return mode < 5 ? names[mode] : "Unknown";
}
bool paired(const uint8_t *mac) {
  uint8_t zero[6] = {};
  if (!mac || !memcmp(mac, zero, 6)) return false;
  for (const auto &p : peerPovotos) if (!memcmp(p.mac, mac, 6)) return true;
  return false;
}
void transition(Slot &s, bool before) {
  if (s.watch.incident != before) {
    ++s.generation;
    s.attempted = s.mailed = false;
    s.saved.incident = s.watch.incident;
    s.dirty = true;
    if (logSendTaskHandle) xTaskNotifyGive(logSendTaskHandle);
  }
}
bool persist(Slot &s) {
  if (!s.dirty) return true;
  const uint32_t now = millis();
  if (s.storeFailed && uint32_t(now - s.storeAttempt) < 5000) return false;
  s.storeAttempt = now;
  Preferences prefs;
  char key[8]; snprintf(key, sizeof(key), "p%u", unsigned(&s - slots));
  bool ok = prefs.begin("sk_health", false) &&
      prefs.putBytes(key, &s.saved, sizeof(s.saved)) == sizeof(s.saved);
  prefs.end();
  s.dirty = s.storeFailed = !ok;
  storageOk = ok;
  return ok;
}
}
void healthBegin() {
  mutex = xSemaphoreCreateMutex();
  challenge = esp_random();
  if (!challenge) challenge = 1;
  Preferences prefs;
  if (!prefs.begin("sk_health", false)) { storageOk = false; return; }
  bool validStorage = true;
  for (unsigned i = 0; i < MAXFMTS; ++i) {
    char key[8]; snprintf(key, sizeof(key), "p%u", i);
    Stored saved;
    if (!prefs.isKey(key)) continue;
    if (prefs.getBytesLength(key) == sizeof(saved) &&
        prefs.getBytes(key, &saved, sizeof(saved)) == sizeof(saved) &&
        saved.magic == 0x48573101 && saved.mode <= 4 && saved.number <= 99) {
      slots[i].saved = saved;
      slots[i].watch.known = saved.known;
      slots[i].watch.mode = saved.mode;
      slots[i].watch.incident = saved.incident;
      // A full grace period after boot, even if an incident was open.
      slots[i].watch.lastSeen = millis();
    } else validStorage = false;
  }
  prefs.end();
  storageOk = storageLoaded = validStorage;
}
void healthReceive(const char *payload, const uint8_t *mac) {
  HealthPacket packet;
  if (!mutex || !paired(mac) || !parseHealth(payload, packet)) return;
  // A boot-specific receiver challenge prevents buffered packets after restart
  // from being mistaken for live data. Never acknowledge before durable mode storage.
  if (packet.challenge != challenge) {
    packet.challenge = challenge;
    char reply[100]; formatHealth(reply, sizeof(reply), packet);
    sendEspNow(mac, 0, false, HEALTH_ACK_PACKET, reply);
    return;
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  Slot *slot = nullptr;
  for (auto &s : slots) if (s.saved.known && !memcmp(s.saved.mac, mac, 6)) { slot = &s; break; }
  if (!slot) for (auto &s : slots) if (!s.saved.known) { slot = &s; break; }
  if (!slot) { xSemaphoreGive(mutex); return; }
  auto &s = *slot;
  if (s.saved.known && !healthFresh(s.saved.boot, s.seq, packet.boot, packet.seq)) {
    xSemaphoreGive(mutex); return;
  }
  const bool before = s.watch.incident;
  const bool changed = !s.saved.known || s.saved.mode != packet.mode ||
      s.saved.batch != packet.batch || s.saved.number != packet.number || s.saved.boot != packet.boot;
  s.watch.receive(packet.mode, millis());
  s.seq = packet.seq;
  s.lastEpoch = clockEpoch ? clockEpoch + uint32_t(millis() - clockAt) / 1000 : 0;
  if (changed) {
    ++s.generation;
    s.saved.known = true;
    memcpy(s.saved.mac, mac, 6);
    s.saved.number = packet.number; s.saved.mode = packet.mode;
    s.saved.batch = packet.batch; s.saved.boot = packet.boot;
    s.dirty = true;
  }
  transition(s, before);
  // Persist only state changes, never every heartbeat.
  bool durable = persist(s) && storageLoaded;
  xSemaphoreGive(mutex);
  if (durable) {
    char reply[100]; formatHealth(reply, sizeof(reply), packet);
    sendEspNow(mac, 0, false, HEALTH_ACK_PACKET, reply);
  }
}
void healthProcess() {
  if (!mutex) return;
  const uint32_t now = millis();
  unsigned mask = 0;
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (unsigned i = 0; i < MAXFMTS; ++i) {
    auto &s = slots[i];
    bool before = s.watch.incident;
    s.watch.tick(now);
    transition(s, before);
    if (s.watch.incident || (s.saved.incident && healthArmed(s.saved.mode))) mask |= 1U << i;
    persist(s);
  }
  xSemaphoreGive(mutex);
  if (storageLoaded && (mask != ledMask || uint32_t(now - ledSend) >= (ledAck == ledSeq ? 60000UL : 5000UL))) {
    ledMask = mask; ledSend = now; ++ledSeq;
    sendSerial2(RCCOMMANDPACKET, "HEALTH1,%lu,%lu,%u", (unsigned long)challenge,
                (unsigned long)ledSeq, mask);
  }
}
void healthSerial(const char *payload) {
  unsigned long boot, seq; int end = 0;
  if (sscanf(payload, "HEALTHACK1,%lu,%lu%n", &boot, &seq, &end) == 2 &&
      end && !payload[end] && boot == challenge && seq == ledSeq) ledAck = seq;
}
void healthSendMail() {
  if (!mutex) return;
  // NTPEpoch may perform network I/O: only LogSend calls it, never the receiver.
  const uint32_t epoch = NTPEpoch();
  if (epoch) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    clockEpoch = epoch; clockAt = millis();
    xSemaphoreGive(mutex);
  }
  for (auto &s : slots) {
    const uint32_t now = millis();
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool alarm = s.watch.incident;
    if (!s.seq && uint32_t(now - s.watch.lastSeen) < HEALTH_TIMEOUT_MS) {
      xSemaphoreGive(mutex); continue;
    }
    const bool recovery = !alarm && s.saved.notified && !s.saved.incident;
    if ((!alarm && !recovery) || (s.attempted && uint32_t(now - s.lastAttempt) < 60000) ||
        (alarm && s.mailed && uint32_t(now - s.lastMail) < HEALTH_REMINDER_MS)) {
      xSemaphoreGive(mutex); continue;
    }
    const uint32_t generation = s.generation;
    char subject[120], body[500], lastTime[32] = "unknown";
    if (s.lastEpoch) {
      time_t epoch = s.lastEpoch;
      struct tm parts;
      gmtime_r(&epoch, &parts); // IOTK epoch already includes the configured local offset.
      strftime(lastTime, sizeof(lastTime), "%Y-%m-%d %H:%M:%S", &parts);
    }
    snprintf(subject, sizeof(subject), "Povoto %u: %s", s.saved.number,
             alarm ? "communication lost" : "monitoring incident closed");
    snprintf(body, sizeof(body),
        "Povoto: %u\r\nBatch: %u\r\nLast known mode: %s\r\nLast heartbeat (SideKick local time): %s\r\n"
        "Time since last heartbeat (or SideKick startup): %lu s\r\n%s\r\n",
        s.saved.number, s.saved.batch, modeName(s.saved.mode), lastTime,
        (unsigned long)(uint32_t(now - s.watch.lastSeen) / 1000),
        alarm ? "Possible freeze or unavailable device. Please inspect the equipment."
              : "Communication is stable again, or the confirmed mode no longer requires monitoring.");
    s.attempted = true; s.lastAttempt = now;
    xSemaphoreGive(mutex);
    if (WiFi.status() != WL_CONNECTED || ESP.getMaxAllocHeap() < 45000) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      mailResult = "waiting for WiFi / memory";
      xSemaphoreGive(mutex); return;
    }
    // Revalidate immediately before SMTP. An in-flight SMTP transaction cannot be recalled.
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool current = s.generation == generation;
    xSemaphoreGive(mutex);
    if (!current) continue;
    bool sent = sendSimpleMail(String(subject), String(body), false);
    xSemaphoreTake(mutex, portMAX_DELAY);
    mailResult = sent ? "accepted by SMTP server" : "failed; retry pending";
    if (sent) {
      // Even if recovery arrived during SMTP, remember that a recovery mail is owed.
      s.saved.notified = alarm;
      s.dirty = true;
      if (s.generation == generation) { s.mailed = true; s.lastMail = millis(); }
    }
    xSemaphoreGive(mutex);
    return; // One attempt per LogSend pass; no blocking retry delays.
  }
}
void healthStatus(char *out, size_t size) {
  if (!mutex) { strlcat(out, "<br>Povoto monitoring unavailable<br>", size); return; }
  xSemaphoreTake(mutex, portMAX_DELAY);
  char line[220];
  bool healthyStorage = storageOk && storageLoaded;
  for (const auto &s : slots) if (s.storeFailed) healthyStorage = false;
  snprintf(line, sizeof(line), "<br><b>Povoto monitoring</b>: 60 s heartbeat / 300 s timeout; storage %s; mail %s; BrewCore %s<br>",
      healthyStorage ? "OK" : "FAILED", mailResult, ledSeq && ledAck == ledSeq ? "confirmed" : "confirmation pending");
  strlcat(out, line, size);
  for (const auto &s : slots) if (s.saved.known) {
    snprintf(line, sizeof(line), "Slot %u, Povoto %u: mode %s, batch %u, age %lu s, %s<br>",
        unsigned(&s - slots), s.saved.number, modeName(s.saved.mode), s.saved.batch,
        (unsigned long)(uint32_t(millis() - s.watch.lastSeen) / 1000),
        s.watch.incident ? "ALERT" : !s.seq ? "awaiting live state" :
        healthArmed(s.watch.mode) ? "watching" : "disarmed");
    strlcat(out, line, size);
  }
  for (const auto &p : peerPovotos) {
    uint8_t zero[6] = {}; if (!memcmp(p.mac, zero, 6)) continue;
    bool known = false;
    for (const auto &s : slots) if (s.saved.known && !memcmp(s.saved.mac, p.mac, 6)) known = true;
    if (!known) {
      snprintf(line, sizeof(line), "Peer %02X:%02X:%02X:%02X:%02X:%02X: unknown state, NOT monitored<br>",
          p.mac[0], p.mac[1], p.mac[2], p.mac[3], p.mac[4], p.mac[5]);
      strlcat(out, line, size);
    }
  }
  xSemaphoreGive(mutex);
}
