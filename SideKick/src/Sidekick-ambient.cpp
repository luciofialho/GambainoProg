#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include "GambainoCommon.h"
#include "GambainoBMP581.h"
#include "Sidekick-ambient.h"

// BMP581 on I2C (SDA 32, SCL 33); the Dallas bus on a free GPIO.
#define AMBIENT_I2C_SDA 32
#define AMBIENT_I2C_SCL 33
#define AMBIENT_DALLAS_PIN 17

static constexpr unsigned long BAROMETER_SAMPLE_MS = 1000UL;
static constexpr unsigned long BAROMETER_SEND_MS = 10000UL;   // mean of the samples since the last packet
static constexpr unsigned long DALLAS_SAMPLE_MS = 2000UL;
static constexpr unsigned long DALLAS_SEND_MS = 60000UL;
static constexpr unsigned long DALLAS_RESCAN_MS = 60000UL;   // hot-plugged sensor
static constexpr float PRESSURE_MIN_HPA = 700.0f;
static constexpr float PRESSURE_MAX_HPA = 1100.0f;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static GambainoBMP581 barometer(Wire);
static bool barometerPresent = false;
static double pressureSum = 0.0;
static uint16_t pressureCount = 0;
static float lastPressureHPa = NAN;
static float lastBarometerTempC = NAN;
static unsigned long lastPressureSentMillis = 0;
static unsigned long barometerErrors = 0;

static OneWire oneWire(AMBIENT_DALLAS_PIN);
static DallasTemperature dallas(&oneWire);
static bool dallasPresent = false;
static DeviceAddress dallasAddress;
static float lastDallasC = NAN;
static unsigned long lastDallasValidMillis = 0;
static unsigned long lastDallasSentMillis = 0;

static bool validDallas(float t) {
  return isfinite(t) && t != DEVICE_DISCONNECTED_C && t != 85.0f && t > -40.0f && t < 70.0f;
}

static void scanDallas() {
  dallas.begin();
  dallasPresent = dallas.getAddress(dallasAddress, 0);
  if (dallasPresent) {
    dallas.setResolution(dallasAddress, 12);
    dallas.setWaitForConversion(false);
    dallas.requestTemperatures();
  }
  Serial.printf("[AMBIENT] Dallas thermometer %s\n", dallasPresent ? "found" : "not found");
}

void ambientSensorsBegin() {
  Wire.begin(AMBIENT_I2C_SDA, AMBIENT_I2C_SCL);
  barometerPresent = barometer.begin();
  Serial.printf("[AMBIENT] BMP581 %s", barometerPresent ? "found" : "not found");
  if (barometerPresent) Serial.printf(" at 0x%02X", barometer.address());
  Serial.println();
  scanDallas();
}

static void processBarometer(unsigned long now) {
  static unsigned long lastSample = 0;
  if (!barometerPresent || now - lastSample < BAROMETER_SAMPLE_MS) return;
  lastSample = now;
  float pressure = NAN, temperature = NAN;
  if (barometer.read(pressure, temperature) &&
      pressure >= PRESSURE_MIN_HPA && pressure <= PRESSURE_MAX_HPA) {
    pressureSum += pressure;
    ++pressureCount;
    lastPressureHPa = pressure;
    lastBarometerTempC = temperature;
  } else {
    ++barometerErrors;
  }
  if (now - lastPressureSentMillis < BAROMETER_SEND_MS) return;
  lastPressureSentMillis = now;
  if (pressureCount == 0) return; // no valid sample: the Povotos let it expire
  char payload[16];
  snprintf(payload, sizeof(payload), "%.2f", pressureSum / pressureCount);
  pressureSum = 0.0;
  pressureCount = 0;
  sendEspNow(BROADCAST_MAC, 0, false, (uint8_t)ATMPRESSUREPACKET, payload);
}

static void processDallas(unsigned long now) {
  static unsigned long lastSample = 0;
  static unsigned long lastRescan = 0;
  if (!dallasPresent) {
    if (now - lastRescan >= DALLAS_RESCAN_MS) {
      lastRescan = now;
      scanDallas();
    }
    return;
  }
  if (now - lastSample < DALLAS_SAMPLE_MS) return;
  lastSample = now;
  const float t = dallas.getTempC(dallasAddress); // conversion requested 2 s ago
  dallas.requestTemperatures();
  if (validDallas(t)) {
    lastDallasC = t;
    lastDallasValidMillis = now;
  }
  if (now - lastDallasSentMillis < DALLAS_SEND_MS) return;
  lastDallasSentMillis = now;
  // Only a reading of the last minute is sent; the Povotos expire it after 10 min.
  if (lastDallasValidMillis == 0 || now - lastDallasValidMillis > DALLAS_SEND_MS) return;
  char payload[16];
  snprintf(payload, sizeof(payload), "%.2f", lastDallasC);
  sendEspNow(BROADCAST_MAC, 0, false, (uint8_t)ENVTEMPPACKET, payload);
}

void ambientSensorsProcess() {
  const unsigned long now = millis();
  processBarometer(now);
  processDallas(now);
}

void appendAmbientSensorsStatus(char *st, size_t size) {
  char line[160];
  if (barometerPresent)
    snprintf(line, sizeof(line), "<br>Barometer BMP581 (0x%02X): %.2f hPa, %.2f C; read errors %lu<br>",
             barometer.address(), lastPressureHPa, lastBarometerTempC, barometerErrors);
  else
    snprintf(line, sizeof(line), "<br>Barometer BMP581: not found (SDA %d, SCL %d)<br>",
             AMBIENT_I2C_SDA, AMBIENT_I2C_SCL);
  strlcat(st, line, size);
  if (dallasPresent)
    snprintf(line, sizeof(line), "Environment thermometer (Dallas, GPIO %d): %.2f C, %lu s ago<br>",
             AMBIENT_DALLAS_PIN, lastDallasC,
             lastDallasValidMillis ? (millis() - lastDallasValidMillis) / 1000UL : 0UL);
  else
    snprintf(line, sizeof(line), "Environment thermometer (Dallas, GPIO %d): not found<br>", AMBIENT_DALLAS_PIN);
  strlcat(st, line, size);
}
