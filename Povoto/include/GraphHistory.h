#ifndef POVOTO_GRAPH_HISTORY_H
#define POVOTO_GRAPH_HISTORY_H

#include <Arduino.h>

// One observation every 15 minutes, for at most 62 days. Epoch is the local
// epoch returned by NTPEpoch(), as used by the rest of Povoto.
constexpr uint32_t GRAPH_HISTORY_INTERVAL_SECONDS = 15UL * 60UL;
constexpr uint16_t GRAPH_HISTORY_CAPACITY = 62U * 24U * 4U;
constexpr uint16_t GRAPH_HISTORY_DEMO_POINTS = 14U * 24U * 4U + 1U;

enum GraphHistoryFlags : uint32_t {
  GRAPH_RATE_HELD = 1U << 0,
  GRAPH_RATE_TRANSITION = 1U << 1,
  GRAPH_SYNTHETIC = 1U << 2,
};

// Unavailable numeric values are NAN, never zero or NOTaTEMP.
struct GraphHistoryPoint {
  uint32_t epoch;
  float sg;
  float temperature;
  float temperatureSetpoint;
  float pressure;
  float pressureSetpoint;
  float co2Rate;
  float abv;
  uint32_t flags;
};

// Call only after povotoFilesystemBegin(). A missing or damaged individual record is
// ignored on restore; good records remain available.
bool graphHistoryBegin();
void graphHistorySampleIfDue();
void graphHistoryResetForNewBatch();
uint16_t graphHistoryCount();
bool graphHistoryGetPoint(uint16_t chronologicalIndex, GraphHistoryPoint &point);
// Copies a stable chronological snapshot for exports under one lock.
// Returns the number copied (at most capacity).
uint16_t graphHistoryCopyPoints(GraphHistoryPoint *destination, uint16_t capacity);
// Debug-only replacement of the single persisted series, from 14 days ago
// through now. Requires valid NTP; future real samples append normally.
bool graphHistoryGenerateDemo14Days();

#endif
