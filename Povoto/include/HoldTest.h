#ifndef HOLD_TEST_H
#define HOLD_TEST_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Leak hold test (docs/hold-test.md): the pressurized system is held without opening the way to the
// atmosphere for the time typed by the operator (up to 72 h), with the transfer valve de-energized
// (A to R, the fermenter closed). With more than one step, the total time is divided into equal steps and,
// between them, a short controlled vent through the port A (open to the atmosphere) halves the pressure
// (or lowers it by the milder factor that lands the last step on 0.3 bar).
// Everything is logged (10, 30 or 60 s samples, in the PSRAM); the settling after each vent is discarded in
// the analysis. The loss rate of each step comes from the slope of (P + Patm) / T.

constexpr uint8_t HOLD_MAX_STEPS = 4;

struct HoldTestWindow {
  bool valid;         // enough samples
  uint16_t samples;   // samples in the fit
  float pressure;     // bar, gauge mean
  float slope;        // mbar/min, raw pressure
  float tempSlope;    // K/min, gas thermometer
  float lossPercent;  // %/min of the absolute pressure at constant temperature (positive = losing gas)
  float flow;         // mL/min of gas at atmospheric pressure (positive = losing gas)
};

struct HoldTestStep {
  HoldTestWindow whole;          // from the settling to the end of the step
  HoldTestWindow first, second;  // the two halves of that range: a thermal tail shows as a difference
};

struct HoldTestResult {
  bool available;     // at least one sample
  bool running;
  float minutes;      // planned
  float volume;       // L of gas for the flow: the FMT volume
  uint8_t steps;
  uint8_t currentStep;
  float stepMinutes;
  uint16_t intervalSeconds;
  uint32_t elapsedSeconds; // of the last sample
  char label[41];
  HoldTestWindow last10, last30; // of the current step, after its settling
  HoldTestStep step[HOLD_MAX_STEPS];
  bool exponentValid;  // loss against the gauge pressure, with 3 or more steps that lose
  float exponent;
};

bool startHoldTest(float minutes, uint8_t steps, const char *label, char *reason, size_t reasonSize);
void abortHoldTest();
bool holdTestActive();
void processHoldTest(); // pressureControl(), main loop
String holdTestStatusText();
HoldTestResult getHoldTestResult();
bool holdTestHasData();
void handleHoldTestCSV(AsyncWebServerRequest *request); // /calibration/holdtest.csv

#endif
