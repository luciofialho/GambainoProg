#ifndef KEG_VOLUME_H
#define KEG_VOLUME_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Fermenter volume by a weighed keg on port A (docs/keg-volume.md): the slow
// volume routine of the expansion tank, with the keg in its place. Each relief
// equalizes the pressurized fermenter with the vented keg; waits end when the
// pressure is stable; the volume comes from the full mole balance with the
// fermenter (Dallas) and keg (environment) temperatures and Patm.

struct KegVolumeResult {
  bool available;
  float kegVolume;  // L
  uint8_t reliefs;
  float mean, sd;   // L (sd NAN with one relief)
  float simpleMean; // V_E (T_F/T_E) P2 / (P1 - P2), for comparison
};

bool startKegVolume(float kegVolume, uint8_t reliefs, char *reason, size_t reasonSize);
void abortKegVolume();
bool kegVolumeActive();
void processKegVolume(); // pressureControl(), main loop
String kegVolumeStatusText();
KegVolumeResult getKegVolumeResult();
String kegVolumeRowsHtml(); // one table row per relief of the last run
void handleKegVolumeCSV(AsyncWebServerRequest *request); // /calibration/keg.csv

#endif
