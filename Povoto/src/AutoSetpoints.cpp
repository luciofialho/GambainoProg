#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include "PovotoCommon.h"
#include "PovotoData.h"
#include <IOTK.h>
#include <IOTK_ESPAsyncServer.h> // Before IOTK_NTP.h: keeps NTPClient in the dependency graph.
#include <IOTK_NTP.h>
#include "PressureControl.h"
#include "TemperatureControl.h"
#include "datalog.h"
#include "PovotoMail.h"
#include "AutoSetpoints.h"

// NVS namespace pvt_autosp: one blob per rule (rule0, rule1, ...) and the
// trigger times (trig0, trig1, ...). Trigger times are written separately so
// that saving definitions never rewrites them.

static const AutoSetpointRule_t emptyRule = {"", NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, 0, 0};

// Filled with emptyRule by resetAutoSetpointsToDefaults() when loaded at boot.
static AutoSetpointRule_t autoSetpointRules[AUTO_SETPOINT_RULE_COUNT];
static AutoSetpointStatus_t autoSetpointStatus = {};
static portMUX_TYPE autoSetpointLock = portMUX_INITIALIZER_UNLOCKED;

void getAutoSetpointRules(AutoSetpointRule_t *rules) {
  portENTER_CRITICAL(&autoSetpointLock);
  memcpy(rules, autoSetpointRules, sizeof(autoSetpointRules));
  portEXIT_CRITICAL(&autoSetpointLock);
}

void getAutoSetpointStatus(AutoSetpointStatus_t &status) {
  portENTER_CRITICAL(&autoSetpointLock);
  status = autoSetpointStatus;
  portEXIT_CRITICAL(&autoSetpointLock);
}

void setAutoSetpointRuleName(AutoSetpointRule_t &rule, const String &name) {
  size_t length = 0;
  for (size_t i = 0; i < name.length() && length + 1 < sizeof(rule.name); i++) {
    const unsigned char c = (unsigned char)name[i];
    if (c < 32 || c == 127) continue;
    rule.name[length++] = (char)c;
  }
  // Drop a multi-byte UTF-8 character cut by the size limit.
  size_t start = length;
  while (start > 0 && ((unsigned char)rule.name[start - 1] & 0xC0) == 0x80) start--;
  if (start > 0 && ((unsigned char)rule.name[start - 1] & 0x80)) {
    const unsigned char lead = (unsigned char)rule.name[start - 1];
    const size_t expected = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
    if (length - (start - 1) < expected) length = start - 1;
  }
  rule.name[length] = '\0';
}

bool autoSetpointRuleHasTrigger(const AutoSetpointRule_t &rule) {
  return !isnan(rule.stableHours) || !isnan(rule.pressureStableHours) ||
         !isnan(rule.sgBelow) || !isnan(rule.co2RateBelow);
}

bool autoSetpointRuleIsEmpty(const AutoSetpointRule_t &rule) {
  return !autoSetpointRuleHasTrigger(rule) &&
         isnan(rule.pressure) && isnan(rule.pressureSlow) &&
         isnan(rule.temperature) && isnan(rule.temperatureSlow);
}

static bool inRangeOrEmpty(float value, float minimum, float maximum) {
  return isnan(value) || (isfinite(value) && value >= minimum && value <= maximum);
}

String validateAutoSetpointRule(const AutoSetpointRule_t &rule) {
  if (rule.manualOnly > 1)
    return "invalid 'manual only' flag.";
  if (rule.requiresPrevious > 1)
    return "invalid 'requires previous rule' flag.";
  if (autoSetpointRuleIsEmpty(rule)) return "";
  if (!rule.manualOnly && !autoSetpointRuleHasTrigger(rule))
    return "at least one trigger is required.";
  if (!inRangeOrEmpty(rule.stableHours, 1.0f, 360.0f))
    return "temperature stable hours must be from 1 to 360.";
  if (!inRangeOrEmpty(rule.pressureStableHours, 1.0f, 360.0f))
    return "pressure stable hours must be from 1 to 360.";
  if (!inRangeOrEmpty(rule.sgBelow, 0.990f, 1.200f))
    return "SG must be from 0.990 to 1.200.";
  if (!inRangeOrEmpty(rule.co2RateBelow, 0.5f, 20.0f))
    return "gCO2/L/d must be from 0.5 to 20.";
  if (!inRangeOrEmpty(rule.pressure, 0.0f, AUTO_SETPOINT_MAX_PRESSURE) ||
      !inRangeOrEmpty(rule.pressureSlow, 0.0f, AUTO_SETPOINT_MAX_PRESSURE))
    return "pressure set points must be from 0 to 2 bar.";
  if (!inRangeOrEmpty(rule.temperature, 0.0f, AUTO_SETPOINT_MAX_TEMPERATURE) ||
      !inRangeOrEmpty(rule.temperatureSlow, 0.0f, AUTO_SETPOINT_MAX_TEMPERATURE))
    return "temperature set points must be from 0 to 42 C.";
  return "";
}

static bool writeRules(const AutoSetpointRule_t *rules) {
  Preferences store;
  if (!store.begin("pvt_autosp", false)) {
    Serial.println("NVS: cannot open pvt_autosp");
    return false;
  }
  bool saved = true;
  char key[8];
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    snprintf(key, sizeof(key), "rule%d", i);
    saved = (store.putBytes(key, &rules[i], sizeof(rules[i])) == sizeof(rules[i])) && saved;
  }
  store.end();
  if (!saved) Serial.println("NVS: auto set point rules save incomplete");
  return saved;
}

static bool writeTrigger(int index, uint32_t triggeredAt) {
  Preferences store;
  if (!store.begin("pvt_autosp", false)) {
    Serial.println("NVS: cannot open pvt_autosp");
    return false;
  }
  char key[8];
  snprintf(key, sizeof(key), "trig%d", index);
  const bool saved = store.putUInt(key, triggeredAt) == sizeof(triggeredAt);
  store.end();
  if (!saved) Serial.println("NVS: auto set point trigger save incomplete");
  return saved;
}

bool saveAutoSetpointRules(const AutoSetpointRule_t *rules) {
  AutoSetpointRule_t snapshot[AUTO_SETPOINT_RULE_COUNT];
  portENTER_CRITICAL(&autoSetpointLock);
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    if (autoSetpointStatus.triggeredAt[i] == 0)
      autoSetpointRules[i] = rules[i];
  }
  memcpy(snapshot, autoSetpointRules, sizeof(snapshot));
  portEXIT_CRITICAL(&autoSetpointLock);
  return writeRules(snapshot);
}

bool importAutoSetpointRules(const AutoSetpointRule_t *rules) {
  portENTER_CRITICAL(&autoSetpointLock);
  memcpy(autoSetpointRules, rules, sizeof(autoSetpointRules));
  autoSetpointStatus = {};
  portEXIT_CRITICAL(&autoSetpointLock);
  return writeAutoSetpointsToNIV();
}

bool resetAutoSetpointTrigger(int index) {
  if (index < 0 || index >= AUTO_SETPOINT_RULE_COUNT) return false;
  portENTER_CRITICAL(&autoSetpointLock);
  autoSetpointStatus.triggeredAt[index] = 0;
  portEXIT_CRITICAL(&autoSetpointLock);
  return writeTrigger(index, 0);
}

bool resetAllAutoSetpointTriggers() {
  bool saved = true;
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++)
    saved = resetAutoSetpointTrigger(i) && saved;
  return saved;
}

bool readAutoSetpointsFromEEPROM() {
  resetAutoSetpointsToDefaults();
  Preferences store;
  if (!store.begin("pvt_autosp", true)) return false;
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status = {};
  bool incompatible[AUTO_SETPOINT_RULE_COUNT] = {};
  char key[8];
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    rules[i] = emptyRule;
    snprintf(key, sizeof(key), "rule%d", i);
    const size_t storedSize = store.getBytesLength(key);
    if (storedSize == sizeof(rules[i]))
      store.getBytes(key, &rules[i], sizeof(rules[i]));
    else if (storedSize != 0)
      incompatible[i] = true;
    rules[i].name[sizeof(rules[i].name) - 1] = '\0';
    // An invalid stored rule is dropped rather than risk an unexpected action.
    if (validateAutoSetpointRule(rules[i]).length() != 0)
      rules[i] = emptyRule;
    snprintf(key, sizeof(key), "trig%d", i);
    status.triggeredAt[i] = incompatible[i] ? 0 : store.getUInt(key, 0);
  }
  store.end();

  // A discarded old rule must not stay locked by its separately stored time.
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++)
    if (incompatible[i]) writeTrigger(i, 0);

  portENTER_CRITICAL(&autoSetpointLock);
  memcpy(autoSetpointRules, rules, sizeof(autoSetpointRules));
  autoSetpointStatus = status;
  portEXIT_CRITICAL(&autoSetpointLock);
  return true;
}

bool writeAutoSetpointsToNIV() {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);
  bool saved = writeRules(rules);
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++)
    saved = writeTrigger(i, status.triggeredAt[i]) && saved;
  return saved;
}

void resetAutoSetpointsToDefaults() {
  portENTER_CRITICAL(&autoSetpointLock);
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++)
    autoSetpointRules[i] = emptyRule;
  autoSetpointStatus = {};
  portEXIT_CRITICAL(&autoSetpointLock);
}

// ===== Evaluation =====

static constexpr unsigned long AUTO_SETPOINT_EVALUATION_MS = 10000UL;

// Each value is NAN when it cannot be used; a criterion on a NAN value is
// never met.
static AutoSetpointMeasurements readAutoSetpointMeasurements(uint32_t now) {
  AutoSetpointMeasurements m;
  const bool stable = CountersData.tempState == TEMP_STATE_STABLE &&
                      CountersData.tempStableSince != 0 &&
                      now >= CountersData.tempStableSince &&
                      ControlData.temperature != NOTaTEMP;
  m.stableHours = stable ? float(now - CountersData.tempStableSince) / 3600.0f : NAN;
  const bool pressureStable = CountersData.pressState == TEMP_STATE_STABLE &&
                              CountersData.pressStableSince != 0 &&
                              now >= CountersData.pressStableSince &&
                              pressureReadingValid();
  m.pressureStableHours = pressureStable ? float(now - CountersData.pressStableSince) / 3600.0f : NAN;
  m.sg = (isfinite(beerSG) && beerSG > 0.980f && beerSG < 1.200f) ? beerSG : NAN;
  // The raw rate is 0 until enough samples exist and may be negative; only
  // a positive computed value is usable.
  m.co2Rate = (isfinite(beerCO2EvolutionGramsPerLiterPerDay) && beerCO2EvolutionGramsPerLiterPerDay > 0.0f)
      ? beerCO2EvolutionGramsPerLiterPerDay : NAN;
  return m;
}

static bool autoSetpointRuleMet(const AutoSetpointRule_t &rule, const AutoSetpointMeasurements &m) {
  if (rule.manualOnly) return false;
  if (!autoSetpointRuleHasTrigger(rule)) return false;
  if (!isnan(rule.stableHours) && !(m.stableHours > rule.stableHours)) return false;
  if (!isnan(rule.pressureStableHours) && !(m.pressureStableHours > rule.pressureStableHours)) return false;
  if (!isnan(rule.sgBelow) && !(m.sg < rule.sgBelow)) return false;
  if (!isnan(rule.co2RateBelow) && !(m.co2Rate < rule.co2RateBelow)) return false;
  return true;
}

// Same meaning as the set point page: a direct value alone cancels a ramp in
// progress; a slow value ramps from the (possibly new) direct value.
static void applyAutoSetpointActions(const AutoSetpointRule_t &rule) {
  bool changed = false;
  if (!isnan(rule.temperature) || !isnan(rule.temperatureSlow)) {
    if (!isnan(rule.temperature)) SetPointData.setPointTemp = rule.temperature;
    SetPointData.setPointSlowTemp = isnan(rule.temperatureSlow) ? NOTaTEMP : rule.temperatureSlow;
    // Restart stability even when the value is unchanged.
    markTemperatureSetpointChanged(SetPointData.setPointSlowTemp != NOTaTEMP);
    changed = true;
  }
  if (!isnan(rule.pressure) || !isnan(rule.pressureSlow)) {
    if (!isnan(rule.pressure)) SetPointData.setPointPressure = rule.pressure;
    SetPointData.setPointSlowPressure = isnan(rule.pressureSlow) ? NOTaTEMP : rule.pressureSlow;
    // Restart stability even when the value is unchanged.
    markPressureSetpointChanged(SetPointData.setPointSlowPressure != NOTaTEMP);
    changed = true;
  }
  if (changed) writeSetPointDataToNIV();
}

static void appendValue(String &text, const char *label, float value, int decimals, const char *unit) {
  if (isnan(value)) return;
  if (text.length()) text += ", ";
  text += label;
  text += String(value, decimals);
  text += unit;
}

String describeAutoSetpointFiring(const AutoSetpointFiring_t &firing) {
  char when[20];
  formatLocalEpochISO(firing.triggeredAt, when, sizeof(when));
  const AutoSetpointRule_t &r = firing.rule;
  const AutoSetpointMeasurements &m = firing.measured;

  String criteria;
  if (firing.manuallyTriggered) {
    criteria += "Triggered manually; measurement criteria were not evaluated.\n";
  } else {
    if (!isnan(r.stableHours))
      criteria += "temperature stable > " + String(r.stableHours, 1) + " h (measured " + String(m.stableHours, 1) + " h)\n";
    if (!isnan(r.pressureStableHours))
      criteria += "pressure stable > " + String(r.pressureStableHours, 1) + " h (measured " + String(m.pressureStableHours, 1) + " h)\n";
    if (!isnan(r.sgBelow))
      criteria += "SG < " + String(r.sgBelow, 3) + " (measured " + String(m.sg, 4) + ")\n";
    if (!isnan(r.co2RateBelow))
      criteria += "gCO2/L/d < " + String(r.co2RateBelow, 2) + " (measured " + String(m.co2Rate, 2) + ")\n";
  }
  if (r.requiresPrevious && firing.index > 0)
    criteria += "rule " + String(firing.index) + " already triggered\n";

  String actions;
  appendValue(actions, "temperature set point ", r.temperature, 1, " C");
  appendValue(actions, "temperature slow set point ", r.temperatureSlow, 1, " C");
  appendValue(actions, "pressure set point ", r.pressure, 2, " bar");
  appendValue(actions, "pressure slow set point ", r.pressureSlow, 2, " bar");
  if (!actions.length()) actions = "none (notification only)";

  return "Povoto " + String((int)FMTData.PovotoNum) + ", batch " + String((int)BatchData.batchNumber) +
         " (" + String(BatchData.batchName) + ")\n" +
         "Automatic set point rule " + String(firing.index + 1) +
         (r.name[0] ? " - " + String(r.name) : String("")) +
         " triggered at " + String(when) + "\n\n" +
         "Criteria:\n" + criteria + "\nActions applied: " + actions + "\n";
}

static AutoSetpointManualTriggerResult fireAutoSetpointRule(
    int index, uint32_t now, const AutoSetpointMeasurements &measured, bool manuallyTriggered) {
  AutoSetpointRule_t rule;
  AutoSetpointManualTriggerResult result = AutoSetpointManualTriggerResult::Triggered;
  portENTER_CRITICAL(&autoSetpointLock);
  if (autoSetpointStatus.triggeredAt[index] != 0)
    result = AutoSetpointManualTriggerResult::AlreadyTriggered;
  else if (index > 0 && autoSetpointRules[index].requiresPrevious &&
           autoSetpointStatus.triggeredAt[index - 1] == 0)
    result = AutoSetpointManualTriggerResult::PreviousNotTriggered;
  else if (!manuallyTriggered && !autoSetpointRuleMet(autoSetpointRules[index], measured))
    result = AutoSetpointManualTriggerResult::InvalidRule;
  else {
    rule = autoSetpointRules[index];
    autoSetpointStatus.triggeredAt[index] = now;
  }
  portEXIT_CRITICAL(&autoSetpointLock);
  if (result != AutoSetpointManualTriggerResult::Triggered) return result;

  // Persist before applying. A failed write must not execute the actions.
  if (!writeTrigger(index, now)) {
    portENTER_CRITICAL(&autoSetpointLock);
    if (autoSetpointStatus.triggeredAt[index] == now)
      autoSetpointStatus.triggeredAt[index] = 0;
    portEXIT_CRITICAL(&autoSetpointLock);
    return AutoSetpointManualTriggerResult::StorageError;
  }

  applyAutoSetpointActions(rule);

  const AutoSetpointFiring_t firing = {index, now, rule, measured, manuallyTriggered};
  const String report = describeAutoSetpointFiring(firing);
  Serial.print("[AUTOSP] ");
  Serial.println(report);

  char subject[160]; // Fits the longest name (AUTO_SETPOINT_NAME_SIZE).
  snprintf(subject, sizeof(subject), "[POVOTO %d] Automatic set point rule %d triggered%s%s",
           (int)FMTData.PovotoNum, index + 1, rule.name[0] ? ": " : "", rule.name);
  queuePovotoMail(subject, report);
  return AutoSetpointManualTriggerResult::Triggered;
}

AutoSetpointManualTriggerResult triggerAutoSetpointRuleNow(int index) {
  if (index < 0 || index >= AUTO_SETPOINT_RULE_COUNT)
    return AutoSetpointManualTriggerResult::InvalidRule;
  const uint32_t now = NTPEpoch();
  if (now == 0) return AutoSetpointManualTriggerResult::NoClock;
  return fireAutoSetpointRule(index, now, readAutoSetpointMeasurements(now), true);
}

void evaluateAutoSetpoints() {
  static unsigned long lastEvaluation = 0;
  if (!MILLISDIFF(lastEvaluation, AUTO_SETPOINT_EVALUATION_MS)) return;
  lastEvaluation = millis();
  if (SetPointData.mode != MODE_FERMENTING) return;

  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);

  bool pending = false;
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++)
    pending = pending || (status.triggeredAt[i] == 0 && !rules[i].manualOnly &&
                          autoSetpointRuleHasTrigger(rules[i]));
  if (!pending) return;

  const uint32_t now = NTPEpoch();
  if (now == 0) return; // No valid time: nothing fires.
  const AutoSetpointMeasurements measured = readAutoSetpointMeasurements(now);

  // In index order, at most one rule per evaluation, so a rule that changes
  // the temperature restarts stability before the next rule is checked.
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    if (status.triggeredAt[i] != 0) continue;
    if (i > 0 && rules[i].requiresPrevious && status.triggeredAt[i - 1] == 0) continue;
    if (!autoSetpointRuleMet(rules[i], measured)) continue;
    if (fireAutoSetpointRule(i, now, measured, false) == AutoSetpointManualTriggerResult::Triggered)
      return;
  }
}
