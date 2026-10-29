#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "GambainoCommon.h"
#include "HealthProtocol.h"
#include "PovotoData.h"
#include "PovotoHealth.h"

namespace {
HealthPacket packet;
portMUX_TYPE ackMux = portMUX_INITIALIZER_UNLOCKED;
HealthPacket receivedAck;
unsigned long challenge = 0;
bool confirmed = false;
std::atomic<bool> publishedConfirmed{false}, available{false};
std::atomic<uint32_t> publishedState{0};
uint32_t lastSend = 0;
bool initialized = false;
}
void povotoHealthAck(const char *payload, const uint8_t *mac) {
  if (!mac || memcmp(mac, peerSideKick.mac, 6)) return;
  HealthPacket ack;
  if (!parseHealth(payload, ack)) return;
  portENTER_CRITICAL(&ackMux);
  receivedAck = ack;
  portEXIT_CRITICAL(&ackMux);
}
void povotoHealthProcess() {
  if (!initialized) {
    initialized = true;
    Preferences prefs;
    if (prefs.begin("pvt_health", false)) {
      packet.boot = prefs.getULong("boot", 0) + 1;
      available = packet.boot && prefs.putULong("boot", packet.boot) == sizeof(uint32_t);
      prefs.end();
    }
    if (!available) Serial.println("[HEALTH] Boot counter unavailable; monitoring unavailable");
  }
  if (!available) return;
  HealthPacket ack;
  portENTER_CRITICAL(&ackMux);
  ack = receivedAck;
  portEXIT_CRITICAL(&ackMux);
  if (packet.seq && ack.boot == packet.boot && ack.seq == packet.seq &&
      ack.mode == packet.mode && ack.batch == packet.batch && ack.number == packet.number) {
    challenge = ack.challenge;
    confirmed = packet.challenge && packet.challenge == challenge;
  }
  const uint32_t now = millis();
  const bool changed = !packet.seq || packet.mode != SetPointData.mode ||
      packet.batch != BatchData.batchNumber || packet.number != FMTData.PovotoNum;
  publishedConfirmed = confirmed && !changed;
  if (!changed && uint32_t(now - lastSend) < (confirmed ? HEALTH_PERIOD_MS : 5000UL)) return;
  uint8_t zero[6] = {};
  if (!memcmp(peerSideKick.mac, zero, 6)) return;
  packet.number = FMTData.PovotoNum;
  packet.mode = SetPointData.mode;
  packet.batch = BatchData.batchNumber;
  packet.challenge = challenge;
  confirmed = false;
  publishedConfirmed = false;
  publishedState = (packet.mode << 24) | (packet.number << 16) | packet.batch;
  ++packet.seq;
  char payload[100];
  formatHealth(payload, sizeof(payload), packet);
  lastSend = now;
  sendEspNow(peerSideKick.mac, 0, false, HEALTH_PACKET, payload);
}
void povotoHealthStatus(char *out, size_t size) {
  const bool confirmed = publishedConfirmed.load() && publishedState.load() ==
      ((uint32_t(SetPointData.mode) << 24) | (uint32_t(FMTData.PovotoNum) << 16) | BatchData.batchNumber);
  snprintf(out, size, "SideKick: %s (60/300 s).<br>",
           !available ? "unavailable" : confirmed ? "state confirmed" : "state confirmation pending");
}
