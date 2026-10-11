#ifndef GAMBAINO_BMP581_H
#define GAMBAINO_BMP581_H

#include <Arduino.h>
#include <Wire.h>

// Minimal Bosch BMP581 barometer driver (I2C, forced mode).
// Register map from the Bosch BMP5 sensor API: CHIP_ID 0x01 (0x50 BMP581,
// 0x51 BMP585), TEMP_DATA 0x1D..0x1F (signed, /65536 C), PRESS_DATA 0x20..0x22
// (unsigned, /64 Pa), INT_STATUS 0x27, STATUS 0x28, DSP_IIR 0x31,
// OSR_CONFIG 0x36, ODR_CONFIG 0x37, CMD 0x7E (0xB6 = soft reset).
// I2C address 0x46 (SDO to GND) or 0x47 (SDO to VDDIO); begin() tries both.
//
// Usage: begin() once after Wire.begin(); then call trigger() and, at least
// ~30 ms later (one second in practice), read(). Each read() also starts the
// next conversion, so a 1 Hz loop of read() is enough.
class GambainoBMP581 {
public:
  explicit GambainoBMP581(TwoWire &wire = Wire) : wire_(wire) {}
  bool begin();               // false when no BMP581 answers
  bool present() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  bool trigger();             // starts one forced-mode conversion
  // Pressure in hPa and temperature in C of the last conversion; starts the
  // next one. False on an I2C error or an implausible value.
  bool read(float &pressureHPa, float &temperatureC);

private:
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegisters(uint8_t reg, uint8_t *data, uint8_t length);
  bool probe(uint8_t address);
  TwoWire &wire_;
  uint8_t address_ = 0;
};

#endif
