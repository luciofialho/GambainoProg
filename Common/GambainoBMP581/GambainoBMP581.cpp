#include "GambainoBMP581.h"

static constexpr uint8_t BMP581_REG_CHIP_ID = 0x01;
static constexpr uint8_t BMP581_REG_TEMP_XLSB = 0x1D;
static constexpr uint8_t BMP581_REG_INT_STATUS = 0x27;
static constexpr uint8_t BMP581_REG_STATUS = 0x28;
static constexpr uint8_t BMP581_REG_DSP_IIR = 0x31;
static constexpr uint8_t BMP581_REG_OSR_CONFIG = 0x36;
static constexpr uint8_t BMP581_REG_ODR_CONFIG = 0x37;
static constexpr uint8_t BMP581_REG_CMD = 0x7E;
static constexpr uint8_t BMP581_CMD_SOFT_RESET = 0xB6;
static constexpr uint8_t BMP581_STATUS_NVM_RDY = 0x02;
static constexpr uint8_t BMP581_STATUS_NVM_ERR = 0x04;
// press_en, pressure oversampling x16, temperature x2.
static constexpr uint8_t BMP581_OSR_CONFIG = (1 << 6) | (4 << 3) | 1;
// deep_dis, forced mode (pwr_mode = 2): one conversion, then standby.
static constexpr uint8_t BMP581_ODR_FORCED = (1 << 7) | 2;
static constexpr uint8_t BMP581_ODR_STANDBY = (1 << 7);

bool GambainoBMP581::writeRegister(uint8_t reg, uint8_t value) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(value);
  return wire_.endTransmission() == 0;
}

bool GambainoBMP581::readRegisters(uint8_t reg, uint8_t *data, uint8_t length) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) return false;
  if (wire_.requestFrom(address_, length) != length) return false;
  for (uint8_t i = 0; i < length; ++i) data[i] = wire_.read();
  return true;
}

bool GambainoBMP581::probe(uint8_t address) {
  address_ = address;
  uint8_t id = 0;
  if (!readRegisters(BMP581_REG_CHIP_ID, &id, 1) || (id != 0x50 && id != 0x51)) {
    address_ = 0;
    return false;
  }
  writeRegister(BMP581_REG_CMD, BMP581_CMD_SOFT_RESET);
  delay(3);
  uint8_t status = 0, interrupt = 0;
  if (!readRegisters(BMP581_REG_CHIP_ID, &id, 1) || (id != 0x50 && id != 0x51) ||
      !readRegisters(BMP581_REG_STATUS, &status, 1) ||
      !(status & BMP581_STATUS_NVM_RDY) || (status & BMP581_STATUS_NVM_ERR)) {
    address_ = 0;
    return false;
  }
  readRegisters(BMP581_REG_INT_STATUS, &interrupt, 1); // clears the power-on flag
  if (!writeRegister(BMP581_REG_ODR_CONFIG, BMP581_ODR_STANDBY) ||
      !writeRegister(BMP581_REG_DSP_IIR, 0) ||          // no IIR: the caller averages
      !writeRegister(BMP581_REG_OSR_CONFIG, BMP581_OSR_CONFIG)) {
    address_ = 0;
    return false;
  }
  return true;
}

bool GambainoBMP581::begin() {
  if (probe(0x47) || probe(0x46)) return trigger();
  return false;
}

bool GambainoBMP581::trigger() {
  return address_ != 0 && writeRegister(BMP581_REG_ODR_CONFIG, BMP581_ODR_FORCED);
}

bool GambainoBMP581::read(float &pressureHPa, float &temperatureC) {
  if (address_ == 0) return false;
  uint8_t raw[6];
  const bool ok = readRegisters(BMP581_REG_TEMP_XLSB, raw, sizeof(raw));
  trigger();
  if (!ok) return false;
  int32_t temperature = (int32_t)((uint32_t)raw[2] << 16 | (uint32_t)raw[1] << 8 | raw[0]);
  if (temperature & 0x800000) temperature -= 0x1000000; // 24-bit two's complement
  const uint32_t pressure = (uint32_t)raw[5] << 16 | (uint32_t)raw[4] << 8 | raw[3];
  temperatureC = temperature / 65536.0f;
  pressureHPa = pressure / 64.0f / 100.0f;
  return pressure != 0 && isfinite(pressureHPa) && pressureHPa > 300.0f && pressureHPa < 1250.0f;
}
