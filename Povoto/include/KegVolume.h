#ifndef KEG_VOLUME_H
#define KEG_VOLUME_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Fermenter volume with an expansion tank of known volume on port A
// (docs/keg-volume.md): the slow volume routine of the expansion tank era, with
// a plain tank. Each relief equalizes the pressurized fermenter with the vented
// tank; waits end when the pressure is stable; the volume comes from the full
// mole balance with the fermenter (Dallas) and tank (environment) temperatures
// and Patm. The number of reliefs is planned from the ratio of the volumes.
// Reliefs that did not settle are listed but left out of the mean.

struct KegVolumeResult {
  bool available;     // at least one usable (finite, stable) relief
  float tankVolume;   // L, the expansion tank
  uint8_t reliefs;    // reliefs used (stable)
  uint8_t total;      // reliefs done
  uint8_t planned;    // reliefs planned at the start
  float mean, sd;     // L (sd NAN with one relief)
  float simpleMean;   // V_E (T_F/T_E) P2 / (P1 - P2), for comparison
  float fmtVolume;    // the configured FMT volume now
  bool canSave;       // finished, at least 2 stable reliefs
};

bool startKegVolume(float tankVolume, char *reason, size_t reasonSize);
// Planned number of reliefs to go from startPressure (gauge) to ~0.5 bar.
uint8_t kegVolumePlannedReliefs(float startPressure, float tankVolume);
void abortKegVolume();
bool kegVolumeActive();
void processKegVolume(); // pressureControl(), main loop
String kegVolumeStatusText();
KegVolumeResult getKegVolumeResult();
// Saves the mean as the FMT volume and appends a calibration event.
bool saveKegVolumeAsFmtVolume(char *reason, size_t reasonSize);
String kegVolumeRowsHtml(); // one table row per relief of the last run
void handleKegVolumeCSV(AsyncWebServerRequest *request); // /calibration/keg.csv

#endif
