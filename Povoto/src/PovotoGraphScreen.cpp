#include "PovotoGraphScreen.h"

#include <TFT_eWidget.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <IOTK_NTP.h>

#include "GraphHistory.h"
#include "PovotoData.h"
#include "Povoto_UI.h"
#include "displayUtils.h"
#include "Swiss_911_Extra_Compressed_Regular12pt7b.h"

namespace {
constexpr int16_t SCREEN_W = 480;
constexpr int16_t SCREEN_H = 320;
constexpr int16_t SPRITE_X = 0;
constexpr int16_t SPRITE_Y = 37;
constexpr int16_t SPRITE_W = SCREEN_W;
constexpr int16_t SPRITE_H = SCREEN_H - SPRITE_Y;
constexpr int16_t PLOT_X = 46;
constexpr int16_t PLOT_Y = 30;
constexpr int16_t PLOT_W = 420;
constexpr int16_t ATTENUATION_PLOT_W = 375;
constexpr int16_t PLOT_H = 225;
constexpr int16_t HEADER_Y = 7;
constexpr int16_t HEADER_H = 26;
constexpr int16_t BATCH_RIGHT = 294;
constexpr int16_t TAB_X = 297;
constexpr int16_t TAB_STEP = 43;
constexpr int16_t TAB_W = 42;
constexpr uint32_t REFRESH_MS = 15000UL;

using View = GraphScreenView;
enum class EvolutionSeries : uint8_t { SG, Temperature, Pressure, CO2Rate };

bool active = false;
View view = View::Temperature;
EvolutionSeries evolutionSeries = EvolutionSeries::SG;
GraphHistoryPoint *snapshot = nullptr;
uint16_t pointCount = 0;
uint16_t viewFirst = 0;
uint16_t viewLast = 0;
uint16_t snapshotBatch = 0;
char snapshotName[sizeof(BatchData.batchName)] = "";
float snapshotOG = NAN;
uint32_t lastRefreshAt = 0;
uint32_t lastTouchAt = 0;
bool touchHeld = false;

uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
  return tft.color565(r, g, b);
}

int16_t plotWidth() {
  return view == View::Attenuation ? ATTENUATION_PLOT_W : PLOT_W;
}

void drawBars() {
  const uint16_t amber = color(255, 199, 125);
  const uint16_t tabColors[4] = {
    color(255, 199, 125), color(247, 228, 153),
    color(238, 183, 210), color(194, 191, 247)
  };

  tft.fillScreen(TFT_BLACK);
  // Batch capsule: amber number, dark divider and a lavender name field.
  tft.fillRoundRect(8, HEADER_Y, BATCH_RIGHT - 8, HEADER_H, HEADER_H / 2,
                    color(151, 126, 236));
  tft.fillRoundRect(8, HEADER_Y, 50, HEADER_H, HEADER_H / 2, amber);
  tft.fillRect(21, HEADER_Y, 37, HEADER_H, amber);
  for (int16_t x = 59; x < BATCH_RIGHT - HEADER_H / 2; ++x) {
    const uint8_t blend = uint8_t((uint32_t(x - 59) * 255) /
                                  (BATCH_RIGHT - HEADER_H / 2 - 59));
    tft.drawFastVLine(x, HEADER_Y, HEADER_H,
      color(125 + (uint16_t(26) * blend) / 255,
            103 + (uint16_t(23) * blend) / 255,
            187 + (uint16_t(49) * blend) / 255));
  }
  tft.fillRect(55, HEADER_Y, 4, HEADER_H, TFT_BLACK);

  char numberLabel[16];
  snprintf(numberLabel, sizeof(numberLabel), "%04u", unsigned(BatchData.batchNumber));
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular12pt7b);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(numberLabel, 33, HEADER_Y + HEADER_H / 2);

  char batchName[sizeof(BatchData.batchName)];
  strncpy(batchName, BatchData.batchName, sizeof(batchName) - 1);
  batchName[sizeof(batchName) - 1] = '\0';
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular12pt7b);
  tft.setTextColor(color(220, 222, 231));
  size_t nameLength = strlen(batchName);
  while (nameLength && tft.textWidth(batchName) > 225)
    batchName[--nameLength] = '\0';
  tft.setTextDatum(ML_DATUM);
  tft.drawString(batchName, 65, HEADER_Y + HEADER_H / 2 - 1);

  const char *labels[4] = {"EVOL", "TEMP", "PRESS", "ATTN"};
  const View tabs[4] = {View::Evolution, View::Temperature,
                        View::Pressure, View::Attenuation};
  tft.setTextDatum(MC_DATUM);
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t x = TAB_X + TAB_STEP * i;
    const bool selected = tabs[i] == view;
    const uint16_t fill = tabColors[i];
    if (i == 0) {
      tft.fillRoundRect(x, HEADER_Y, TAB_W, HEADER_H, HEADER_H / 2, fill);
      tft.fillRect(x + HEADER_H / 2, HEADER_Y,
                   TAB_W - HEADER_H / 2, HEADER_H, fill);
    } else {
      tft.fillRect(x, HEADER_Y, TAB_W, HEADER_H, fill);
    }
    tft.setFreeFont(nullptr);
    tft.setTextFont(1);
    tft.setTextColor(TFT_BLACK);
    tft.drawString(labels[i], x + TAB_W / 2, HEADER_Y + HEADER_H / 2);
    if (selected)
      tft.fillRect(x + 6, HEADER_Y + HEADER_H - 3, TAB_W - 12, 2, TFT_BLACK);
  }
  // Separate rounded end cap; leave a black gap after the last button.
  const int16_t capX = TAB_X + 3 * TAB_STEP + TAB_W + 2;
  const int16_t capW = SCREEN_W - capX;
  const uint16_t capColor = color(242, 163, 171);
  tft.fillRoundRect(capX, HEADER_Y, capW, HEADER_H, capW / 2, capColor);
  tft.fillRect(capX, HEADER_Y, capW / 2, HEADER_H, capColor);

}

void formatTime(uint32_t epoch, char *out, size_t length) {
  if (!epoch) {
    snprintf(out, length, "--/-- --:--");
    return;
  }
  unsigned long day;
  int8_t weekday, hour, minute, second, date, month;
  int16_t year;
  convertFromEpoch(epoch, day, weekday, hour, minute, second, date, month, year);
  snprintf(out, length, "%02d/%02d %02d:%02d", int(date), int(month),
           int(hour), int(minute));
}

void refreshSnapshot(bool resetZoom) {
  const uint16_t previousCount = pointCount;
  const bool wasAtEnd = previousCount && viewLast == previousCount - 1;
  if (!snapshot) {
    snapshot = static_cast<GraphHistoryPoint *>(heap_caps_malloc(
        size_t(GRAPH_HISTORY_CAPACITY) * sizeof(GraphHistoryPoint),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  pointCount = snapshot ? graphHistoryCopyPoints(snapshot, GRAPH_HISTORY_CAPACITY) : 0;
  snapshotBatch = BatchData.batchNumber;
  strncpy(snapshotName, BatchData.batchName, sizeof(snapshotName) - 1);
  snapshotName[sizeof(snapshotName) - 1] = '\0';
  snapshotOG = BatchData.batchOG;
  if (!pointCount) {
    viewFirst = viewLast = 0;
  } else if (resetZoom || !previousCount) {
    viewFirst = 0;
    viewLast = pointCount - 1;
  } else {
    if (viewFirst >= pointCount) viewFirst = pointCount - 1;
    if (wasAtEnd || viewLast >= pointCount) viewLast = pointCount - 1;
    if (viewLast < viewFirst) viewFirst = viewLast;
  }
  lastRefreshAt = millis();
}

uint16_t columnFor(uint32_t epoch, uint32_t firstEpoch, uint32_t lastEpoch,
                   int16_t width) {
  if (lastEpoch <= firstEpoch) return 0;
  const uint32_t clamped = epoch < firstEpoch ? firstEpoch
                           : epoch > lastEpoch ? lastEpoch : epoch;
  return uint16_t((uint64_t(clamped - firstEpoch) * (width - 1)) /
                  (lastEpoch - firstEpoch));
}

enum class SeriesField : uint8_t {
  Temperature, TemperatureTarget, Pressure, PressureTarget, SG, ABV, CO2Rate
};

SeriesField evolutionField(EvolutionSeries series) {
  switch (series) {
    case EvolutionSeries::SG: return SeriesField::SG;
    case EvolutionSeries::Temperature: return SeriesField::Temperature;
    case EvolutionSeries::Pressure: return SeriesField::Pressure;
    case EvolutionSeries::CO2Rate: return SeriesField::CO2Rate;
  }
  return SeriesField::SG;
}

uint16_t evolutionColor(EvolutionSeries series) {
  switch (series) {
    case EvolutionSeries::SG: return color(240, 101, 101);
    case EvolutionSeries::Temperature: return color(117, 148, 237);
    case EvolutionSeries::Pressure: return color(255, 150, 87);
    case EvolutionSeries::CO2Rate: return color(136, 199, 121);
  }
  return color(240, 101, 101);
}

float seriesValue(const GraphHistoryPoint &point, SeriesField field) {
  switch (field) {
    case SeriesField::Temperature: return point.temperature;
    case SeriesField::TemperatureTarget: return point.temperatureSetpoint;
    case SeriesField::Pressure: return point.pressure;
    case SeriesField::PressureTarget: return point.pressureSetpoint;
    case SeriesField::SG: return point.sg;
    case SeriesField::ABV: return point.abv;
    case SeriesField::CO2Rate: return point.co2Rate;
  }
  return NAN;
}

void drawSeries(GraphWidget &graph, SeriesField field, bool stepped,
                uint16_t stroke, uint32_t firstEpoch, uint32_t lastEpoch,
                int16_t width) {
  TraceWidget trace(&graph);
  trace.startTrace(stroke);
  int16_t column = -1;
  float minimum = 0.0f, maximum = 0.0f, last = 0.0f;
  bool have = false;
  bool previousTarget = false;
  int16_t previousColumn = 0;
  float previousValue = 0.0f;

  auto flush = [&]() {
    if (!have) return;
    if (stepped) {
      if (previousTarget) {
        graph.addLine(previousColumn, previousValue, column, previousValue, stroke);
        graph.addLine(column, previousValue, column, last, stroke);
      } else {
        graph.addLine(column, last, column, last, stroke);
      }
      previousTarget = true;
      previousColumn = column;
      previousValue = last;
    } else {
      trace.addPoint(column, last);
    }
    // Preserve short spikes even when several observations map to one pixel.
    graph.addLine(column, minimum, column, maximum, stroke);
  };

  for (uint16_t i = viewFirst; i <= viewLast; ++i) {
    const GraphHistoryPoint &point = snapshot[i];
    const float value = seriesValue(point, field);
    if (!isfinite(value)) continue;
    const int16_t x = columnFor(point.epoch, firstEpoch, lastEpoch, width);
    if (x != column) {
      flush();
      column = x;
      have = false;
    }
    if (!have) {
      minimum = maximum = value;
      have = true;
    } else {
      if (value < minimum) minimum = value;
      if (value > maximum) maximum = value;
    }
    last = value;
  }
  flush();
}

void includeValue(float value, float &low, float &high) {
  if (!isfinite(value)) return;
  low = fminf(low, value);
  high = fmaxf(high, value);
}

void padScale(float &low, float &high, float minimumPadding, float quantum,
              bool nonnegative = false) {
  const float padding = fmaxf(minimumPadding, (high - low) * 0.12f);
  low = floorf((low - padding) / quantum) * quantum;
  high = ceilf((high + padding) / quantum) * quantum;
  if (nonnegative) low = fmaxf(0.0f, low);
  if (high <= low) high = low + quantum;
}

void padEvolutionScale(EvolutionSeries series, float &low, float &high) {
  switch (series) {
    case EvolutionSeries::SG: padScale(low, high, 0.001f, 0.001f); break;
    case EvolutionSeries::Temperature: padScale(low, high, 0.5f, 0.5f); break;
    case EvolutionSeries::Pressure: padScale(low, high, 0.10f, 0.05f); break;
    case EvolutionSeries::CO2Rate: padScale(low, high, 0.10f, 0.1f, true); break;
  }
}

const char *evolutionAxisFormat(EvolutionSeries series) {
  return series == EvolutionSeries::SG ? "%.3f" :
         series == EvolutionSeries::Pressure ? "%.2f" : "%.1f";
}

void drawLegend(TFT_eSprite &sprite, int16_t x, uint16_t stroke,
                const char *label) {
  sprite.fillRect(x, 9, 14, 3, stroke);
  sprite.drawString(label, x + 19, 2);
}

void drawChart() {
  static TFT_eSprite sprite(&tft);
  static bool spriteAttempted = false;
  static bool spriteReady = false;
  if (!spriteAttempted) {
    spriteAttempted = true;
    sprite.setColorDepth(16);
    spriteReady = sprite.createSprite(SPRITE_W, SPRITE_H) != nullptr;
  }
  if (!spriteReady) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Graph memory unavailable", SCREEN_W / 2, 160, 2);
    return;
  }
  sprite.fillSprite(TFT_BLACK);
  sprite.setTextFont(2);
  sprite.setTextDatum(TL_DATUM);
  const bool temperature = view == View::Temperature;
  const bool pressure = view == View::Pressure;
  const bool attenuation = view == View::Attenuation;
  const bool evolution = view == View::Evolution;
  const int16_t width = plotWidth();
  const uint16_t actualColor = evolution ? evolutionColor(evolutionSeries)
                                : temperature ? color(255, 204, 102)
                                : pressure ? color(255, 150, 87)
                                           : color(240, 101, 101);
  const uint16_t targetColor = temperature ? color(164, 160, 246)
                                : pressure ? color(255, 204, 102)
                                           : color(255, 189, 105);
  const uint16_t abvColor = color(136, 199, 121);
  const uint16_t gridColor = color(59, 57, 75);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  if (temperature) {
    sprite.drawString("TEMPERATURE (C)", 8, 2);
    drawLegend(sprite, 165, actualColor, "Actual");
    drawLegend(sprite, 247, targetColor, "Target");
  } else if (pressure) {
    sprite.drawString("PRESSURE (bar)", 8, 2);
    drawLegend(sprite, 165, actualColor, "Actual");
    drawLegend(sprite, 247, targetColor, "Target");
  } else if (attenuation) {
    sprite.drawString("ATTENUATION", 8, 2);
    drawLegend(sprite, 145, actualColor, "SG");
    drawLegend(sprite, 207, targetColor, "OG");
    drawLegend(sprite, 270, abvColor, "ABV (%)");
  } else {
    sprite.drawString("EVOLUTION", 8, 2);
    const char *labels[4] = {"SG", "Temp", "Press", "gCO2/L/d"};
    const int16_t positions[4] = {115, 168, 239, 315};
    for (uint8_t i = 0; i < 4; ++i)
      drawLegend(sprite, positions[i], evolutionColor(EvolutionSeries(i)), labels[i]);
  }

  if (!snapshot) {
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("History memory unavailable", SPRITE_W / 2, SPRITE_H / 2);
    sprite.pushSprite(SPRITE_X, SPRITE_Y);
    return;
  }
  if (!pointCount) {
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("No observations yet", SPRITE_W / 2, SPRITE_H / 2);
    sprite.pushSprite(SPRITE_X, SPRITE_Y);
    return;
  }

  float low = INFINITY, high = -INFINITY;
  float rightLow = INFINITY, rightHigh = -INFINITY;
  float evolutionLow[4] = {INFINITY, INFINITY, INFINITY, INFINITY};
  float evolutionHigh[4] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
  bool evolutionHasData[4] = {false, false, false, false};
  const float og = BatchData.batchOG;
  const bool hasOG = isfinite(og) && og > 0.8f && og < 1.2f;
  for (uint16_t i = viewFirst; i <= viewLast; ++i) {
    const GraphHistoryPoint &point = snapshot[i];
    if (temperature) {
      includeValue(point.temperature, low, high);
      includeValue(point.temperatureSetpoint, low, high);
    } else if (pressure) {
      includeValue(point.pressure, low, high);
      includeValue(point.pressureSetpoint, low, high);
    } else if (attenuation) {
      includeValue(point.sg, low, high);
      includeValue(point.abv, rightLow, rightHigh);
    } else {
      for (uint8_t series = 0; series < 4; ++series)
        includeValue(seriesValue(point, evolutionField(EvolutionSeries(series))),
                     evolutionLow[series], evolutionHigh[series]);
    }
  }
  if (attenuation && hasOG) includeValue(og, low, high);
  bool anyEvolutionData = false;
  if (evolution) {
    for (uint8_t series = 0; series < 4; ++series) {
      evolutionHasData[series] = isfinite(evolutionLow[series]) &&
                                 isfinite(evolutionHigh[series]);
      if (!evolutionHasData[series]) continue;
      anyEvolutionData = true;
      padEvolutionScale(EvolutionSeries(series),
                        evolutionLow[series], evolutionHigh[series]);
    }
    const uint8_t selected = uint8_t(evolutionSeries);
    if (evolutionHasData[selected]) {
      low = evolutionLow[selected];
      high = evolutionHigh[selected];
    }
  }
  const bool hasLeftScale = isfinite(low) && isfinite(high);
  const bool hasRightScale = isfinite(rightLow) && isfinite(rightHigh);
  if (!hasLeftScale && !hasRightScale && !anyEvolutionData) {
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("No valid observations", SPRITE_W / 2, SPRITE_H / 2);
    sprite.pushSprite(SPRITE_X, SPRITE_Y);
    return;
  }
  if (hasLeftScale && !evolution) {
    if (temperature)
      padScale(low, high, 0.5f, 0.5f);
    else if (pressure)
      padScale(low, high, 0.10f, 0.05f);
    else
      padScale(low, high, 0.001f, 0.001f);
  } else if (!hasLeftScale) {
    low = 0.0f;
    high = 1.0f;
  }
  if (hasRightScale) padScale(rightLow, rightHigh, 0.5f, 0.5f, true);

  GraphWidget leftGraph(&sprite);
  leftGraph.createGraph(width - 1, PLOT_H - 1, TFT_BLACK);
  leftGraph.setGraphScale(0.0f, float(width - 1), low, high);
  leftGraph.setGraphPosition(PLOT_X, PLOT_Y);
  GraphWidget rightGraph(&sprite);
  if (attenuation && hasRightScale) {
    rightGraph.createGraph(width - 1, PLOT_H - 1, TFT_BLACK);
    rightGraph.setGraphScale(0.0f, float(width - 1), rightLow, rightHigh);
    rightGraph.setGraphPosition(PLOT_X, PLOT_Y);
  }
  sprite.fillRect(PLOT_X, PLOT_Y, width, PLOT_H, TFT_BLACK);
  sprite.setTextColor(evolution ? actualColor : color(202, 200, 217), TFT_BLACK);
  sprite.setTextDatum(MR_DATUM);
  const char *axisFormat = evolution ? evolutionAxisFormat(evolutionSeries) :
      attenuation ? "%.3f" : pressure ? "%.2f" : "%.1f";
  for (uint8_t tick = 0; tick <= 4; ++tick) {
    const float value = low + (high - low) * tick / 4.0f;
    const int16_t y = leftGraph.getPointY(value);
    leftGraph.addLine(0, value, width - 1, value, gridColor);
    if (!hasLeftScale) continue;
    char label[16];
    snprintf(label, sizeof(label), axisFormat, value);
    sprite.drawString(label, PLOT_X - 5, y);
  }
  for (uint8_t tick = 1; tick < 4; ++tick) {
    const int16_t x = PLOT_X + (width - 1) * tick / 4;
    sprite.drawFastVLine(x, PLOT_Y, PLOT_H, gridColor);
  }
  if (attenuation && hasRightScale) {
    sprite.setTextColor(abvColor, TFT_BLACK);
    sprite.setTextDatum(ML_DATUM);
    for (uint8_t tick = 0; tick <= 4; ++tick) {
      const float value = rightLow + (rightHigh - rightLow) * tick / 4.0f;
      char label[16];
      snprintf(label, sizeof(label), "%.1f", value);
      sprite.drawString(label, PLOT_X + width + 5, rightGraph.getPointY(value));
    }
  }

  const uint32_t firstEpoch = snapshot[viewFirst].epoch;
  const uint32_t lastEpoch = snapshot[viewLast].epoch;
  if (temperature) {
    drawSeries(leftGraph, SeriesField::TemperatureTarget, true, targetColor,
               firstEpoch, lastEpoch, width);
    drawSeries(leftGraph, SeriesField::Temperature, false, actualColor,
               firstEpoch, lastEpoch, width);
  } else if (pressure) {
    drawSeries(leftGraph, SeriesField::PressureTarget, true, targetColor,
               firstEpoch, lastEpoch, width);
    drawSeries(leftGraph, SeriesField::Pressure, false, actualColor,
               firstEpoch, lastEpoch, width);
  } else if (attenuation) {
    if (hasOG) {
      for (int16_t x = 0; x < width - 1; x += 11) {
        const int16_t end = x + 5 < width - 1 ? x + 5 : width - 1;
        leftGraph.addLine(x, og, end, og, targetColor);
      }
    }
    drawSeries(leftGraph, SeriesField::SG, false, actualColor,
               firstEpoch, lastEpoch, width);
    if (hasRightScale)
      drawSeries(rightGraph, SeriesField::ABV, false, abvColor,
                 firstEpoch, lastEpoch, width);
  } else {
    // Keep a fixed layer order so changing the visible axis needs no trace redraw.
    const uint8_t layerOrder[4] = {1, 2, 3, 0};
    for (uint8_t layer = 0; layer < 4; ++layer) {
      const uint8_t series = layerOrder[layer];
      if (!evolutionHasData[series]) continue;
      leftGraph.setGraphScale(0.0f, float(width - 1),
                              evolutionLow[series], evolutionHigh[series]);
      drawSeries(leftGraph, evolutionField(EvolutionSeries(series)), false,
                 evolutionColor(EvolutionSeries(series)),
                 firstEpoch, lastEpoch, width);
    }
  }
  if (evolution)
    sprite.drawFastVLine(PLOT_X, PLOT_Y, PLOT_H, actualColor);
  char leftLabel[20], rightLabel[20];
  formatTime(firstEpoch, leftLabel, sizeof(leftLabel));
  formatTime(lastEpoch, rightLabel, sizeof(rightLabel));
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  sprite.setTextDatum(TL_DATUM);
  sprite.drawString(leftLabel, PLOT_X, PLOT_Y + PLOT_H + 9);
  sprite.setTextDatum(TR_DATUM);
  sprite.drawString(rightLabel, PLOT_X + width, PLOT_Y + PLOT_H + 9);
  sprite.pushSprite(SPRITE_X, SPRITE_Y);
}

void redrawEvolutionAxis() {
  if (!active || view != View::Evolution || isScreenSaverActive()) return;
  const uint16_t axisColor = evolutionColor(evolutionSeries);
  // This strip contains only Y labels. Keep the traces and grid untouched.
  tft.fillRect(0, SPRITE_Y + PLOT_Y - 8, PLOT_X, PLOT_H + 16, TFT_BLACK);
  tft.drawFastVLine(PLOT_X, SPRITE_Y + PLOT_Y, PLOT_H, axisColor);
  if (!snapshot || !pointCount) return;

  float low = INFINITY, high = -INFINITY;
  const SeriesField field = evolutionField(evolutionSeries);
  for (uint16_t i = viewFirst; i <= viewLast; ++i)
    includeValue(seriesValue(snapshot[i], field), low, high);
  if (!isfinite(low) || !isfinite(high)) return;
  padEvolutionScale(evolutionSeries, low, high);

  tft.setFreeFont(nullptr);
  tft.setTextFont(2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(axisColor, TFT_BLACK);
  for (uint8_t tick = 0; tick <= 4; ++tick) {
    const float value = low + (high - low) * tick / 4.0f;
    const int16_t y = SPRITE_Y + PLOT_Y + int16_t(0.5f +
        float(PLOT_H - 1) * (high - value) / (high - low));
    char label[16];
    snprintf(label, sizeof(label), evolutionAxisFormat(evolutionSeries), value);
    tft.drawString(label, PLOT_X - 5, y);
  }
}

void drawScreen() {
  if (!active || isScreenSaverActive()) return;
  tft.setRotation(3);
  tft.setSwapBytes(false);
  tft.invertDisplay(false);
  drawBars();
  drawChart();
}
} // namespace

bool isGraphScreenActive() { return active; }

bool showGraphScreen(GraphScreenView initial) {
  if (active) return true;
  active = true;
  view = initial;
  evolutionSeries = EvolutionSeries::SG;
  refreshSnapshot(true);
  lastTouchAt = millis();
  touchHeld = true; // Wait for release of the touch that opened this screen.
  drawScreen();
  return true;
}

void redrawGraphScreen() { drawScreen(); }

void closeGraphScreen() {
  if (!active) return;
  dismissGraphScreenForScreenSaver();
  tft.setTextDatum(TL_DATUM);
  mainScreen();
}

void dismissGraphScreenForScreenSaver() {
  if (!active) return;
  active = false;
  if (snapshot) heap_caps_free(snapshot);
  snapshot = nullptr;
  pointCount = 0;
  touchHeld = false;
}

void updateGraphScreenIfNeeded() {
  if (!active || isScreenSaverActive() || millis() - lastRefreshAt < REFRESH_MS) return;
  lastRefreshAt = millis();
  const uint16_t count = graphHistoryCount();
  GraphHistoryPoint newest = {};
  const bool newLast = count && graphHistoryGetPoint(count - 1, newest);
  const bool ogChanged = snapshotOG != BatchData.batchOG &&
      !(isnan(snapshotOG) && isnan(BatchData.batchOG));
  const bool changed = snapshotBatch != BatchData.batchNumber ||
      strncmp(snapshotName, BatchData.batchName, sizeof(snapshotName)) != 0 ||
      ogChanged ||
      count != pointCount ||
      (newLast && pointCount && snapshot && newest.epoch != snapshot[pointCount - 1].epoch);
  if (!changed) return;
  refreshSnapshot(snapshotBatch != BatchData.batchNumber);
  drawScreen();
}

void handleGraphScreenTouch(uint16_t x, uint16_t y) {
  if (!active || touchHeld || millis() - lastTouchAt < 180UL) return;
  touchHeld = true;
  lastTouchAt = millis();
  if (y < HEADER_Y + HEADER_H && x >= 8 && x < BATCH_RIGHT) {
    closeGraphScreen();
    return;
  }
  if (y >= HEADER_Y && y < HEADER_Y + HEADER_H &&
      x >= TAB_X && x < TAB_X + 3 * TAB_STEP + TAB_W) {
    if ((x - TAB_X) % TAB_STEP >= TAB_W) return;
    const uint8_t selected = (x - TAB_X) / TAB_STEP;
    if (selected < 4) {
      const View tabs[4] = {View::Evolution, View::Temperature,
                            View::Pressure, View::Attenuation};
      if (tabs[selected] == View::Evolution && view != View::Evolution)
        evolutionSeries = EvolutionSeries::SG;
      view = tabs[selected];
      if (pointCount) {
        viewFirst = 0;
        viewLast = pointCount - 1;
      }
      drawScreen();
    }
    return;
  }
  const int16_t width = plotWidth();
  const int16_t plotLeft = SPRITE_X + PLOT_X;
  const int16_t plotTop = SPRITE_Y + PLOT_Y;
  if (view == View::Evolution && x >= SPRITE_X && x < plotLeft + 5 &&
      y >= plotTop && y < plotTop + PLOT_H) {
    evolutionSeries = EvolutionSeries((uint8_t(evolutionSeries) + 1) % 4);
    redrawEvolutionAxis();
    return;
  }
  if (x < plotLeft || x >= plotLeft + width ||
      y < plotTop || y >= plotTop + PLOT_H ||
      !pointCount || viewLast <= viewFirst) return;

  const uint8_t quarter = uint8_t((uint32_t(x - plotLeft) * 4) / width);
  const uint32_t firstEpoch = snapshot[viewFirst].epoch;
  const uint32_t lastEpoch = snapshot[viewLast].epoch;
  if (lastEpoch <= firstEpoch) return;
  const uint32_t span = lastEpoch - firstEpoch;
  const uint32_t quarterStart = firstEpoch + (uint64_t(span) * quarter) / 4;
  const uint32_t quarterEnd = firstEpoch + (uint64_t(span) * (quarter + 1)) / 4;
  uint16_t nextFirst = viewFirst;
  while (nextFirst <= viewLast && snapshot[nextFirst].epoch < quarterStart)
    ++nextFirst;
  uint16_t nextLast = nextFirst;
  while (nextLast <= viewLast && snapshot[nextLast].epoch <= quarterEnd)
    ++nextLast;
  if (nextLast >= nextFirst + 2) {
    viewFirst = nextFirst;
    viewLast = nextLast - 1;
    drawScreen();
  }
}

void notifyGraphScreenTouchReleased() { touchHeld = false; }
