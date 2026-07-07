#include <Arduino.h>
#include "RaptIntegration.h"

#if defined(BREWCORE_ENABLE_BLE) && BREWCORE_ENABLE_BLE

#include <NimBLEDevice.h>

static NimBLEScan* bleScan = NULL;
static bool bleReady = false;
static bool bleScanStarted = false;
static unsigned long bleLastHeartbeat = 0;
static unsigned long bleMatchCount = 0;
static const unsigned long BLE_HEARTBEAT_MS = 5000;
static const uint32_t BLE_SCAN_DURATION_S = 0;

static String toHex(const std::string& data) {
  String out;
  out.reserve(data.length() * 2);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < data.length(); i++) {
    const uint8_t b = static_cast<uint8_t>(data[i]);
    out += hex[(b >> 4) & 0x0F];
    out += hex[b & 0x0F];
  }
  return out;
}

static bool isRaptPacket(NimBLEAdvertisedDevice* dev) {
  if (dev == NULL)
    return false;

  if (dev->haveName()) {
    String name = String(dev->getName().c_str());
    name.toUpperCase();
    if (name.indexOf("RAPT") >= 0 || name.indexOf("KL24334") >= 0)
      return true;
  }

  if (dev->haveManufacturerData()) {
    const String mfgHex = toHex(dev->getManufacturerData());
    if (mfgHex.indexOf("4B4C3234333334") >= 0) // ASCII: KL24334
      return true;
  }

  return false;
}

class RaptAdvertisedCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (dev == NULL || !isRaptPacket(dev))
      return;

    bleMatchCount++;

    std::string mfg = dev->getManufacturerData();
    if (mfg.length() < 25)
      return;

    const uint8_t* d = (const uint8_t*)mfg.data();

    uint16_t major = ((uint16_t)d[20] << 8) | d[21];
    uint16_t minor = ((uint16_t)d[22] << 8) | d[23];
    int8_t txPower = (int8_t)d[24];

    float temperature = (float)major / 64 - 273.15;
    Serial.printf("RAPT: major=%u 0x%04X minor=%u 0x%04X tx=%d rssi=%d temperature=%.2f\n",
                  major, major, minor, minor, txPower, dev->getRSSI(), temperature);
  }
};

static RaptAdvertisedCallbacks raptAdvertisedCallbacks;

void initBLEReader() {
  if (bleReady)
    return;

  NimBLEDevice::init("BrewCore");
  bleScan = NimBLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(&raptAdvertisedCallbacks, true);
  bleScan->setActiveScan(true);
  bleScan->setInterval(45);
  bleScan->setWindow(30);
  bleReady = true;
  bleScanStarted = false;
  bleLastHeartbeat = 0;
  bleMatchCount = 0;
  Serial.println("BLE reader initialized (NimBLE async)");
}

void readBLE() {
  if (!bleReady || bleScan == NULL)
    return;

  if (!bleScanStarted) {
    const bool started = bleScan->start(BLE_SCAN_DURATION_S, nullptr, false);
    bleScanStarted = started;
    if (started) {
      bleLastHeartbeat = millis();
      Serial.println("BLE scan started (continuous async)");
    }
    else {
      Serial.println("BLE scan start failed; will retry");
    }
    return;
  }

  if (bleScan->isScanning()) {
    if (millis() - bleLastHeartbeat >= BLE_HEARTBEAT_MS) {
      bleLastHeartbeat = millis();
      Serial.print("BLE scanning alive; matches=");
      Serial.print(bleMatchCount);
      Serial.println("; waiting for RAPT/KL24334");
    }
    return;
  }

  bleScanStarted = false;
  Serial.println("BLE scan stopped; restarting");
}

#else

void initBLEReader() {}
void readBLE() {}

#endif
