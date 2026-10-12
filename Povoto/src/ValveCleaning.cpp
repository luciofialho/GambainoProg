#include <Arduino.h>
#include "ValveCleaning.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoTasks.h"
#include "CalibrationEvents.h"

// docs/valve-cleaning.md
static constexpr unsigned long CLEANING_ON_MS = 2000UL;   // energized: A->P
static constexpr unsigned long CLEANING_OFF_MS = 2000UL;  // de-energized: A->R
static constexpr float CLEANING_MIN_MINUTES = 0.5f, CLEANING_MAX_MINUTES = 15.0f;

static const char *const PHASES[VALVE_CLEANING_PHASES] = {
  "neutral detergent", "water", "70% alcohol", "air (drying)"};

static bool sessionOpen = false;
static bool phaseRunning = false;
static uint8_t nextPhase = 0;
static unsigned long phaseStart = 0, phaseMs = 0, toggleMillis = 0;
static bool energized = false;
static uint16_t cycles = 0;
static char lastText[96] = "Not started";
// Set by the web handler; the loop closes the valve and ends the session.
static volatile bool abortRequested = false;

const char *valveCleaningPhaseName(uint8_t phase) {
  return phase < VALVE_CLEANING_PHASES ? PHASES[phase] : "done";
}

bool valveCleaningActive() { return sessionOpen; }
bool valveCleaningRunning() { return phaseRunning; }
uint8_t valveCleaningNextPhase() { return nextPhase; }

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (phaseRunning) {
    snprintf(line1, sizeof(line1), "Valve cleaning: phase %u/%u", nextPhase + 1, VALVE_CLEANING_PHASES);
    snprintf(line2, sizeof(line2), "%s on port A", PHASES[nextPhase]);
    snprintf(line3, sizeof(line3), "%lu/%lu s, valve %s", (now - phaseStart) / 1000UL, phaseMs / 1000UL,
             energized ? "A->P" : "A->R");
  } else if (sessionOpen) {
    snprintf(line1, sizeof(line1), "Valve cleaning: WAIT");
    snprintf(line2, sizeof(line2), "Next: %s on port A", valveCleaningPhaseName(nextPhase));
    snprintf(line3, sizeof(line3), "Confirm on the Calibration page");
  } else {
    snprintf(line1, sizeof(line1), "Valve cleaning: END");
    snprintf(line2, sizeof(line2), "%s", lastText);
    snprintf(line3, sizeof(line3), "Reconnect P, refit the muffler");
  }
  showCalibrationPanel(line1, line2, line3);
}

String valveCleaningStatusText() {
  char text[160];
  if (phaseRunning)
    snprintf(text, sizeof(text), "Phase %u of %u (%s): %lu of %lu s, %u cycles", nextPhase + 1, VALVE_CLEANING_PHASES,
             PHASES[nextPhase], (millis() - phaseStart) / 1000UL, phaseMs / 1000UL, cycles);
  else if (sessionOpen)
    snprintf(text, sizeof(text), "Waiting: put %s on port A and start phase %u of %u",
             valveCleaningPhaseName(nextPhase), nextPhase + 1, VALVE_CLEANING_PHASES);
  else
    snprintf(text, sizeof(text), "%s", lastText);
  return String(text);
}

bool startValveCleaningPhase(float minutes, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (phaseRunning) blocked = "A phase is running";
  else if (!sessionOpen && (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()))
    blocked = "Process in progress";
  else if (!isfinite(minutes) || minutes < CLEANING_MIN_MINUTES || minutes > CLEANING_MAX_MINUTES)
    blocked = "Phase duration must be from 0.5 to 15 min";
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  if (!sessionOpen) nextPhase = 0;
  abortRequested = false;
  sessionOpen = true;
  // The valve is driven by the loop only: it energizes on its next pass.
  phaseStart = millis();
  toggleMillis = phaseStart - CLEANING_OFF_MS;
  phaseMs = (unsigned long)(minutes * 60000.0f);
  cycles = 0;
  energized = false;
  phaseRunning = true;
  return true;
}

static void endSession(const char *text, bool completed) {
  routineValve(false);
  energized = false;
  phaseRunning = false;
  sessionOpen = false;
  snprintf(lastText, sizeof(lastText), "%s", text);
  char details[96];
  snprintf(details, sizeof(details), "%u of %u phases%s", nextPhase, VALVE_CLEANING_PHASES,
           completed ? "" : " (aborted)");
  // Only a completed cycle presets the next venting test's label.
  calibrationEventAppend(completed ? "valve-cleaning" : "valve-cleaning-aborted", details);
  showProgress(true);
}

// Web task: only requests it; processValveCleaning() aborts in the main loop.
void abortValveCleaning() {
  if (sessionOpen) abortRequested = true;
}

void processValveCleaning() {
  if (!sessionOpen) return;
  if (abortRequested) {
    abortRequested = false;
    endSession("Aborted", false);
    return;
  }
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0) {
    endSession("Aborted: mode or task changed", false);
    return;
  }
  if (!phaseRunning) {
    showProgress(false);
    return;
  }
  const unsigned long now = millis();
  if (now - phaseStart >= phaseMs) {
    routineValve(false);
    energized = false;
    phaseRunning = false;
    Serial.printf("[VALVE CLEANING] phase %u (%s) done, %u cycles\n", nextPhase + 1, PHASES[nextPhase], cycles);
    if (++nextPhase >= VALVE_CLEANING_PHASES) endSession("Completed: the next venting test is after-cleaning-cycle", true);
    else showProgress(true);
    return;
  }
  if (now - toggleMillis >= (energized ? CLEANING_ON_MS : CLEANING_OFF_MS)) {
    energized = !energized;
    if (energized) ++cycles;
    routineValve(energized);
    toggleMillis = now;
  }
  showProgress(false);
}
