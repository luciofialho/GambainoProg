#include "CloudSync.h"

#include <Preferences.h>
#include <WiFi.h>
#include <IOTK_ESPAsyncServer.h>
#include <IOTK_NTP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#include "AutoSetpoints.h"
#include "CloudJson.h"
#include "GambainoCommon.h"
#include "PovotoCommon.h"
#include "PovotoData.h"

namespace {
constexpr char NVS_NAMESPACE[] = "pvt_cloud";
constexpr size_t REQUEST_MAX_BYTES = 1024;
constexpr size_t SNAPSHOT_MAX_BYTES = 640;
constexpr unsigned long HASH_CHECK_MS = 2000;
// One snapshot line per pass: the SideKick receive queue holds 16 ESP-NOW
// frames, and the 8 rules would otherwise arrive together.
constexpr unsigned long SNAPSHOT_GAP_MS = 300;
constexpr unsigned long ACK_RETRY_MS = 5000;
constexpr int ACK_TRIES = 6;

bool editsAccepted = true;

struct Request {
  char json[REQUEST_MAX_BYTES];
};
Request requestStorage[2];
StaticQueue_t requestQueueControl;
QueueHandle_t requestQueue = nullptr;

unsigned long applied = 0;
unsigned long rejected = 0;
unsigned long foreign = 0;

// Snapshots already sent (hash), and what is still to send.
uint32_t sentSetpointHash = 0;
uint32_t sentRulesHash = 0;
bool setpointPending = false;
uint16_t rulesPending = 0;       // bit per rule
uint32_t pendingRulesHash = 0;   // hash the pending rule lines carry
unsigned long lastSnapshotMs = 0;

// Last answered requests: a repeated id gets the same answer, not a second apply.
struct Answer {
  long id;
  bool ok;
  char message[64];
  int triesLeft;
  unsigned long lastTryMs;
};
Answer answers[4];
int nextAnswer = 0;

bool isZeroMac(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i]) return false;
  }
  return true;
}

uint32_t crc32(const char *text, uint32_t crc = 0) {
  crc = ~crc;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    crc ^= *p;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}

float setpointValue(float value) {
  return isfinite(value) && value != NOTaTEMP ? value : NAN;
}

// Set points as the cloud shows them. While a ramp runs, its direct value moves
// every step, so it stays out (the target and speed define the change).
bool buildSetpointData(char *out, size_t size) {
  const bool tempRamp = SetPointData.setPointSlowTemp != NOTaTEMP;
  const bool pressRamp = SetPointData.setPointSlowPressure != NOTaTEMP;
  JsonOut json(out, size);
  json.raw("{");
  json.number("t", tempRamp ? NAN : setpointValue(SetPointData.setPointTemp), 2);
  json.number("ts", setpointValue(SetPointData.setPointSlowTemp), 2);
  json.number("tv", SetPointData.setPointSlowTempSpeed, 2);
  json.number("p", pressRamp ? NAN : setpointValue(SetPointData.setPointPressure), 3);
  json.number("ps", setpointValue(SetPointData.setPointSlowPressure), 3);
  json.number("pv", SetPointData.setPointSlowPressureSpeed, 3);
  json.raw("}");
  return json.ok;
}

bool buildRuleData(int index, const AutoSetpointRule_t &rule, uint32_t triggeredAt, char *out, size_t size) {
  JsonOut json(out, size);
  json.raw("{");
  json.integer("i", index);
  json.string("n", rule.name);
  json.number("sh", rule.stableHours, 1);
  json.number("ph", rule.pressureStableHours, 1);
  json.number("sg", rule.sgBelow, 3);
  json.number("co", rule.co2RateBelow, 1);
  json.number("t", rule.temperature, 2);
  json.number("tsl", rule.temperatureSlow, 2);
  json.number("p", rule.pressure, 3);
  json.number("psl", rule.pressureSlow, 3);
  json.integer("rq", rule.requiresPrevious ? 1 : 0);
  json.integer("mo", rule.manualOnly ? 1 : 0);
  json.unsignedInteger("at", triggeredAt);
  json.raw("}");
  return json.ok;
}

uint32_t currentSetpointHash() {
  char data[SNAPSHOT_MAX_BYTES];
  return buildSetpointData(data, sizeof(data)) ? crc32(data) : 0;
}

// Hash of the 8 rule texts in order, each followed by a newline.
uint32_t currentRulesHash() {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);
  uint32_t crc = 0;
  char data[SNAPSHOT_MAX_BYTES];
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; ++i) {
    if (!buildRuleData(i, rules[i], status.triggeredAt[i], data, sizeof(data))) return 0;
    crc = crc32(data, crc);
    crc = crc32("\n", crc);
  }
  return crc;
}

void hashText(uint32_t hash, char *out, size_t size) {
  snprintf(out, size, "%08lx", (unsigned long)hash);
}

bool linkReady() {
  return WiFi.status() == WL_CONNECTED && !isZeroMac(peerSideKick.mac) && NTPEpoch() != 0;
}

// Through the SideKick spool, like the history records.
bool sendLine(const char *line) {
  return sendEspNow(peerSideKick.mac, 0, false, (uint8_t)CLOUDLOGPACKET, line) == ESP_OK;
}

bool sendSetpointSnapshot() {
  char data[SNAPSHOT_MAX_BYTES];
  if (!buildSetpointData(data, sizeof(data))) return true; // cannot happen; do not retry forever
  char hash[9];
  hashText(crc32(data), hash, sizeof(hash));
  char line[SNAPSHOT_MAX_BYTES + 96];
  snprintf(line, sizeof(line), "{\"v\":1,\"k\":\"sp\",\"p\":%d,\"e\":%lu,\"h\":\"%s\",\"d\":%s}",
           FMTData.PovotoNum, (unsigned long)NTPEpoch(), hash, data);
  return sendLine(line);
}

bool sendRuleSnapshot(int index, uint32_t rulesHash) {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);
  char data[SNAPSHOT_MAX_BYTES];
  if (!buildRuleData(index, rules[index], status.triggeredAt[index], data, sizeof(data))) return true;
  char hash[9];
  hashText(rulesHash, hash, sizeof(hash));
  char line[SNAPSHOT_MAX_BYTES + 96];
  snprintf(line, sizeof(line), "{\"v\":1,\"k\":\"r\",\"p\":%d,\"e\":%lu,\"h\":\"%s\",\"i\":%d,\"d\":%s}",
           FMTData.PovotoNum, (unsigned long)NTPEpoch(), hash, index, data);
  return sendLine(line);
}

bool sendAnswer(const Answer &answer) {
  char line[160];
  JsonOut out(line, sizeof(line));
  out.raw("{");
  out.integer("v", 1);
  out.raw(",\"k\":\"ack\"");
  out.integer("p", FMTData.PovotoNum);
  out.integer("id", answer.id);
  out.integer("ok", answer.ok ? 1 : 0);
  out.string("m", answer.message);
  out.raw("}");
  return out.ok && sendLine(line);
}

void answer(long id, bool ok, const char *message) {
  Answer &slot = answers[nextAnswer];
  nextAnswer = (nextAnswer + 1) % 4;
  slot.id = id;
  slot.ok = ok;
  strlcpy(slot.message, message, sizeof(slot.message));
  slot.triesLeft = ACK_TRIES;
  slot.lastTryMs = 0;
  if (ok) ++applied;
  else ++rejected;
  Serial.printf("[CLOUD] request %ld %s%s%s\n", id, ok ? "applied" : "rejected", message[0] ? ": " : "", message);
}

// Range of the set point page and of the rules (docs/automatic-actions.md).
bool inRange(float value, float low, float high, bool optional) {
  if (isnan(value)) return optional;
  return value >= low && value <= high;
}

const char *applySetpoints(const char *data) {
  float t, ts, tv, p, ps, pv;
  if (!jsonNumber(data, "t", t) || !jsonNumber(data, "ts", ts) || !jsonNumber(data, "tv", tv) ||
      !jsonNumber(data, "p", p) || !jsonNumber(data, "ps", ps) || !jsonNumber(data, "pv", pv))
    return "invalid set point values";
  if (!inRange(t, 0.0f, 42.0f, true) || !inRange(ts, 0.0f, 42.0f, true)) return "temperature must be 0 to 42";
  if (!inRange(p, 0.0f, 2.0f, false) || !inRange(ps, 0.0f, 2.0f, true)) return "pressure must be 0 to 2 bar";
  if (!inRange(tv, 1.0f, 8.0f, true)) return "temperature ramp speed must be 1 to 8 degrees/day";
  if (!inRange(pv, 0.1f, 2.0f, true)) return "pressure ramp speed must be 0.1 to 2 bar/day";
  // As the set point page: each field replaces the stored one; a speed left
  // out keeps the current speed.
  SetPointData.setPointTemp = isnan(t) ? NOTaTEMP : t;
  SetPointData.setPointSlowTemp = isnan(ts) ? NOTaTEMP : ts;
  if (!isnan(tv)) SetPointData.setPointSlowTempSpeed = tv;
  SetPointData.setPointPressure = p;
  SetPointData.setPointSlowPressure = isnan(ps) ? NOTaTEMP : ps;
  if (!isnan(pv)) SetPointData.setPointSlowPressureSpeed = pv;
  return writeSetPointDataToNIV() ? "" : "could not save the set points";
}

bool ruleIndex(const char *data, int &index) {
  long value;
  if (!jsonInteger(data, "i", value) || value < 0 || value >= AUTO_SETPOINT_RULE_COUNT) return false;
  index = (int)value;
  return true;
}

const char *applyRule(const char *data, char *message, size_t messageSize) {
  int index;
  if (!ruleIndex(data, index)) return "invalid rule number";
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);
  if (status.triggeredAt[index]) return "rule already triggered: reset it first";
  AutoSetpointRule_t rule = {};
  char name[AUTO_SETPOINT_NAME_SIZE * 2];
  long requiresPrevious = 0, manualOnly = 0;
  if (!jsonString(data, "n", name, sizeof(name))) name[0] = '\0';
  // The rule struct is packed: no references to its fields.
  float sh, ph, sg, co, t, tsl, p, psl;
  if (!jsonNumber(data, "sh", sh) || !jsonNumber(data, "ph", ph) || !jsonNumber(data, "sg", sg) ||
      !jsonNumber(data, "co", co) || !jsonNumber(data, "t", t) || !jsonNumber(data, "tsl", tsl) ||
      !jsonNumber(data, "p", p) || !jsonNumber(data, "psl", psl))
    return "invalid rule values";
  rule.stableHours = sh;
  rule.pressureStableHours = ph;
  rule.sgBelow = sg;
  rule.co2RateBelow = co;
  rule.temperature = t;
  rule.temperatureSlow = tsl;
  rule.pressure = p;
  rule.pressureSlow = psl;
  jsonInteger(data, "rq", requiresPrevious);
  jsonInteger(data, "mo", manualOnly);
  setAutoSetpointRuleName(rule, String(name));
  rule.requiresPrevious = index > 0 && requiresPrevious ? 1 : 0;
  rule.manualOnly = manualOnly ? 1 : 0;
  const String error = validateAutoSetpointRule(rule);
  if (error.length()) {
    strlcpy(message, error.c_str(), messageSize);
    return message;
  }
  rules[index] = rule;
  return saveAutoSetpointRules(rules) ? "" : "could not save the rules";
}

const char *applyReset(const char *data) {
  int index;
  if (!ruleIndex(data, index)) return "invalid rule number";
  return resetAutoSetpointTrigger(index) ? "" : "could not reset the rule";
}

const char *applyTrigger(const char *data) {
  int index;
  if (!ruleIndex(data, index)) return "invalid rule number";
  switch (triggerAutoSetpointRuleNow(index)) {
    case AutoSetpointManualTriggerResult::Triggered:            return "";
    case AutoSetpointManualTriggerResult::InvalidRule:          return "the rule is empty or invalid";
    case AutoSetpointManualTriggerResult::AlreadyTriggered:     return "rule already triggered";
    case AutoSetpointManualTriggerResult::PreviousNotTriggered: return "the previous rule has not triggered";
    case AutoSetpointManualTriggerResult::NoClock:              return "no NTP time on the Povoto";
    default:                                                    return "could not save the trigger time";
  }
}

void requestSnapshots() {
  setpointPending = true;
  pendingRulesHash = currentRulesHash();
  rulesPending = (1U << AUTO_SETPOINT_RULE_COUNT) - 1;
}

void handleRequest(const char *json) {
  long id = 0;
  char type[12], base[12], data[REQUEST_MAX_BYTES];
  if (!jsonString(json, "t", type, sizeof(type))) return;
  if (!strcmp(type, "snap")) {
    // Read-only: always allowed, no answer needed.
    requestSnapshots();
    return;
  }
  if (!jsonInteger(json, "id", id)) return;
  for (Answer &previous : answers) {
    if (id && previous.id == id) {
      // Repeated request: the same answer again, never a second apply.
      previous.triesLeft = ACK_TRIES;
      previous.lastTryMs = 0;
      return;
    }
  }
  if (!editsAccepted) return answer(id, false, "the Povoto does not accept cloud edits");
  if (!jsonString(json, "h", base, sizeof(base)) || !jsonObject(json, "d", data, sizeof(data)))
    return answer(id, false, "malformed request");

  // Made on the version the cloud showed? Otherwise the Povoto changed since.
  const bool setpoints = !strcmp(type, "sp");
  char current[9];
  hashText(setpoints ? currentSetpointHash() : currentRulesHash(), current, sizeof(current));
  if (strcmp(base, current)) return answer(id, false, "changed on the Povoto since: reload and try again");

  char message[64] = "";
  const char *error;
  if (setpoints) error = applySetpoints(data);
  else if (!strcmp(type, "rule")) error = applyRule(data, message, sizeof(message));
  else if (!strcmp(type, "reset")) error = applyReset(data);
  else if (!strcmp(type, "trigger")) error = applyTrigger(data);
  else error = "unknown request";
  answer(id, !error[0], error);
}
} // namespace

void cloudSyncBegin() {
  requestQueue = xQueueCreateStatic(2, sizeof(Request), reinterpret_cast<uint8_t *>(requestStorage),
                                    &requestQueueControl);
  for (Answer &slot : answers) {
    slot.id = 0;
    slot.triesLeft = -1;
  }
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, true)) return; // namespace absent: defaults
  editsAccepted = store.getBool("edits", true);
  store.end();
}

bool cloudEditsAccepted() { return editsAccepted; }

bool setCloudEditsAccepted(bool accepted) {
  if (accepted == editsAccepted) return true;
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = store.putBool("edits", accepted) == 1;
  store.end();
  if (saved) editsAccepted = accepted;
  return saved;
}

void cloudSetpointHash(char *out, size_t size) { hashText(currentSetpointHash(), out, size); }
void cloudRulesHash(char *out, size_t size) { hashText(currentRulesHash(), out, size); }

unsigned long cloudRequestsApplied() { return applied; }
unsigned long cloudRequestsRejected() { return rejected; }
unsigned long cloudRequestsForeign() { return foreign; }

void cloudSyncReceive(const char *payload, const uint8_t *senderMac) {
  // Only the paired SideKick may send requests (docs/cloud-plan.md).
  if (!senderMac || isZeroMac(peerSideKick.mac) || memcmp(senderMac, peerSideKick.mac, 6)) {
    ++foreign;
    return;
  }
  if (!requestQueue || !payload) return;
  Request request;
  if (strlcpy(request.json, payload, sizeof(request.json)) >= sizeof(request.json)) return;
  xQueueSend(requestQueue, &request, 0); // full: dropped, the cloud reports it unconfirmed
}

void cloudSyncProcess() {
  if (!requestQueue) return;
  static Request request;
  if (xQueueReceive(requestQueue, &request, 0) == pdTRUE) handleRequest(request.json);

  if (!linkReady()) return;
  const unsigned long now = millis();

  // Answers: resent a few times, the cloud ignores repeats.
  for (Answer &slot : answers) {
    if (slot.triesLeft <= 0 || (slot.lastTryMs && now - slot.lastTryMs < ACK_RETRY_MS)) continue;
    if (sendAnswer(slot)) slot.triesLeft = slot.triesLeft > 1 ? 1 : 0; // sent: once more for safety
    else --slot.triesLeft;
    slot.lastTryMs = now | 1UL;
  }

  // A changed hash sends its snapshot (first pass after boot too).
  static unsigned long lastCheckMs = 0;
  if (!lastCheckMs || now - lastCheckMs >= HASH_CHECK_MS) {
    lastCheckMs = now | 1UL;
    const uint32_t setpointHash = currentSetpointHash();
    if (setpointHash != sentSetpointHash) setpointPending = true;
    const uint32_t rulesHash = currentRulesHash();
    if (rulesHash != sentRulesHash && rulesHash != pendingRulesHash) {
      pendingRulesHash = rulesHash;
      rulesPending = (1U << AUTO_SETPOINT_RULE_COUNT) - 1;
    }
  }
  if (now - lastSnapshotMs < SNAPSHOT_GAP_MS) return;
  if (setpointPending) {
    lastSnapshotMs = now;
    if (sendSetpointSnapshot()) {
      setpointPending = false;
      sentSetpointHash = currentSetpointHash();
    }
    return;
  }
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT && rulesPending; ++i) {
    if (!(rulesPending & (1U << i))) continue;
    lastSnapshotMs = now;
    if (currentRulesHash() != pendingRulesHash) { // changed while sending: start over
      pendingRulesHash = 0;
      return;
    }
    if (sendRuleSnapshot(i, pendingRulesHash)) {
      rulesPending &= ~(1U << i);
      if (!rulesPending) sentRulesHash = pendingRulesHash;
    }
    return;
  }
}
