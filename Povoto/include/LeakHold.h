#ifndef LEAK_HOLD_H
#define LEAK_HOLD_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Gas-loss test (docs/leak-hold-test.md): the pressurized fermenter is held closed for a while
// (valve de-energized, the expansion tank vents), then held with the valve to the expansion tank
// open. The pressure (10-s means) and temperatures are logged; the loss rate of each phase comes
// from the slope of (P + Patm) / T after the thermal settling. The tank side (tank, valve seat,
// connections) is the difference between the two.

struct LeakHoldPhase {
  bool valid;        // enough samples after the settling time
  uint8_t samples;   // samples used in the fit
  float pressure;    // bar, gauge mean
  float slope;       // mbar/min, raw pressure
  float tempSlope;   // K/min, fermenter Dallas
  float lossPercent; // %/min of the absolute pressure at constant temperature (positive = losing gas)
  float flow;        // mL/min of gas at atmospheric pressure (positive = losing gas)
};

struct LeakHoldResult {
  bool available;    // at least one phase has a result
  bool running;
  float tankVolume;  // L
  LeakHoldPhase closed;  // fermenter alone
  LeakHoldPhase open;    // fermenter + expansion tank
  bool tankSideValid;
  float tankSideFlow; // mL/min: open minus closed (closed scaled by the gauge pressures)
};

bool startLeakHold(float tankVolume, char *reason, size_t reasonSize);
void abortLeakHold();
bool leakHoldActive();
void processLeakHold(); // pressureControl(), main loop
String leakHoldStatusText();
LeakHoldResult getLeakHoldResult();
bool leakHoldHasData();
void handleLeakHoldCSV(AsyncWebServerRequest *request); // /calibration/leak.csv

#endif
