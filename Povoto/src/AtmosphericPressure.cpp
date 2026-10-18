#include <Arduino.h>
#include <math.h>
#include "PovotoData.h"
#include "AtmosphericPressure.h"
#include "datalog.h"

// From IOTK_NTP. Not #included: this file is the first one the PlatformIO LDF
// scans, and finding __IOTK_NTP this early leaves NTPClient.h out of the
// include path of __IOTK_ESPAsyncServer (build fails).
unsigned long int NTPEpoch();

// docs/atmospheric-pressure.md
static constexpr float PATM_MIN_HPA = 700.0f;
static constexpr float PATM_MAX_HPA = 1100.0f;
static constexpr unsigned long PATM_AVERAGE_MS = 60000UL;         // moving average
static constexpr unsigned long PATM_SIDEKICK_TIMEOUT_MS = 10UL * 60000UL; // same as the environment
static constexpr unsigned long PATM_SELECT_MS = 1000UL;  // the source is re-evaluated this often

struct PatmSample {
  unsigned long millisStamp;
  float hPa;
};

// Moving average of the samples of the last PATM_AVERAGE_MS before the newest.
template <uint8_t N> struct PatmSeries {
  PatmSample samples[N];
  uint8_t next = 0, count = 0;
  void add(unsigned long now, float hPa) {
    samples[next] = {now, hPa};
    next = (next + 1) % N;
    if (count < N) ++count;
  }
  bool newest(PatmSample &out) const {
    if (count == 0) return false;
    out = samples[(next + N - 1) % N];
    return true;
  }
  float average() const {
    PatmSample last;
    if (!newest(last)) return NAN;
    double sum = 0.0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const PatmSample &s = samples[(next + N - 1 - i) % N];
      if (last.millisStamp - s.millisStamp > PATM_AVERAGE_MS) break;
      sum += s.hPa;
      ++n;
    }
    return n ? float(sum / n) : NAN;
  }
};

static PatmSeries<16> sidekickSeries;  // one packet every 10 s
static portMUX_TYPE sidekickMux = portMUX_INITIALIZER_UNLOCKED;

struct PatmSourceState {
  bool valid;
  float hPa;           // moving average
  unsigned long ageMs; // of the newest sample
};
static PatmSourceState sourceState[2];
static uint8_t selectedSource = PATM_SOURCE_ALTITUDE;
// Highest-priority source seen valid since the boot; using a lower one is a
// fallback, shown in the status until the better source returns.
static uint8_t bestSourceSeen = PATM_SOURCE_ALTITUDE;
static uint32_t fallbackEpoch = 0;
static uint8_t fallbackFrom = PATM_SOURCE_ALTITUDE;

static bool plausible(float hPa) {
  return isfinite(hPa) && hPa >= PATM_MIN_HPA && hPa <= PATM_MAX_HPA;
}

static float altitudeHPa() {
  const float altitude = FMTData.FMTAltitude > 0.0f ? FMTData.FMTAltitude : 0.0f;
  return 1013.25f * powf(1.0f - (2.25577e-5f * altitude), 5.25588f);
}

const char *atmosphericPressureSourceLabel(uint8_t source) {
  switch (source) {
    case PATM_SOURCE_SIDEKICK: return "sidekick";
    default: return "altitude";
  }
}

const char *atmosphericPressureSourceLabel() {
  return atmosphericPressureSourceLabel(selectedSource);
}

uint8_t atmosphericPressureSource() {
  return selectedSource;
}

// Re-evaluates the sources and sets Patm (bar).
static void selectAtmosphericPressure(unsigned long now) {
  PatmSample newest;
  // No barometer on the Povoto: the pressure comes from the SideKick.
  PatmSourceState &sidekick = sourceState[PATM_SOURCE_SIDEKICK];
  sidekick = {false, NAN, 0};
  portENTER_CRITICAL(&sidekickMux);
  const PatmSeries<16> received = sidekickSeries;
  portEXIT_CRITICAL(&sidekickMux);
  if (received.newest(newest)) {
    sidekick.ageMs = now - newest.millisStamp;
    sidekick.hPa = received.average();
    sidekick.valid = sidekick.ageMs <= PATM_SIDEKICK_TIMEOUT_MS && plausible(sidekick.hPa);
  }
  sourceState[PATM_SOURCE_ALTITUDE] = {true, altitudeHPa(), 0};

  uint8_t source = PATM_SOURCE_ALTITUDE;
  for (uint8_t s = 0; s < PATM_SOURCE_ALTITUDE; ++s)
    if (sourceState[s].valid) { source = s; break; }
  if (source < bestSourceSeen) bestSourceSeen = source;
  if (source != selectedSource) {
    Serial.printf("[PATM] source %s -> %s (%.2f hPa)\n", atmosphericPressureSourceLabel(selectedSource),
                  atmosphericPressureSourceLabel(source), sourceState[source].hPa);
    if (source > selectedSource) {
      fallbackEpoch = NTPEpoch();
      fallbackFrom = selectedSource;
    }
    selectedSource = source;
  }
  if (selectedSource == bestSourceSeen) fallbackEpoch = 0;
  Patm = sourceState[selectedSource].hPa / 1000.0f;
}

static bool loopRunning = false;

// Altitude changed (Settings page, settings file) or boot. Web handlers run
// on the other core: once the loop runs, it re-selects within a second.
void updatePatmFromFMTAltitude() {
  if (!loopRunning) selectAtmosphericPressure(millis());
}

void atmosphericPressureBegin() {
  selectAtmosphericPressure(millis());
}

void setAtmosphericPressureFromPacket(const char *payload) {
  char *end = nullptr;
  const float hPa = payload ? strtof(payload, &end) : NAN;
  if (!payload || end == payload || *end != '\0' || !plausible(hPa)) return;
  portENTER_CRITICAL(&sidekickMux);
  sidekickSeries.add(millis(), hPa);
  portEXIT_CRITICAL(&sidekickMux);
}

void atmosphericPressureProcess() {
  static unsigned long lastSelect = 0;
  loopRunning = true;
  const unsigned long now = millis();
  if (now - lastSelect < PATM_SELECT_MS) return;
  lastSelect = now;
  selectAtmosphericPressure(now);
}

// Two lines: the value in use (and a fallback warning), then each source.
void appendAtmosphericPressureStatus(char *st, size_t size) {
  char when[20] = "";
  const bool fallback = fallbackEpoch != 0 || selectedSource > bestSourceSeen;
  if (fallback) formatLocalEpochISO(fallbackEpoch, when, sizeof(when));
  char line[200];
  snprintf(line, sizeof(line), "Atmospheric pressure: %.2f hPa (%s)%s%s %s<br>",
           sourceState[selectedSource].hPa, atmosphericPressureSourceLabel(),
           fallback ? "; <b>Warning:</b> fell back from " : "",
           fallback ? atmosphericPressureSourceLabel(fallbackFrom) : "", when);
  strlcat(st, line, size);
  // "1013.25 hPa 4 s" (valid), "INVALID 1013.25 hPa 700 s" or "n/a".
  char text[32];
  const PatmSourceState &state = sourceState[PATM_SOURCE_SIDEKICK];
  if (!isfinite(state.hPa)) snprintf(text, sizeof(text), "n/a");
  else snprintf(text, sizeof(text), "%s%.2f hPa %lu s", state.valid ? "" : "INVALID ", state.hPa, state.ageMs / 1000UL);
  snprintf(line, sizeof(line), "&nbsp;&nbsp;&nbsp;&nbsp;sidekick %s; altitude %.2f hPa (%.0f m)<br>",
           text, sourceState[PATM_SOURCE_ALTITUDE].hPa, FMTData.FMTAltitude);
  strlcat(st, line, size);
}
