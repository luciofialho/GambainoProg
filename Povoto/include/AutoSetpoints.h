#ifndef AUTOSETPOINTS_H
#define AUTOSETPOINTS_H

#include <Arduino.h>

constexpr int AUTO_SETPOINT_RULE_COUNT = 8;
constexpr size_t AUTO_SETPOINT_NAME_SIZE = 81; // 80 bytes of UTF-8 plus the terminator
// Same pressure limit as the touch keyboard for the pressure set point.
constexpr float AUTO_SETPOINT_MAX_PRESSURE = 2.0f;
constexpr float AUTO_SETPOINT_MAX_TEMPERATURE = 42.0f;

// Every numeric field is optional: NAN means empty (criterion ignored / no action).
struct AutoSetpointRule_t {
  char name[AUTO_SETPOINT_NAME_SIZE]; // what the rule is meant for (may be empty)
  // Triggers, combined with AND.
  float stableHours;          // temperature STABLE for more than this (h)
  float pressureStableHours;  // pressure STABLE for more than this (h)
  float sgBelow;              // SG <
  float co2RateBelow;         // g CO2/L/day < (a computed value of exactly 0 never fires)
  // Actions; slow values use the slow set point speeds of the set point page.
  float pressure;
  float pressureSlow;
  float temperature;
  float temperatureSlow;
  // Extra condition (rules 2 and later): the previous rule must have fired.
  uint8_t requiresPrevious;
  // Only the Trigger now button can fire this rule; measurement triggers stay stored.
  uint8_t manualOnly;
} __attribute__((packed));

// Local NTP epoch (UTC-3) of the trigger; 0 = not yet triggered.
struct AutoSetpointStatus_t {
  uint32_t triggeredAt[AUTO_SETPOINT_RULE_COUNT];
};

// Readers and writers use copies under a lock: the web server runs in
// another task than the evaluation loop.
void getAutoSetpointRules(AutoSetpointRule_t *rules);
void getAutoSetpointStatus(AutoSetpointStatus_t &status);

// Stores the name without control characters, cut at a UTF-8 boundary.
void setAutoSetpointRuleName(AutoSetpointRule_t &rule, const String &name);
bool autoSetpointRuleHasTrigger(const AutoSetpointRule_t &rule);
bool autoSetpointRuleIsEmpty(const AutoSetpointRule_t &rule);
// Returns an empty string when valid, otherwise a message for the page.
String validateAutoSetpointRule(const AutoSetpointRule_t &rule);

// Rules already triggered keep their stored definition.
bool saveAutoSetpointRules(const AutoSetpointRule_t *rules);
// Replaces every rule, triggered or not, and clears all trigger times.
bool importAutoSetpointRules(const AutoSetpointRule_t *rules);
bool resetAutoSetpointTrigger(int index);
bool resetAllAutoSetpointTriggers();

// Values used when the rules are evaluated (NAN = unavailable).
struct AutoSetpointMeasurements {
  float stableHours;
  float pressureStableHours;
  float sg;
  float co2Rate;
};

struct AutoSetpointFiring_t {
  int index;
  uint32_t triggeredAt;
  AutoSetpointRule_t rule;
  AutoSetpointMeasurements measured;
  bool manuallyTriggered;
};

// Called from loop(); only in Fermenting mode and with a valid NTP time.
void evaluateAutoSetpoints();
enum class AutoSetpointManualTriggerResult {
  Triggered,
  InvalidRule,
  AlreadyTriggered,
  PreviousNotTriggered,
  NoClock,
  StorageError
};
AutoSetpointManualTriggerResult triggerAutoSetpointRuleNow(int index);
// Plain-text report: rule, time, criteria with measured values, actions.
String describeAutoSetpointFiring(const AutoSetpointFiring_t &firing);

bool readAutoSetpointsFromEEPROM();
bool writeAutoSetpointsToNIV();
void resetAutoSetpointsToDefaults();

#endif // AUTOSETPOINTS_H
