#include "GraphHistory.h"

#include <LittleFS.h>
#include <IOTK_NTP.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "PovotoData.h"
#include "PressureControl.h"
#include "PovotoCommon.h"

namespace {
constexpr char HISTORY_FILE[] = "/graph-history.bin";
constexpr char HISTORY_TEMP[] = "/graph-history.tmp";
constexpr char HISTORY_BACKUP[] = "/graph-history.bak";
constexpr uint32_t HISTORY_MAGIC = 0x31484750UL; // PGH1
constexpr uint32_t MIN_VALID_EPOCH = 1577836800UL; // 2020-01-01
constexpr uint32_t MAX_AGE_SECONDS = 62UL * 24UL * 60UL * 60UL;

struct HistoryRecord {
  uint32_t magic;
  uint32_t batchKey;
  GraphHistoryPoint point;
  uint32_t checksum;
};
static_assert(sizeof(GraphHistoryPoint) == 36, "Unexpected graph point layout");
static_assert(sizeof(HistoryRecord) == 48, "Unexpected graph record layout");

GraphHistoryPoint *points = nullptr;
SemaphoreHandle_t historyMutex = nullptr;
uint16_t first = 0;
uint16_t count = 0;
uint16_t nextFileSlot = 0;
uint32_t lastEpoch = 0;
uint32_t batchKey = 0;
bool ready = false;

float smoothStep(float value) {
  if (value <= 0.0f) return 0.0f;
  if (value >= 1.0f) return 1.0f;
  return value * value * (3.0f - 2.0f * value);
}

uint32_t fnv1a(const uint8_t *data, size_t length, uint32_t hash = 2166136261UL) {
  for (size_t i = 0; i < length; ++i) hash = (hash ^ data[i]) * 16777619UL;
  return hash;
}

uint32_t currentBatchKey() {
  // Batch date/name may be corrected while a batch is running. A deliberate
  // new-batch operation clears the file even if the number is reused.
  return fnv1a(reinterpret_cast<const uint8_t *>(&BatchData.batchNumber),
               sizeof(BatchData.batchNumber));
}

uint32_t recordChecksum(const HistoryRecord &record) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&record),
               offsetof(HistoryRecord, checksum));
}

int comparePoints(const void *a, const void *b) {
  const uint32_t left = static_cast<const GraphHistoryPoint *>(a)->epoch;
  const uint32_t right = static_cast<const GraphHistoryPoint *>(b)->epoch;
  return (left > right) - (left < right);
}

void appendPoint(const GraphHistoryPoint &point) {
  if (count == GRAPH_HISTORY_CAPACITY) {
    first = (first + 1) % GRAPH_HISTORY_CAPACITY;
    --count;
  }
  points[(first + count) % GRAPH_HISTORY_CAPACITY] = point;
  ++count;
}

void discardExpired(uint32_t now) {
  while (count && now > points[first].epoch &&
         now - points[first].epoch > MAX_AGE_SECONDS) {
    first = (first + 1) % GRAPH_HISTORY_CAPACITY;
    --count;
  }
}

float validTemperature(float value) {
  return isfinite(value) && value != NOTaTEMP && value != 85.0f ? value : NAN;
}

GraphHistoryPoint capturePoint(uint32_t epoch) {
  GraphHistoryPoint point = {};
  point.epoch = epoch;
  point.sg = isfinite(beerSG) && beerSG > 0.8f && beerSG < 1.2f ? beerSG : NAN;
  point.temperature = validTemperature(ControlData.temperature);
  point.temperatureSetpoint = validTemperature(SetPointData.setPointTemp);
  point.pressure = pressureReadingValid() && isfinite(ControlData.pressure)
      ? ControlData.pressure : NAN;
  point.pressureSetpoint = isfinite(SetPointData.setPointPressure) &&
      SetPointData.setPointPressure >= 0.0f ? SetPointData.setPointPressure : NAN;
  point.abv = isfinite(beerABV) && beerABV >= 0.0f && beerABV < 30.0f
      ? beerABV : NAN;
  point.co2Rate = NAN;
  // Exactly the same inclusion rule as Brewfather's bpm field.
  const char *source = getCO2EvolutionSource();
  if (strcmp(source, "held") == 0) point.flags |= GRAPH_RATE_HELD;
  if (strcmp(source, "transition") == 0) point.flags |= GRAPH_RATE_TRANSITION;
  const float rate = getBeerCO2EvolutionGramsPerLiterPerDay();
  if (isfinite(rate) && rate > 0.0f && !co2RateInTransition()) point.co2Rate = rate;
  return point;
}
} // namespace

bool graphHistoryBegin() {
  if (ready) return true;
  if (!psramFound()) {
    Serial.println("[GRAPH] PSRAM unavailable; history disabled");
    return false;
  }
  points = static_cast<GraphHistoryPoint *>(heap_caps_malloc(
      sizeof(GraphHistoryPoint) * GRAPH_HISTORY_CAPACITY,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  historyMutex = xSemaphoreCreateMutex();
  if (!points || !historyMutex) {
    if (points) heap_caps_free(points);
    if (historyMutex) vSemaphoreDelete(historyMutex);
    points = nullptr;
    historyMutex = nullptr;
    Serial.println("[GRAPH] Cannot allocate history in PSRAM");
    return false;
  }

  batchKey = currentBatchKey();
  // If power failed between moving the old file aside and publishing the
  // generated history, restore the old file. A completed replacement wins.
  if (!LittleFS.exists(HISTORY_FILE) && LittleFS.exists(HISTORY_BACKUP))
    LittleFS.rename(HISTORY_BACKUP, HISTORY_FILE);
  if (LittleFS.exists(HISTORY_FILE) && LittleFS.exists(HISTORY_BACKUP))
    LittleFS.remove(HISTORY_BACKUP);
  if (LittleFS.exists(HISTORY_TEMP)) LittleFS.remove(HISTORY_TEMP);
  File file = LittleFS.open(HISTORY_FILE, "r");
  uint32_t newestEpoch = 0;
  if (file) {
    const size_t storedSlots = file.size() / sizeof(HistoryRecord);
    const size_t fileSlots = storedSlots < GRAPH_HISTORY_CAPACITY
        ? storedSlots : GRAPH_HISTORY_CAPACITY;
    for (size_t slot = 0; slot < fileSlots; ++slot) {
      HistoryRecord record;
      if (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record)) break;
      if (record.magic != HISTORY_MAGIC || record.batchKey != batchKey ||
          record.checksum != recordChecksum(record) ||
          record.point.epoch < MIN_VALID_EPOCH) continue;
      appendPoint(record.point);
      if (record.point.epoch >= newestEpoch) {
        newestEpoch = record.point.epoch;
        nextFileSlot = (slot + 1) % GRAPH_HISTORY_CAPACITY;
      }
    }
    file.close();
  }
  if (count) {
    // Disk slots are circular; the public view is always chronological.
    qsort(points, count, sizeof(GraphHistoryPoint), comparePoints);
    first = 0;
    lastEpoch = points[count - 1].epoch;
    const uint32_t now = NTPEpoch();
    if (now >= MIN_VALID_EPOCH) discardExpired(now);
  }
  ready = true;
  Serial.printf("[GRAPH] restored %u points, PSRAM %u bytes, LittleFS %u/%u bytes\n",
                count, unsigned(sizeof(GraphHistoryPoint) * GRAPH_HISTORY_CAPACITY),
                unsigned(LittleFS.usedBytes()), unsigned(LittleFS.totalBytes()));
  return true;
}

void graphHistorySampleIfDue() {
  if (!ready || SetPointData.mode != MODE_FERMENTING) return;
  const uint32_t epoch = NTPEpoch();
  if (epoch < MIN_VALID_EPOCH || xSemaphoreTake(historyMutex, 0) != pdTRUE) return;
  if (currentBatchKey() != batchKey) {
    xSemaphoreGive(historyMutex);
    graphHistoryResetForNewBatch();
    return;
  }
  if (lastEpoch && epoch / GRAPH_HISTORY_INTERVAL_SECONDS <=
                       lastEpoch / GRAPH_HISTORY_INTERVAL_SECONDS) {
    xSemaphoreGive(historyMutex);
    return;
  }
  // Do not create a new, incomplete file over a recoverable pre-demo backup.
  if (!LittleFS.exists(HISTORY_FILE) && LittleFS.exists(HISTORY_BACKUP) &&
      !LittleFS.rename(HISTORY_BACKUP, HISTORY_FILE)) {
    xSemaphoreGive(historyMutex);
    return;
  }

  HistoryRecord record = {};
  record.magic = HISTORY_MAGIC;
  record.batchKey = batchKey;
  record.point = capturePoint(epoch);
  record.checksum = recordChecksum(record);
  File file = LittleFS.open(HISTORY_FILE, LittleFS.exists(HISTORY_FILE) ? "r+" : "w+");
  bool saved = file && file.seek(size_t(nextFileSlot) * sizeof(record)) &&
               file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  if (file) {
    file.flush();
    file.close();
  }
  if (saved) {
    appendPoint(record.point);
    discardExpired(epoch);
    lastEpoch = epoch;
    nextFileSlot = (nextFileSlot + 1) % GRAPH_HISTORY_CAPACITY;
  } else {
    static uint32_t lastWarning = 0;
    if (millis() - lastWarning > 60000UL) {
      Serial.println("[GRAPH] LittleFS write failed; sample will be retried");
      lastWarning = millis();
    }
  }
  xSemaphoreGive(historyMutex);
}

void graphHistoryResetForNewBatch() {
  if (!ready || xSemaphoreTake(historyMutex, portMAX_DELAY) != pdTRUE) return;
  first = count = nextFileSlot = 0;
  lastEpoch = 0;
  batchKey = currentBatchKey();
  if (!LittleFS.remove(HISTORY_FILE) && LittleFS.exists(HISTORY_FILE))
    Serial.println("[GRAPH] Could not remove previous batch history");
  LittleFS.remove(HISTORY_TEMP);
  LittleFS.remove(HISTORY_BACKUP);
  xSemaphoreGive(historyMutex);
}

uint16_t graphHistoryCount() {
  if (!ready || xSemaphoreTake(historyMutex, portMAX_DELAY) != pdTRUE) return 0;
  const uint32_t now = NTPEpoch();
  if (now >= MIN_VALID_EPOCH) discardExpired(now);
  const uint16_t result = count;
  xSemaphoreGive(historyMutex);
  return result;
}

bool graphHistoryGetPoint(uint16_t chronologicalIndex, GraphHistoryPoint &point) {
  if (!ready || xSemaphoreTake(historyMutex, portMAX_DELAY) != pdTRUE) return false;
  const uint32_t now = NTPEpoch();
  if (now >= MIN_VALID_EPOCH) discardExpired(now);
  const bool exists = chronologicalIndex < count;
  if (exists) point = points[(first + chronologicalIndex) % GRAPH_HISTORY_CAPACITY];
  xSemaphoreGive(historyMutex);
  return exists;
}

uint16_t graphHistoryCopyPoints(GraphHistoryPoint *destination, uint16_t capacity) {
  if (!ready || !destination || capacity == 0 ||
      xSemaphoreTake(historyMutex, portMAX_DELAY) != pdTRUE) return 0;
  const uint32_t now = NTPEpoch();
  if (now >= MIN_VALID_EPOCH) discardExpired(now);
  const uint16_t copied = count < capacity ? count : capacity;
  for (uint16_t i = 0; i < copied; ++i)
    destination[i] = points[(first + i) % GRAPH_HISTORY_CAPACITY];
  xSemaphoreGive(historyMutex);
  return copied;
}

bool graphHistoryGenerateDemo14Days() {
  if (!ready) return false;
  const uint32_t now = NTPEpoch();
  if (now < MIN_VALID_EPOCH) return false;
  if (xSemaphoreTake(historyMutex, portMAX_DELAY) != pdTRUE) return false;
  GraphHistoryPoint *generated = static_cast<GraphHistoryPoint *>(heap_caps_malloc(
      sizeof(GraphHistoryPoint) * GRAPH_HISTORY_DEMO_POINTS,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!generated) {
    xSemaphoreGive(historyMutex);
    return false;
  }

  const uint32_t firstEpoch = now - 14UL * 24UL * 60UL * 60UL;
  constexpr float PI2 = 6.28318530718f;
  constexpr float OG = 1.054f;
  for (uint16_t i = 0; i < GRAPH_HISTORY_DEMO_POINTS; ++i) {
    const float day = float(i) / 96.0f;
    const float dailyPhase = PI2 * day;
    GraphHistoryPoint &point = generated[i];
    point = {};
    point.epoch = firstEpoch + uint32_t(i) * GRAPH_HISTORY_INTERVAL_SECONDS;
    point.flags = GRAPH_SYNTHETIC;

    // Slow start, vigorous fermentation, then a progressively flatter tail.
    point.sg = OG - 0.004f * smoothStep((day - 0.3f) / 1.6f)
                  - 0.032f * smoothStep((day - 1.3f) / 8.8f)
                  - 0.007f * smoothStep((day - 7.0f) / 7.0f);
    point.abv = 100.0f * (105.0f * (OG - point.sg) / (100.0f - point.sg)
                          * (point.sg / 0.79f));

    // A raised temperature target near the end, with circadian and control
    // oscillations that make actual temperature visually distinguishable.
    point.temperatureSetpoint = day < 8.0f ? 19.0f : (day < 12.5f ? 20.0f : 21.0f);
    point.temperature = 19.0f + 1.0f * smoothStep((day - 8.0f) / 0.6f)
                        + 1.0f * smoothStep((day - 12.5f) / 0.6f)
                        + 0.14f * sinf(dailyPhase - 0.5f)
                        + 0.06f * sinf(PI2 * day * 5.0f);

    // Gradual spunding rise and a late pressure-target step as in the example.
    point.pressureSetpoint = 0.20f + 0.90f * smoothStep((day - 0.7f) / 7.8f)
                             + 0.55f * smoothStep((day - 13.25f) / 0.35f);
    point.pressure = point.pressureSetpoint
                     - 0.08f * expf(-day / 1.0f)
                     + 0.035f * sinf(PI2 * day * 1.7f);

    // Positive rate: one broad active-fermentation maximum and a small tail.
    const float mainPeak = (day - 4.5f) / 2.8f;
    const float tailPeak = (day - 9.0f) / 1.6f;
    point.co2Rate = 0.35f + 12.0f * expf(-0.5f * mainPeak * mainPeak)
                    + 1.2f * expf(-0.5f * tailPeak * tailPeak);
    // Mirror Brewfather's omission during temperature/pressure transitions.
    if ((day >= 8.0f && day < 8.7f) ||
        (day >= 12.5f && day < 13.2f) ||
        (day >= 13.25f && day < 13.7f)) {
      point.co2Rate = NAN;
      point.flags |= GRAPH_RATE_TRANSITION;
    }
  }

  // Write a complete replacement before touching the current history.
  File file = LittleFS.open(HISTORY_TEMP, "w");
  bool saved = file;
  for (uint16_t i = 0; saved && i < GRAPH_HISTORY_DEMO_POINTS; ++i) {
    HistoryRecord record = {};
    record.magic = HISTORY_MAGIC;
    record.batchKey = batchKey;
    record.point = generated[i];
    record.checksum = recordChecksum(record);
    saved = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
    if ((i & 63U) == 0U) vTaskDelay(1);
  }
  if (file) {
    file.flush();
    file.close();
  }
  saved = saved && LittleFS.exists(HISTORY_TEMP);
  if (saved) {
    File check = LittleFS.open(HISTORY_TEMP, "r");
    saved = check && check.size() == size_t(GRAPH_HISTORY_DEMO_POINTS) * sizeof(HistoryRecord);
    if (check) check.close();
  }
  const bool hadHistory = LittleFS.exists(HISTORY_FILE);
  if (saved && LittleFS.exists(HISTORY_BACKUP)) saved = LittleFS.remove(HISTORY_BACKUP);
  if (saved && hadHistory) saved = LittleFS.rename(HISTORY_FILE, HISTORY_BACKUP);
  if (saved) saved = LittleFS.rename(HISTORY_TEMP, HISTORY_FILE);
  if (!saved && hadHistory && LittleFS.exists(HISTORY_BACKUP) &&
      !LittleFS.exists(HISTORY_FILE))
    LittleFS.rename(HISTORY_BACKUP, HISTORY_FILE);
  if (saved) {
    memcpy(points, generated, sizeof(GraphHistoryPoint) * GRAPH_HISTORY_DEMO_POINTS);
    first = 0;
    count = GRAPH_HISTORY_DEMO_POINTS;
    lastEpoch = generated[GRAPH_HISTORY_DEMO_POINTS - 1].epoch;
    nextFileSlot = GRAPH_HISTORY_DEMO_POINTS;
    if (hadHistory) LittleFS.remove(HISTORY_BACKUP);
  } else {
    LittleFS.remove(HISTORY_TEMP);
    Serial.println("[GRAPH] Demo generation failed; original history kept");
  }
  heap_caps_free(generated);
  xSemaphoreGive(historyMutex);
  return saved;
}
