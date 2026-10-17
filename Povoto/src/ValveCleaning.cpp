#include <Arduino.h>
#include "ValveCleaning.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "PovotoTasks.h"
#include "CalibrationEvents.h"

// docs/valve-cleaning.md
static constexpr unsigned long CLEANING_ON_MS = 2000UL;   // energized: A->P
static constexpr unsigned long CLEANING_OFF_MS = 2000UL;  // de-energized: A->R
static constexpr float CLEANING_MIN_MINUTES = 0.5f, CLEANING_MAX_MINUTES = 15.0f;

static bool running = false;
static unsigned long runStart = 0, runMs = 0, toggleMillis = 0;
static bool energized = false;
static uint16_t cycles = 0;
static char lastText[96] = "Not started";
// Set by the web handler; the loop closes the valve and ends the run.
static volatile bool abortRequested = false;

bool valveCleaningActive() { return running; }

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (running) {
    snprintf(line1, sizeof(line1), "Valve cleaning: RUN");
    snprintf(line2, sizeof(line2), "%lu/%lu s, %u cycles", (now - runStart) / 1000UL, runMs / 1000UL, cycles);
    snprintf(line3, sizeof(line3), "Valve %s", energized ? "A->P" : "A->R (LED on)");
  } else {
    snprintf(line1, sizeof(line1), "Valve cleaning: END");
    snprintf(line2, sizeof(line2), "%s", lastText);
    snprintf(line3, sizeof(line3), "Change the liquid or reconnect P, R");
  }
  showCalibrationPanel(line1, line2, line3);
}

String valveCleaningStatusText() {
  char text[160];
  if (running)
    snprintf(text, sizeof(text), "Running: %lu of %lu s, %u cycles", (millis() - runStart) / 1000UL, runMs / 1000UL, cycles);
  else
    snprintf(text, sizeof(text), "%s", lastText);
  return String(text);
}

bool startValveCleaning(float minutes, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (running) blocked = "A run is in progress";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(minutes) || minutes < CLEANING_MIN_MINUTES || minutes > CLEANING_MAX_MINUTES)
    blocked = "Duration must be from 0.5 to 15 min";
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  abortRequested = false;
  // The valve is driven by the loop only: it energizes on its next pass.
  runStart = millis();
  toggleMillis = runStart - CLEANING_OFF_MS;
  runMs = (unsigned long)(minutes * 60000.0f);
  cycles = 0;
  energized = false;
  running = true;
  return true;
}

static void endRun(const char *text, bool completed) {
  routineValve(false);
  digitalWrite(PINVENTINGLED, LOW);
  energized = false;
  running = false;
  snprintf(lastText, sizeof(lastText), "%s", text);
  char details[96];
  snprintf(details, sizeof(details), "%.1f of %.1f min, %u cycles%s", (millis() - runStart) / 60000.0f, runMs / 60000.0f,
           cycles, completed ? "" : " (aborted)");
  // A completed run presets the next venting test's label.
  calibrationEventAppend(completed ? "valve-cleaning" : "valve-cleaning-aborted", details);
  showProgress(true);
}

// Web task: only requests it; processValveCleaning() aborts in the main loop.
void abortValveCleaning() {
  if (running) abortRequested = true;
}

void processValveCleaning() {
  if (!running) return;
  if (abortRequested) {
    abortRequested = false;
    endRun("Aborted", false);
    return;
  }
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0) {
    endRun("Aborted: mode or task changed", false);
    return;
  }
  const unsigned long now = millis();
  if (now - runStart >= runMs) {
    endRun("Completed: the next venting test is after-cleaning-cycle", true);
    return;
  }
  if (now - toggleMillis >= (energized ? CLEANING_ON_MS : CLEANING_OFF_MS)) {
    energized = !energized;
    if (energized) ++cycles;
    routineValve(energized);
    toggleMillis = now;
  }
  // The venting LED signals the closed valve (A->R) while the cleaning runs.
  digitalWrite(PINVENTINGLED, energized ? LOW : HIGH);
  showProgress(false);
}
