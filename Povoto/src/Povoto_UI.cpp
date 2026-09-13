#include <IOTK.h>
#include <IOTK_NTP.h>
#include <IOTK_ESPAsyncServer.h>
#include <WiFi.h>
#include "displayUtils.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>  
#include "stdarg.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "Povoto_UI.h"
#include "PressureControl.h"
#include "PovotoWifi.h"
#include "Swiss_911_Extra_Compressed_Regular7pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular8pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular9pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular10pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular12pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular16pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular18pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular20pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular22pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular24pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular28pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular32pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular36pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular42pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular60pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular72pt7b.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32-hal-psram.h"
#endif

extern TFT_eSPI tft;

#define UI_PERF_LOG 1

#define LEFT   0
#define CENTER 1
#define RIGHT  2

static bool screenDataInvalidated = true;
static char lastScreenDataSignature[384] = "";
static char lastScreenStaticSignature[128] = "";

void invalidateScreenData() {
  screenDataInvalidated = true;
}

static void formatEpochDateShort(uint32_t epoch, char *buf, size_t bufSize) {
  if (!buf || bufSize == 0) return;
  if (epoch == 0) {
    snprintf(buf, bufSize, "n/a");
    return;
  }

  unsigned long day;
  int8_t dayOfWeek;
  int8_t hours;
  int8_t minutes;
  int8_t seconds;
  int8_t dayOfMonth;
  int8_t month;
  int16_t year;
  convertFromEpoch(epoch, day, dayOfWeek, hours, minutes, seconds, dayOfMonth, month, year);
  snprintf(buf, bufSize, "%02d/%02d", dayOfMonth, month);
}

static void formatBatchDateShort(const char *batchDate, char *buf, size_t bufSize) {
  if (!buf || bufSize == 0) return;
  if (!batchDate || batchDate[0] == '\0') {
    snprintf(buf, bufSize, "n/a");
    return;
  }

  int d = 0;
  int m = 0;
  int y = 0;

  if (sscanf(batchDate, "%2d/%2d", &d, &m) == 2) {
    snprintf(buf, bufSize, "%02d/%02d", d, m);
    return;
  }

  if (sscanf(batchDate, "%4d-%2d-%2d", &y, &m, &d) == 3) {
    snprintf(buf, bufSize, "%02d/%02d", d, m);
    return;
  }

  if (sscanf(batchDate, "%2d-%2d", &d, &m) == 2) {
    snprintf(buf, bufSize, "%02d/%02d", d, m);
    return;
  }

  snprintf(buf, bufSize, "n/a");
}

int16_t textRealHeight(const GFXfont *gfxFont,const char *text, int16_t *yMinOut) {
  int16_t yMin = 32767;
  int16_t yMax = -32768;

  for (const char *p = text; *p; p++) {
    uint8_t c = *p;
    if (c < gfxFont->first || c > gfxFont->last) continue;

    GFXglyph *g = &gfxFont->glyph[c - gfxFont->first];

    int16_t top    = g->yOffset;
    int16_t bottom = g->yOffset + g->height;

    if (top < yMin) yMin = top;
    if (bottom > yMax) yMax = bottom;
  }

  if (yMinOut) *yMinOut = yMin;
  return yMax - yMin;
}

void textOut(byte alignment, const GFXfont *font, int x, int y, const char *text, ...) {
  if (!font)
    return;

  tft.setFreeFont(font);
  tft.setTextDatum(TL_DATUM);

  va_list args;
  va_start(args, text);
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), text, args);
  va_end(args);

  
  int16_t w = tft.textWidth(buffer);
  int16_t h = tft.fontHeight();
  int16_t yMin;
  int16_t hReal = textRealHeight(font,buffer, &yMin);


  y = y - (hReal / 2);

  switch (alignment) {
    case LEFT:
      tft.drawString(buffer, x, y);
      break;
    case CENTER:
      tft.drawString(buffer, x - w / 2, y);
      break;
    case RIGHT:
      tft.drawString(buffer, x - w, y);
      break;
  }
}

void screenBackground() {
  tft.setRotation(3);
  
  // Mantem swap de bytes desativado no caminho normal de desenho.
  tft.setSwapBytes(false);
  
  // No painel novo a inversao forcada deixa as cores trocadas.
  tft.invertDisplay(false);
  
  drawBmp("/LCars.bmp", 0, 0);  // format: 
}


void screenData() {
  if (DisplayMode != 0)
    return;
  if (povotoWiFiConfigurationActive())
    return;
  if (isTaskUIActive())
    return;
  if (isBatchInfoActive())
    return;

  char reliefsPerHourCompact[24];
  getReliefsPerHourCompactText(reliefsPerHourCompact, sizeof(reliefsPerHourCompact));
  char staticSignature[sizeof(lastScreenStaticSignature)];
  snprintf(staticSignature, sizeof(staticSignature), "%d|%s|%d|%s|%.3f",
    (int)BatchData.batchNumber, BatchData.batchDate, (int)FMTData.PovotoNum,
    BatchData.batchName, BatchData.batchOG);
  const bool drawStatic = screenDataInvalidated ||
    strcmp(staticSignature, lastScreenStaticSignature) != 0;
  if (drawStatic) {
    strncpy(lastScreenStaticSignature, staticSignature, sizeof(lastScreenStaticSignature) - 1);
    lastScreenStaticSignature[sizeof(lastScreenStaticSignature) - 1] = '\0';
  }

  char screenSignature[sizeof(lastScreenDataSignature)];
  snprintf(screenSignature, sizeof(screenSignature),
    "%.1f|%.1f|%.1f|%.1f|%.2f|%s|%.0f|%.0f|%.3f|%.2f",
    ControlData.temperature, SetPointData.setPointTemp, SetPointData.setPointSlowTemp,
    ControlData.pressure, SetPointData.setPointPressure, reliefsPerHourCompact,
    CO2Mass(), beerVolume, beerSG, beerABV);
  const bool drawDynamic = screenDataInvalidated ||
    strcmp(screenSignature, lastScreenDataSignature) != 0;
  if (!drawStatic && !drawDynamic) {
    povotoWiFiDrawStatusIndicator();
    drawCalibrationStatus();
    return;
  }
  if (drawDynamic) {
    strncpy(lastScreenDataSignature, screenSignature, sizeof(lastScreenDataSignature) - 1);
    lastScreenDataSignature[sizeof(lastScreenDataSignature) - 1] = '\0';
  }
  screenDataInvalidated = false;

  if (drawStatic) {
    // These GFX fonts draw transparently. Restore only the affected parts of
    // the cached background before replacing rare, configuration-driven text.
    restoreMainBackgroundRect(35, 0, 105, 42);   // Batch number
    restoreMainBackgroundRect(115, 0, 110, 35);  // Batch date
    restoreMainBackgroundRect(0, 70, 135, 120);  // Povoto number
    restoreMainBackgroundRect(235, 0, 245, 55);  // Batch name
    restoreMainBackgroundRect(315, 155, 165, 40); // Batch OG label

    tft.setTextColor(TFT_WHITE);
    textOut(CENTER,&Swiss_911_Extra_Compressed_Regular12pt7b,86,21, "%04d", BatchData.batchNumber);

    char batchDateShortBuf[8];
    formatBatchDateShort(BatchData.batchDate, batchDateShortBuf, sizeof(batchDateShortBuf));

    tft.setTextColor(TFT_LIGHTGREY,tft.color565(204,102,153));
    textOut(CENTER,&Swiss_911_Extra_Compressed_Regular10pt7b,168,16, "  %s  ", batchDateShortBuf);

    tft.setTextColor(TFT_LIGHTGREY);
    textOut(CENTER, &Swiss_911_Extra_Compressed_Regular72pt7b, 63,150, "%d",FMTData.PovotoNum);

    tft.setTextColor(tft.color565(238,214,157),0);
    textOut(CENTER,&Swiss_911_Extra_Compressed_Regular16pt7b,343,27, "   %s   ",BatchData.batchName);
    tft.setTextColor(0);
    textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,176, "SG   [ OG=%.3f ]", BatchData.batchOG);
  }

  if (!drawDynamic) {
    povotoWiFiDrawStatusIndicator();
    drawCalibrationStatus();
    return;
  }

  tft.setTextColor(TFT_WHITE); 

  // TEMPERATURE  
  tft.setTextColor(TFT_WHITE,tft.color565(0xff, 0xcc, 0x66)); 
  if (ControlData.temperature != NOTaTEMP)
    textOut(CENTER,&Swiss_911_Extra_Compressed_Regular18pt7b,170,105, "%.1f",ControlData.temperature);
  else
    textOut(CENTER,&Swiss_911_Extra_Compressed_Regular18pt7b,170,105, "ERR");
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular7pt7b);                                  
  tft.drawString("o",162,128);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular12pt7b);                                    
  tft.drawString("C",170,128);

  // TEMPERATURE TARGET
  tft.setTextColor(0);   
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,104, "Target");
  tft.setTextColor(TFT_YELLOW,0); 
  if (SetPointData.setPointTemp != NOTaTEMP)
    textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,104, "%.1f",SetPointData.setPointTemp);
  else
    textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,104, "n/a");

  // SLOW TEMPERATURE TARGET 
  tft.setTextColor(0,tft.color565(153,153,204)); 
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,132, "Slow target");
  tft.setTextColor(TFT_YELLOW,0); 
  if (SetPointData.setPointSlowTemp != NOTaTEMP) 
    textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,132, "   %.1f",SetPointData.setPointSlowTemp);
  else
    textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,132, "   n/a");

  // PRESSURE 
  tft.setTextColor(TFT_WHITE,tft.color565(153,153,255)); 
  textOut(CENTER,&Swiss_911_Extra_Compressed_Regular18pt7b,170,259, "  %.1f  ",ControlData.pressure);
  textOut(CENTER,&Swiss_911_Extra_Compressed_Regular12pt7b,170,284, "bar");

  // PRESSURE TARGET 
  tft.setTextColor(0);
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,245, "Target");
  tft.setTextColor(TFT_YELLOW,0);
  if (SetPointData.setPointPressure != 0)
    textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,245, "   %.2f", SetPointData.setPointPressure);
  else
    textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,245, "   n/a");

  // CO2 MASS COUNT
  tft.setTextColor(0, tft.color565(185,208,170));
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,273, "g CO2%s ", reliefsPerHourCompact);
  tft.setTextColor(TFT_YELLOW,0);
  textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,273, " %.0f", CO2Mass()); 

  // VOLUME
  tft.setTextColor(TFT_WHITE,tft.color565(204,102,153)); 
  textOut(CENTER,&Swiss_911_Extra_Compressed_Regular18pt7b,170,179, " %.0f ",beerVolume);
  textOut(CENTER,&Swiss_911_Extra_Compressed_Regular12pt7b,170,204, "liters");

  // SPECIFIC GRAVITY
  tft.setTextColor(TFT_YELLOW,0);
  textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,176, " %.3f", beerSG);

  // ABV
  tft.setTextColor(0);
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,204, "%% ABV");
  tft.setTextColor(TFT_YELLOW,0);
  textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,204, " %.2f", beerABV);

  povotoWiFiDrawStatusIndicator();
  drawCalibrationStatus();
}

void mainScreen() {
  if (isTempKeyboardActive()) return; // não sobrescreve o teclado
  if (isTaskUIActive()) return;       // não sobrescreve a UI de tarefas
  if (isBatchInfoActive()) return;    // não sobrescreve o batch info
  const unsigned long t0 = millis();
  screenBackground();
  const unsigned long t1 = millis();
  invalidateScreenData();
  screenData();
  const unsigned long t2 = millis();
  if (UI_PERF_LOG) {
    Serial.printf("[UI PERF] mainScreen total=%lums bg=%lums data=%lums psram=%u\n",
                  (unsigned long)(t2 - t0),
                  (unsigned long)(t1 - t0),
                  (unsigned long)(t2 - t1),
                  (unsigned)psramFound());
  }
}
