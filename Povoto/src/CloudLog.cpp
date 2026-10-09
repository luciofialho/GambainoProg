#include "CloudLog.h"
#include "CloudJson.h"
#include "CloudSync.h"

#include <Preferences.h>
#include <WiFi.h>
#include <IOTK_ESPAsyncServer.h>
#include <IOTK_NTP.h>
#include <math.h>
#include "GambainoCommon.h"
#include "GraphHistory.h"
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "PressureControl.h"
#include "TemperatureControl.h"

namespace {
constexpr char NVS_NAMESPACE[] = "pvt_cloud";
constexpr uint32_t CLOUD_SLOT_SECONDS = 300;
constexpr uint32_t MIN_VALID_EPOCH = 1577836800UL; // 2020-01-01
constexpr unsigned long RETRY_MS = 5000;
constexpr float SYNTHETIC_PROFILE_DAYS = 14.0f;

bool syntheticStored = false;
uint32_t syntheticStart = 0;
uint32_t lastSentSlot = 0;
uint32_t lastStateSlot = 0;
uint32_t lastSentEpoch = 0;
unsigned long sentCount = 0;
unsigned long sendErrors = 0;

bool isZeroMac(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i]) return false;
  }
  return true;
}

float setpointOrNan(float value) {
  return isfinite(value) && value != NOTaTEMP ? value : NAN;
}

uint32_t slotSeconds() {
  // A debug bench may log faster to stress the path (docs/cloud-log.md).
  const uint32_t logInterval = FMTData.dataLogIntervalSeconds;
  if (debugging && logInterval >= 30 && logInterval < CLOUD_SLOT_SECONDS) return logInterval;
  return CLOUD_SLOT_SECONDS;
}

// History record: spooled by the SideKick, one row per slot in the cloud.
bool buildRecord(uint32_t epoch, char *out, size_t size) {
  const bool synthetic = cloudSyntheticLogActive();
  GraphHistoryPoint point;
  if (synthetic) {
    const float day = epoch > syntheticStart ? float(epoch - syntheticStart) / 86400.0f : 0.0f;
    graphSyntheticPoint(fmodf(day, SYNTHETIC_PROFILE_DAYS), point);
    point.epoch = epoch;
  } else {
    point = graphCapturePoint(epoch);
  }

  JsonOut json(out, size);
  json.raw("{");
  json.integer("v", 1);
  json.integer("p", FMTData.PovotoNum);
  json.integer("e", (long)epoch);
  json.integer("m", SetPointData.mode);
  json.integer("b", BatchData.batchNumber);
  json.number("t", point.temperature, 2);
  json.number("ts", point.temperatureSetpoint, 2);
  json.number("tsl", setpointOrNan(SetPointData.setPointSlowTemp), 2);
  json.number("pr", point.pressure, 3);
  json.number("ps", point.pressureSetpoint, 3);
  json.number("psl", setpointOrNan(SetPointData.setPointSlowPressure), 3);
  json.number("sg", point.sg, 5);
  json.number("abv", point.abv, 2);
  json.number("r", point.co2Rate, 3);
  json.integer("f", (long)point.flags);
  json.number("vol", beerVolume, 1);
  json.number("co2", CO2Mass(), 0);
  json.number("rph", getReliefsPerHourValue(), 1);
  // Day 0 of the profile, so the cloud check can recompute every value.
  if (synthetic) json.integer("ss", (long)syntheticStart);
  json.raw("}");
  return json.ok;
}

// Batch state: only its latest version is kept, by the SideKick (RAM) and by
// the cloud (one row per batch), so it never takes space in the spool.
bool buildState(uint32_t epoch, char *out, size_t size) {
  const float og = cloudSyntheticLogActive() ? GRAPH_SYNTHETIC_OG : BatchData.batchOG;
  JsonOut json(out, size);
  json.raw("{");
  json.integer("v", 1);
  json.raw(",\"k\":\"s\"");
  json.integer("p", FMTData.PovotoNum);
  json.integer("e", (long)epoch);
  json.integer("b", BatchData.batchNumber);
  json.string("bn", BatchData.batchName);
  json.string("bd", BatchData.batchDate);
  json.number("og", isfinite(og) && og > 0.0f ? og : NAN, 5);
  json.integer("ct", CountersData.totalChillTime);
  json.integer("ht", CountersData.totalHeatTime);
  json.number("mh", headSpaceCO2Mols, 3);
  json.number("md", (float)CountersData.CO2InSolution, 3);
  json.number("me", (float)CountersData.totalMolsEjected, 3);
  json.integer("nx", (long)CountersData.totalReliefCount);
  json.number("dv", CountersData.dumpedVolume, 2);
  // Dashboard statistics: temperature mode and headspace volume (L).
  json.string("tm", ChillHeatMode == FMTCHILL ? "chill" : ChillHeatMode == FMTHEAT ? "heat" : "idle");
  json.number("hv", CountersData.headSpaceVolume, 2);
  // Phase 2: versions of the set point and rule snapshots, and whether the
  // Povoto accepts edits; the cloud asks for snapshots when its copy differs.
  char hash[9];
  cloudSetpointHash(hash, sizeof(hash));
  json.string("hs", hash);
  cloudRulesHash(hash, sizeof(hash));
  json.string("hr", hash);
  json.integer("ae", cloudEditsAccepted() ? 1 : 0);
  json.raw("}");
  return json.ok;
}
} // namespace

void cloudLogBegin() {
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, true)) return; // missing namespace: defaults
  syntheticStored = store.getBool("synth", false);
  syntheticStart = store.getULong("synthStart", 0);
  store.end();
}

bool cloudSyntheticLogStored() { return syntheticStored; }
bool cloudSyntheticLogActive() { return debugging && syntheticStored; }
uint32_t cloudSyntheticLogStart() { return syntheticStart; }

bool setCloudSyntheticLog(bool enabled) {
  if (enabled == syntheticStored) return true;
  uint32_t start = syntheticStart;
  if (enabled) {
    // Day 0 is the current slot, so the profile starts from the beginning.
    const uint32_t now = NTPEpoch();
    if (now < MIN_VALID_EPOCH) return false;
    start = now / slotSeconds() * slotSeconds();
  }
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = store.putBool("synth", enabled) == 1 &&
                     store.putULong("synthStart", start) == sizeof(uint32_t);
  store.end();
  if (!saved) return false;
  syntheticStored = enabled;
  syntheticStart = start;
  return true;
}

uint32_t cloudLogSlotSeconds() { return slotSeconds(); }
uint32_t cloudLogLastSentEpoch() { return lastSentEpoch; }
unsigned long cloudLogSentCount() { return sentCount; }
unsigned long cloudLogSendErrors() { return sendErrors; }

void maybeSendCloudLog() {
  static unsigned long lastAttempt = 0;
  if (BatchData.batchNumber == 0 || SetPointData.mode == MODE_OFF) return;
  if (WiFi.status() != WL_CONNECTED || isZeroMac(peerSideKick.mac)) return;
  if (lastAttempt && millis() - lastAttempt < RETRY_MS) return;
  const uint32_t now = NTPEpoch();
  if (now < MIN_VALID_EPOCH) return;
  const uint32_t seconds = slotSeconds();
  const uint32_t slot = now / seconds;
  if (slot == lastSentSlot && slot == lastStateSlot) return;

  // The record time is the start of the slot: one record per slot, and a
  // second one after a reboot is ignored by the cloud.
  const uint32_t epoch = slot * seconds;
  char payload[640];
  lastAttempt = millis();
  if (slot != lastSentSlot) {
    if (!buildRecord(epoch, payload, sizeof(payload))) {
      Serial.println("[CLOUD] Record does not fit the buffer");
      lastSentSlot = slot;
      ++sendErrors;
    }
    else {
      const esp_err_t err = sendEspNow(peerSideKick.mac, 0, false, (uint8_t)CLOUDLOGPACKET, payload);
      if (err != ESP_OK) {
        Serial.printf("[CLOUD] ESP-NOW send failed: %d\n", (int)err);
        ++sendErrors;
        return; // retried in RETRY_MS, same slot
      }
      lastSentSlot = slot;
      lastSentEpoch = epoch;
      ++sentCount;
    }
  }

  // Batch state, same slot: a lost one is replaced by the next slot's.
  if (slot != lastStateSlot) {
    if (!buildState(epoch, payload, sizeof(payload))) {
      Serial.println("[CLOUD] State does not fit the buffer");
      lastStateSlot = slot;
      ++sendErrors;
      return;
    }
    const esp_err_t err = sendEspNow(peerSideKick.mac, 0, false, (uint8_t)CLOUDSTATEPACKET, payload);
    if (err != ESP_OK) {
      Serial.printf("[CLOUD] ESP-NOW state send failed: %d\n", (int)err);
      ++sendErrors;
      return; // retried in RETRY_MS, same slot
    }
    lastStateSlot = slot;
  }
}
