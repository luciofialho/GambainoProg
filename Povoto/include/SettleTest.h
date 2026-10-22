#ifndef SETTLE_TEST_H
#define SETTLE_TEST_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Settle validation test (docs/venting-test.md, section 5): a one-off routine, run by URL only (no page),
// that releases the pressurized empty fermenter eight times and logs the whole 20 min pressure recovery of
// each one (1 reading per second) in /settle.csv. It tells how the recovery after closing behaves, to
// validate an exponential extrapolation of the settled pressure. It is not kept in any history: the file
// is overwritten by the next run.
//   /settle/start?confirm=yes   starts it (mode OFF, ~2 bar in the fermenter, nothing else running)
//   /settle/status              progress (refreshes every 10 s)
//   /settle/abort?confirm=yes   aborts it (the valve is closed; the file keeps what was logged)
//   /settle/file                downloads /settle.csv (also while it runs)
bool settleTestActive();
void processSettleTest();        // from processVentingTest(), main loop
void handleSettleStart(AsyncWebServerRequest *request);
void handleSettleStatus(AsyncWebServerRequest *request);
void handleSettleAbort(AsyncWebServerRequest *request);
void handleSettleFile(AsyncWebServerRequest *request);

#endif
