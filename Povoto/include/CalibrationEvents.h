#ifndef CALIBRATION_EVENTS_H
#define CALIBRATION_EVENTS_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Calibration and maintenance events (sensor zero saved, valve cleaning
// cycle, ...), one text line each in the data filesystem (/calevents.txt,
// "local ISO time;type;details"). The oldest half is dropped above 16 KB.
void calibrationEventAppend(const char *type, const char *details);
// The last count events, newest first, as HTML table rows.
String calibrationEventsHtmlRows(uint8_t count);
// Type of the newest event ("" = none).
String lastCalibrationEventType();
void handleCalibrationEventsText(AsyncWebServerRequest *request); // /calibration/events.txt
// Download name PovotoCalibration-<type>-FMT<n>-yyyymmddhhmm.<extension> with
// the local time of `epoch` ("nodate" without NTP time); "Incomplete-" in
// front when the routine has not finished.
String calibrationFileName(const char *type, uint32_t epoch, bool incomplete, const char *extension = "csv");

#endif
