#ifndef EXPANSION_TANK_H
#define EXPANSION_TANK_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Fermenter volume with an expansion tank of known volume on port A
// (docs/expansion-tank-volume.md): the slow volume routine of the expansion tank era, with
// a plain tank. Each relief equalizes the pressurized fermenter with the vented
// tank; waits end when the pressure is stable; the volume comes from the full
// mole balance with the fermenter (Dallas) and tank (environment) temperatures
// and Patm. The number of reliefs is planned from the ratio of the volumes.
// Reliefs that did not settle are listed but left out of the mean.

struct ExpansionTankResult {
  bool available;     // at least one usable (finite, stable) relief
  float tankVolume;   // L, the expansion tank
  uint8_t reliefs;    // reliefs used (stable)
  uint8_t total;      // reliefs done
  uint8_t planned;    // reliefs planned at the start
  float mean, sd;     // L, weighted by (P1 - P2)^2 (sd NAN with one relief): the error of a relief from the pressure noise goes as 1/(P1 - P2)
  float minVolume, maxVolume; // L, of the stable reliefs
  float simpleMean;   // V_E (T_F/T_E) P2 / (P1 - P2), same weights, for comparison
  float maxWeight;    // share (0-1) of the largest weight among the stable reliefs
  float fmtVolume;    // the configured FMT volume now
  bool canSave;       // finished, at least 2 stable reliefs
};

bool startExpansionTank(float tankVolume, char *reason, size_t reasonSize);
// Planned number of reliefs to go from startPressure (gauge) to ~0.5 bar.
uint8_t expansionTankPlannedReliefs(float startPressure, float tankVolume);
void abortExpansionTank();
bool expansionTankActive();
void processExpansionTank(); // pressureControl(), main loop
String expansionTankStatusText();
ExpansionTankResult getExpansionTankResult();
// Saves the mean as the FMT volume and appends a calibration event.
bool saveExpansionTankAsFmtVolume(char *reason, size_t reasonSize);
String expansionTankRowsHtml(); // one table row per relief of the last run
void handleExpansionTankCSV(AsyncWebServerRequest *request); // /calibration/expansion.csv

#endif
