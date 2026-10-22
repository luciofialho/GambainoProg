#ifndef VENTING_TEST_H
#define VENTING_TEST_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "VentModel.h"

// Venting test (docs/venting-test.md): the empty fermenter, with air, vented
// through the valve for alternating durations at pressure levels from ~1.9 to
// ~0.5 bar; the real opening time of each release is measured. The fit gives
// the flow curve of the valve and its outlet (k, beta), its dispersion and the drift. The last 50 tests
// are kept in the data filesystem with their raw releases.

constexpr uint8_t VENTING_LABEL_COUNT = 5;
const char *ventingTestLabelName(uint8_t label); // as-is, muffler-cleaned, ...

struct VentingTestSummary {
  bool available;        // a finished test (the newest of the history)
  uint32_t epoch;
  uint16_t batch;
  uint8_t label;
  bool co2;
  float volume;          // L
  float patm;            // bar
  uint8_t patmSource;    // PATM_SOURCE_*
  float ambientC, fermenterC;
  uint8_t releases;      // used in the fit
  bool fitted;
  VentModel::Curve curve;
  float shapeRmsPercent; // relative residuals of the releases >= 4 s (information)
  float chiSquare;       // reduced chi-square: residuals / noise of each reading (~1; above 3: shape off)
  float driftPercent;    // flow of the last minus the first 2-s release of each level, mean
  float driftSePercent;  // its standard error (reading noise of the short releases)
  float vsReference[3];  // flow / reference flow - 1 at 1.8, 1.2, 0.8 bar (NAN = no reference)
  float vsSaved[3];      // the same against the saved curve
};

bool startVentingTest(uint8_t label, bool co2, float volume, char *reason, size_t reasonSize);
void abortVentingTest();
bool ventingTestActive();
void processVentingTest();       // pressureControl(), main loop
String ventingTestProgressText(); // one line, for the page
// index 0 = newest test of the history.
bool getVentingTestSummary(uint8_t index, VentingTestSummary &out);
uint8_t ventingTestHistoryCount();
uint8_t defaultVentingTestLabel(); // after-cleaning-cycle after a cleaning cycle
// Saves the curve of a test of the history as the flow curve or as the reference.
bool saveVentingTestCurve(uint8_t index, bool reference, char *reason, size_t reasonSize);
void handleVentingTestCSV(AsyncWebServerRequest *request); // /calibration/venting.csv?test=N|all

#endif
