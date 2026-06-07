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

extern TFT_eSPI tft;

#define UI_PERF_LOG 1

#define LEFT   0
#define CENTER 1
#define RIGHT  2

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
  
  // Corrige cores invertidas
  tft.setSwapBytes(false);
  
  // Tenta inversão de cores manual
  tft.invertDisplay(true); 

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Display ST7796 SPI OK!", 20, 50, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(20, 100);
  tft.print("Hello World!");
  
  drawBmp("/LCars.bmp", 0, 0);  // format: 
}


void screenData() {
  if (DisplayMode != 0)
    return;
  if (isTaskUIActive())
    return;
  if (isBatchInfoActive())
    return;

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
  char reliefsPerHourCompact[24];
  getReliefsPerHourCompactText(reliefsPerHourCompact, sizeof(reliefsPerHourCompact));
  tft.setTextColor(0, tft.color565(185,208,170));
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,273, "g CO2%s ", reliefsPerHourCompact);
  tft.setTextColor(TFT_YELLOW,0);
  textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,273, " %.0f", CO2Mass()); 

  // VOLUME
  tft.setTextColor(TFT_WHITE,tft.color565(204,102,153)); 
  textOut(CENTER,&Swiss_911_Extra_Compressed_Regular18pt7b,170,179, " %.0f ",beerVolume);
  textOut(CENTER,&Swiss_911_Extra_Compressed_Regular12pt7b,170,204, "liters");

  // SPECIFIC GRAVITY
  tft.setTextColor(0);
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,176, "SG   [ OG=%.3f ]", BatchData.batchOG);
  tft.setTextColor(TFT_YELLOW,0);
  textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,176, " %.3f", beerSG);

  // ABV
  tft.setTextColor(0);
  textOut(LEFT,&Swiss_911_Extra_Compressed_Regular12pt7b,320,204, "%% ABV");
  tft.setTextColor(TFT_YELLOW,0);
  textOut(RIGHT,&Swiss_911_Extra_Compressed_Regular12pt7b,304,204, " %.2f", beerABV);
}

void mainScreen() {
  if (isTempKeyboardActive()) return; // não sobrescreve o teclado
  if (isTaskUIActive()) return;       // não sobrescreve a UI de tarefas
  if (isBatchInfoActive()) return;    // não sobrescreve o batch info
  const unsigned long t0 = millis();
  screenBackground();
  const unsigned long t1 = millis();
  screenData();
  const unsigned long t2 = millis();
  if (UI_PERF_LOG) {
    Serial.printf("[UI PERF] mainScreen total=%lums bg=%lums data=%lums\n",
                  (unsigned long)(t2 - t0),
                  (unsigned long)(t1 - t0),
                  (unsigned long)(t2 - t1));
  }
}