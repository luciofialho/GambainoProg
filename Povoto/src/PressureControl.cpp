#include "PressureControl.h"
#include "TemperatureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include <Adafruit_INA219.h>
#include <Arduino.h>
#include <IOTK.h>
#include <IOTK_NTP.h>
#include "__NumFilters.h"
#include <math.h>


#define DEBUGACCELERATION (debugging ? 20L : 1L)
#define TRANSFERTIME (10000 / DEBUGACCELERATION) 
#define RELIEFTIME (10000 / DEBUGACCELERATION)
#define SOLENOIDCLOSINGTIME (1000 / DEBUGACCELERATION)

#define PRESSURE_MEDIAN_WINDOW 5

// Detecção de instabilidade: range máximo tolerado dentro da janela do filtro
#define PRESSURE_INSTABILITY_RANGE_THRESHOLD 0.5f  // bar
// Ciclos consecutivos ruins para declarar instabilidade / bons para recuperar
#define PRESSURE_INSTABILITY_BAD_COUNT  3
#define PRESSURE_INSTABILITY_GOOD_COUNT 8

#define SOLENOID_NOISE_MS 400

#define PRESSURE_SAMPLE_MIN_MS 250
#define PRESSURE_SAMPLES_MAX 3000

#define CONST_R 0.0831446 // constante dos gases em bar*L/(mol*K) 
#define CO2MOLAR_MASS 44.01


// Instância global do INA219 
Adafruit_INA219 ina219;
bool pressureSensorConnected = false;
float currentReading = 0.0; // Corrente em mA


averageFloatVector lnPressureDropAvg(15);
float headSpaceVolume = 0.0f;
float beerVolume = 0.0f;
float beerSG = 0.0;
float beerABV = 0.0;
float beerPlato = 0.0f;
float dissolvedCO2Mols = 0.0f;
float headSpaceCO2Mols = 0.0f;
float sgPointGenerationTime = 0.0f;

static unsigned long int timeToOpenTransfer     = 0;
static unsigned long int timeToCloseTransfer    = 0;
static unsigned long int timeToOpenRelief       = 0;
static unsigned long int timeToCloseRelief      = 0;
static unsigned long int timeToFinishRelief     = 0;
static unsigned long int timeToRegisterPressure_ = 0; // promovido de static local para diagnóstico
static float lastPressure = 0;

static float blindHighPressure = 0;
static unsigned long int blindHighPressureMillis = 0;
static float blindLowPressure = 0;
static unsigned long int blindLowPressureMillis = 0;

struct PressureReliefRecord {
  char timestamp[32];
  float temperature;
  float pressureBefore;
  float pressureAfter;
};

static PressureReliefRecord pressureReliefHistory[1000];
static uint16_t pressureReliefIndex = 0;
static uint16_t pressureReliefCount = 0;
static int16_t pendingReliefIndex = -1;
static unsigned long lastSolenoidToggleMillis = 0;
static float pressureDropFactor = 0.99f;

static bool volumeDeterminationActive = false;
static float volumeStartPressure = 0.0f;
static float volumeTargetPressure = 0.0f;
static unsigned long volumeLastReliefMillis = 0;
static uint16_t volumeIteration = 0;
static bool volumeAwaitingRecord = false;
static int16_t volumeRecordIndex = -1;

struct PressureSampleRecord {
  char timestamp[6];
  unsigned long millisStamp;
  float pressure;
};

static PressureSampleRecord *pressureSamples = nullptr;
static uint16_t pressureSamplesIndex = 0;
static uint16_t pressureSamplesCount = 0;
static unsigned long pressureLastSampleMillis = 0;
static bool pressureDumpInProgress = false;
static PressureSampleRecord *pressureDumpSamples = nullptr;
static uint16_t pressureDumpCount = 0;
static uint16_t pressureDumpIndex = 0;
static bool pressureDumpHeaderSent = false;
static bool pressureDumpDone = false;

static float pressureWindow[PRESSURE_MEDIAN_WINDOW];
static uint8_t pressureWindowCount = 0;
static uint8_t pressureWindowIndex = 0;

bool pressureSensorUnstable = false;
static uint8_t pressureBadCount  = 0;
static uint8_t pressureGoodCount = 0;

float kelvin(float x) {
  return x + 273.15f;
}

float CO2Mass(float mols) {
  if (mols == -1) 
    return CountersData.totalCO2MolsProduced * CO2MOLAR_MASS;
  else
    return mols * CO2MOLAR_MASS;
}

float SGToPlato(float sg) {
  if (sg < 1.0f) return 0.0f;
  float sg2 = sg * sg;
  float sg3 = sg2 * sg;
  return -616.868f + 1111.14f * sg - 630.272f * sg2 + 135.997f * sg3;
}

float PlatoToSG(float plato) {
  if (plato <= 0.0f) return 1.0f;
  return 1.0f + plato / (258.6f - (plato / 258.2f) * 227.1f);
}

float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL) {
  if (pressureBar <= 0.0f || volumeL <= 0.0f) {
    return 0.0f;
  }

  const float tempK = temperatureC + 273.15f;
  const float kH_298 = 0.0334f; // mol/(L*atm) at 25C for CO2 in water
  const float pressureAtm = pressureBar * 0.986923f;

  float kH = kH_298 * expf(2400.0f * (1.0f / 298.15f - 1.0f / tempK));

  float sgPoints = (sg - 1.0f) * 1000.0f;
  float sgCorrection = 1.0f - (sgPoints * 0.0015f);
  if (sgCorrection < 0.5f) sgCorrection = 0.5f;
  if (sgCorrection > 1.0f) sgCorrection = 1.0f;

  float molPerL = kH * pressureAtm * sgCorrection;
  return molPerL * volumeL;
}

static void markSolenoidToggle() {
  lastSolenoidToggleMillis = millis();
}

boolean inPressureNoiseWindow() {
  return !(MILLISDIFF(lastSolenoidToggleMillis,SOLENOID_NOISE_MS));
}

static float medianFilter(float sample) {
  pressureWindow[pressureWindowIndex] = sample;
  pressureWindowIndex = (pressureWindowIndex + 1) % PRESSURE_MEDIAN_WINDOW;
  if (pressureWindowCount < PRESSURE_MEDIAN_WINDOW) {
    pressureWindowCount++;
  }

  float temp[PRESSURE_MEDIAN_WINDOW];
  for (uint8_t i = 0; i < pressureWindowCount; ++i) {
    temp[i] = pressureWindow[i];
  }

  for (uint8_t i = 1; i < pressureWindowCount; ++i) {
    float key = temp[i];
    int8_t j = i - 1;
    while (j >= 0 && temp[j] > key) {
      temp[j + 1] = temp[j];
      --j;
    }
    temp[j + 1] = key;
  }

  // Detecção de instabilidade: array já está ordenado → range = max - min (custo zero)
  if (pressureWindowCount >= PRESSURE_MEDIAN_WINDOW) {
    float range = temp[pressureWindowCount - 1] - temp[0];
    if (range > PRESSURE_INSTABILITY_RANGE_THRESHOLD) {
      pressureGoodCount = 0;
      if (++pressureBadCount >= PRESSURE_INSTABILITY_BAD_COUNT) {
        pressureBadCount = PRESSURE_INSTABILITY_BAD_COUNT; // evita overflow
        if (!pressureSensorUnstable) {
          pressureSensorUnstable = true;
          Serial.printf("[PRESSURE] Instabilidade detectada! Range=%.3f bar\n", range);
        }
      }
    } else {
      pressureBadCount = 0;
      if (++pressureGoodCount >= PRESSURE_INSTABILITY_GOOD_COUNT) {
        pressureGoodCount = PRESSURE_INSTABILITY_GOOD_COUNT; // evita overflow
        if (pressureSensorUnstable) {
          pressureSensorUnstable = false;
          Serial.println("[PRESSURE] Sensor estabilizado.");
        }
      }
    }
  }

  // Fail fast: qualquer sinal de problema suprime imediatamente o valor.
  // Recovery lento: só libera após PRESSURE_INSTABILITY_GOOD_COUNT leituras boas consecutivas.
  if (pressureBadCount == 0 && !pressureSensorUnstable) {
    uint8_t mid = pressureWindowCount / 2;
    if ((pressureWindowCount % 2) == 1) {
      return temp[mid];
    }
    return (temp[mid - 1] + temp[mid]) * 0.5f;
  }
  else 
    return 0.0f;
}


void readPressure() {
  // Try to initialize the INA219 if it hasn't been done yet
  static bool initialized = false;
  if (!initialized) {
    pressureSensorConnected = ina219.begin() || debugging;
    if (pressureSensorConnected) {
      Serial.println("INA219 pressure sensor initialized successfully");
    } else {
      Serial.println("Could not find INA219 pressure sensor");
    }
    ina219.setCalibration_16V_40mA();
    initialized = true;
  }
  
  // Se o sensor está conectado, faz a leitura
  if (pressureSensorConnected && !inPressureNoiseWindow()) {
    if (!debugging) {
      // Lê a corrente do INA219
      currentReading = ina219.getCurrent_mA();
      
      // Converte a corrente para pressão usando calibração
      // Usa interpolação linear entre os pontos de calibração
      float pressure = 0.0;
      if (currentReading <= CalibrationData.pressure0Current) {
        // Abaixo do primeiro ponto de calibração - considera 0 bar
        pressure = 0.0;
      }
      else if (currentReading <= CalibrationData.pressure1Current) {
        // Entre ponto 0 e ponto 1
        float ratio = (currentReading - CalibrationData.pressure0Current) / 
                      (CalibrationData.pressure1Current - CalibrationData.pressure0Current);
        pressure = ratio * CalibrationData.pressure1Bar;
      }
      else if (currentReading <= CalibrationData.pressure2Current) {
        // Entre ponto 1 e ponto 2
        float ratio = (currentReading - CalibrationData.pressure1Current) / 
                      (CalibrationData.pressure2Current - CalibrationData.pressure1Current);
        pressure = CalibrationData.pressure1Bar + ratio * (CalibrationData.pressure2Bar - CalibrationData.pressure1Bar);
      }
      else {
        // Acima do segundo ponto - extrapola linearmente
        float ratio = (currentReading - CalibrationData.pressure1Current) / 
                      (CalibrationData.pressure2Current - CalibrationData.pressure1Current);
        pressure = CalibrationData.pressure1Bar + ratio * (CalibrationData.pressure2Bar - CalibrationData.pressure1Bar);
      }

      pressure = medianFilter(pressure);
      ControlData.pressure = pressure;
    }
    else { //is debugging
      static unsigned long lastPressureIncrease = 0;
      if (sgPointGenerationTime != 0 && timeToCloseRelief == 0 && beerSG > 1.010f) {
        if (MILLISDIFF(lastPressureIncrease,1000*sgPointGenerationTime))  {
          lastPressureIncrease = millis();
          ControlData.pressure += 0.1f;
        } 
      }
    }
  }
  else if (!pressureSensorConnected) {
    // Se não tem sensor, zera a pressão - Lucio urgente - precisar alertar
    ControlData.pressure = 0.0;
    currentReading = 0.0;
  }  
}  

static void showVolumeStatus(const char *line1, const char *line2, const char *line3) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(10, 220, 300, 90, TFT_BLACK);
  tft.drawString(line1, 10, 225, 2);
  tft.drawString(line2, 10, 245, 2);
  tft.drawString(line3, 10, 265, 2);
  Serial.print(line1); Serial.print(" | "); Serial.print(line2); Serial.print(" | "); Serial.println(line3);
}

static void formatFloatCsv(char *out, size_t size, float value, uint8_t decimals) {
  snprintf(out, size, "%.*f", decimals, value);
  for (size_t i = 0; out[i] != '\0'; ++i) {
    if (out[i] == '.') {
      out[i] = ',';
    }
  }
}



void processPressure(bool relieving) {
  if (debugging) {
    if (volumeDeterminationActive && pressureSamples) 
      ControlData.pressure = ControlData.pressure * 0.984f + random(-5,5) * 0.0005; 
    else
      ControlData.pressure = ControlData.pressure * 0.942f + random(-5,5) * 0.0005;       
  }      

  readPressure();
  
  if  (relieving) {
    static float lastPressureDrop = 0;

    float pressureGainOverTime = (blindHighPressure - blindLowPressure) / (blindHighPressureMillis - blindLowPressureMillis);
    if (pressureGainOverTime < 0 || pressureGainOverTime > 1e-4) {
      pressureGainOverTime = 0;
    }
    
    float blindWindowCorrection = pressureGainOverTime * (millis() - blindHighPressureMillis);
    
    //lastPressureDrop = ControlData.pressure - lastPressure + blindWindowCorrection;

    blindLowPressure = ControlData.pressure;
    blindLowPressureMillis = millis();

    float pressureDropFactor;
    if (ControlData.pressure>0.01)
      pressureDropFactor = (ControlData.pressure-blindWindowCorrection) / lastPressure;
    else
      pressureDropFactor = 1.0f;

    lnPressureDropAvg.add(log(pressureDropFactor));
    pressureDropFactor = exp(lnPressureDropAvg.value());
    
    headSpaceVolume = (FMTData.FMTReliefVolume * pressureDropFactor) / (1-pressureDropFactor);
    beerVolume = FMTData.FMTVolume - headSpaceVolume;
    
    if (pendingReliefIndex >= 0) {
      PressureReliefRecord &record = pressureReliefHistory[pendingReliefIndex];
      record.pressureAfter = ControlData.pressure;
    }
    pendingReliefIndex = -1;

    float ejectedMols = ControlData.pressure * FMTData.FMTReliefVolume / (CONST_R * kelvin(ControlData.temperature));
    CountersData.totalMolsEjected += ejectedMols;
    
    CountersData.totalReliefCount += 1;    
  }

  if (CountersData.totalReliefCount> 1 || ControlData.pressure > BatchData.startPressure+0.2) 
    dissolvedCO2Mols = CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume);
  else
    dissolvedCO2Mols = 0;

  if (CountersData.totalReliefCount>1) {
    headSpaceCO2Mols = ControlData.pressure    * headSpaceVolume / (CONST_R * kelvin(ControlData.temperature))
                     - BatchData.startPressure * headSpaceVolume / (CONST_R * kelvin(BatchData.startTemperature));
    if (headSpaceCO2Mols < 0) 
      headSpaceCO2Mols = 0;
  }
  else 
    headSpaceCO2Mols = 0;  
  
  //float lastTotalCO2MolsProceduced = CountersData.totalCO2MolsProduced; está sendo usado ou não?

  CountersData.totalCO2MolsProduced = CountersData.totalMolsEjected + dissolvedCO2Mols + headSpaceCO2Mols;

//  float massCO2Produced = CO2Mass(CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced);
//  CountersData.SGAttenuation += 2.047*massCO2Produced/(beerVolume)/1000;

//  CountersData.SGAttenuation -= EstimateSGFromProducedCO2Mol(beerSG, beerVolume, CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced) - beerSG;
//  Serial.println(EstimateSGFromProducedCO2Mol(beerSG, beerVolume, CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced) *1000.0);

  float Pi = SGToPlato(BatchData.batchOG) + BatchData.addedPlato;
  float SGu = 1 + (Pi / (258.6-(Pi/258.2)*227.1));
  beerPlato = (100*(10*beerVolume*SGu*Pi - 90.08 * CountersData.totalCO2MolsProduced)
                / (1000*beerVolume*SGu - 44.01 * CountersData.totalCO2MolsProduced) 
             - 0.1808*Pi ) 
             / 0.8192;
  beerSG = PlatoToSG(beerPlato);
  beerABV = 100*(105*(BatchData.batchOG - beerSG) / (100 - beerSG) * (beerSG / 0.79));

  //beerSG = BatchData.batchOG - CountersData.SGAttenuation;
  
  if (relieving && volumeDeterminationActive) {
    if (volumeAwaitingRecord && volumeRecordIndex >= 0) {
      const PressureReliefRecord &record = pressureReliefHistory[volumeRecordIndex];
      volumeIteration++;
      char line1[32];
      char line2[32];
      char line3[32];
      snprintf(line1, sizeof(line1), "Iteracao: %u", volumeIteration);
      snprintf(line2, sizeof(line2), "P antes: %.2f", record.pressureBefore);
      snprintf(line3, sizeof(line3), "P apos:  %.2f", record.pressureAfter);
      showVolumeStatus(line1, line2, line3);
      volumeAwaitingRecord = false;
      volumeRecordIndex = -1;
    }
  }

  lastPressure = ControlData.pressure;
}

bool processPressureRelays() {
  // timeToRegisterPressure_ is now file-scope for diagnostics

  if (timeToFinishRelief) {
    if (timeToOpenTransfer) {
      if (millis() >= timeToOpenTransfer) {
        readPressure();
        lastPressure = ControlData.pressure;
        blindHighPressure = ControlData.pressure;
        blindHighPressureMillis = millis();
        digitalWrite(PINTRANSFERVALVE, HIGH);
        markSolenoidToggle();
        ControlData.transferValve = true;
        timeToCloseTransfer += millis();
        timeToOpenTransfer = 0; 
      }
    }
    else if (timeToCloseTransfer) {
      if (millis() >= timeToCloseTransfer) {
        digitalWrite(PINTRANSFERVALVE, LOW);
        markSolenoidToggle();
        ControlData.transferValve = false;
        timeToOpenRelief    += millis();
        timeToCloseTransfer = 0;
      }
    }
    else if (timeToOpenRelief) {
      if(millis() >= timeToOpenRelief) {
        digitalWrite(PINRELIEFVALVE, HIGH);
        markSolenoidToggle();
        ControlData.reliefValve = true;
        timeToCloseRelief += millis();
        timeToRegisterPressure_ = timeToCloseRelief - 500; 
        timeToOpenRelief = 0;
      }
    }
    else if (timeToRegisterPressure_) {
      if (millis() >= timeToRegisterPressure_) {
        timeToRegisterPressure_ = 0;
        processPressure(true);
      }
    }
    else if (timeToCloseRelief) {
      if (millis() >= timeToCloseRelief) {
        digitalWrite(PINRELIEFVALVE, LOW);
        markSolenoidToggle();
        ControlData.reliefValve = false;
        timeToCloseRelief = 0;
        timeToFinishRelief += millis();
      }
    }
    else {
      digitalWrite(PINTRANSFERVALVE, LOW);
      digitalWrite(PINRELIEFVALVE, LOW);
      ControlData.transferValve = false;
      ControlData.reliefValve = false;
      if (timeToFinishRelief && millis() >= timeToFinishRelief) {
        timeToFinishRelief = 0;
      }
    }
    return true;
  }
  else
    return false;
}

void pressureRelief(bool fromVolumeDetermination) {
  if (timeToFinishRelief) { // if we're still in the middle of a relief, ignore new relief requests to avoid overlapping and potential hardware issues
    return;
  }

  if (fromVolumeDetermination) {
    const uint16_t recordIndex = pressureReliefIndex;
    PressureReliefRecord &record = pressureReliefHistory[recordIndex];
    record.timestamp[0] = '\0';
    NTPFormatedDateTime(record.timestamp);
    record.temperature = ControlData.temperature;
    record.pressureBefore = ControlData.pressure;
    record.pressureAfter = 0.0f;

    pendingReliefIndex = recordIndex;

    volumeAwaitingRecord = true;
    volumeRecordIndex = recordIndex;

    pressureReliefIndex = (pressureReliefIndex + 1) % 1000;
    if (pressureReliefCount < 1000) {
      pressureReliefCount++;
    }
  }

  const float pressureSeconds = (ControlData.pressure > 0.0f) ? ControlData.pressure : 0.0f;
  const unsigned long extraMs = (unsigned long)(pressureSeconds * 1000.0L) / (debugging ? 10 : 1);

  timeToOpenTransfer     = millis() + holdPressureDueToTemperatureRelays();
  timeToCloseTransfer    = TRANSFERTIME + extraMs;
  timeToOpenRelief       = SOLENOIDCLOSINGTIME;
  timeToCloseRelief      = RELIEFTIME + extraMs;
  timeToFinishRelief     = 1000;
  timeToRegisterPressure_ = 0; // garante que estado anterior não vaza para novo ciclo

  // NÃO chamar processPressureRelays() aqui:
  // pressureRelief() é chamado do async web task (core 0) e processPressureRelays()
  // também é chamado do main loop (core 1) via pressureControl().
  // Chamar aqui cria uma race condition onde ambos executam stage 1 simultaneamente
  // e o += millis() de timeToCloseTransfer é aplicado duas vezes,
  // resultando em timeToCloseTransfer ≈ 2*millis()+5000 → transfer fica aberto por ~16min.
  // O main loop chamará processPressureRelays() dentro de milissegundos.
}

void handlePressureHistoryCSV(AsyncWebServerRequest *request) {
  String csv;
  csv.reserve(4096);

  csv += "data_hora;temperatura;pressao_antes;pressao_depois;patm;relief_volume;volume_estimado\n";

  uint16_t available = pressureReliefCount;
  if (available > 100) {
    available = 100;
  }

  if (available > 0) {
    uint16_t startIndex = (pressureReliefIndex + 1000 - available) % 1000;

    for (uint16_t i = 0; i < available; ++i) {
      const uint16_t idx = (startIndex + i) % 1000;
      const PressureReliefRecord &record = pressureReliefHistory[idx];

      const char *dateBuf = record.timestamp[0] ? record.timestamp : "0";

      const float denom = record.pressureBefore - record.pressureAfter;
      float volumeEstimated = 0.0f;
      if (fabsf(denom) > 0.0001f) {
        volumeEstimated = (record.pressureAfter * FMTData.FMTReliefVolume) / denom;
      }

      char tempBuf[16];
      char pBeforeBuf[16];
      char pAfterBuf[16];
      char patmBuf[16];
      char reliefVolBuf[16];
      char volumeBuf[16];

      formatFloatCsv(tempBuf, sizeof(tempBuf), record.temperature, 2);
      formatFloatCsv(pBeforeBuf, sizeof(pBeforeBuf), record.pressureBefore, 3);
      formatFloatCsv(pAfterBuf, sizeof(pAfterBuf), record.pressureAfter, 3);
      formatFloatCsv(patmBuf, sizeof(patmBuf), Patm, 3);
      formatFloatCsv(reliefVolBuf, sizeof(reliefVolBuf), FMTData.FMTReliefVolume, 2);
      formatFloatCsv(volumeBuf, sizeof(volumeBuf), volumeEstimated, 2);

      csv += dateBuf;
      csv += ';';
      csv += tempBuf;
      csv += ';';
      csv += pBeforeBuf;
      csv += ';';
      csv += pAfterBuf;
      csv += ';';
      csv += patmBuf;
      csv += ';';
      csv += reliefVolBuf;
      csv += ';';
      csv += volumeBuf;
      csv += '\n';
    }
  }

  AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition", "attachment; filename=pressure_history.csv");
  request->send(response);
}

void handlePressureDumpCSV(AsyncWebServerRequest *request) {
  if (pressureDumpInProgress) {
    request->send(409, "text/plain", "Dump in progress");
    return;
  }

  pressureDumpInProgress = true;
  pressureDumpSamples = pressureSamples;
  pressureDumpCount = (pressureDumpSamples ? pressureSamplesCount : 0);
  pressureDumpIndex = 0;
  pressureDumpHeaderSent = false;
  pressureDumpDone = false;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv",
      [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        (void)index;

        if (!pressureDumpInProgress) {
          return 0;
        }

        if (pressureDumpDone) {
          pressureDumpDone = false;
          pressureDumpInProgress = false;
          if (pressureSamples) {
            delete[] pressureSamples;
            pressureSamples = nullptr;
          }
          pressureSamplesIndex = 0;
          pressureSamplesCount = 0;
          pressureLastSampleMillis = 0;
          pressureDumpSamples = nullptr;
          pressureDumpCount = 0;
          pressureDumpIndex = 0;
          pressureDumpHeaderSent = false;
          return 0;
        }

        size_t len = 0;
        if (!pressureDumpHeaderSent) {
          const char *header = "min_sec;millis;pressao\n";
          size_t headerLen = strlen(header);
          if (headerLen > maxLen) {
            headerLen = maxLen;
          }
          memcpy(buffer, header, headerLen);
          len += headerLen;
          pressureDumpHeaderSent = (headerLen == strlen(header));
          if (!pressureDumpHeaderSent) {
            return len;
          }
        }

        while (len < maxLen && pressureDumpIndex < pressureDumpCount && pressureDumpSamples) {
          const PressureSampleRecord &record = pressureDumpSamples[pressureDumpIndex];
          const char *dateBuf = record.timestamp[0] ? record.timestamp : "00:00";
          char pressBuf[16];
          formatFloatCsv(pressBuf, sizeof(pressBuf), record.pressure, 3);
          char line[64];
          int lineLen = snprintf(line, sizeof(line), "%s;%lu;%s\n", dateBuf, record.millisStamp, pressBuf);
          if (lineLen <= 0) {
            pressureDumpIndex++;
            continue;
          }
          if (len + (size_t)lineLen > maxLen) {
            break;
          }
          memcpy(buffer + len, line, (size_t)lineLen);
          len += (size_t)lineLen;
          pressureDumpIndex++;
        }

        if (pressureDumpIndex >= pressureDumpCount) {
          pressureDumpDone = true;
        }

        return len;
      });

  response->addHeader("Content-Disposition", "attachment; filename=pressure_dump.csv");
  request->send(response);
}


bool startVolumeDetermination(char *reason, size_t reasonSize) {
  if (reason && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (volumeDeterminationActive || timeToFinishRelief) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Process in progress");
    }
    return false;
  }

  if (FMTData.FMTReliefVolume <= 0.0f) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Missing FMTReliefVolume");
    }
    return false;
  }

  if (ControlData.pressure < 1.5f) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Insufficient pressure (min 1.5 bar)");
    }
    return false;
  }

  volumeDeterminationActive = true;
  volumeStartPressure = ControlData.pressure;
  volumeTargetPressure = volumeStartPressure - 1.0f;
  volumeLastReliefMillis = 0;
  volumeIteration = 0;
  volumeAwaitingRecord = false;
  volumeRecordIndex = -1;
  if (!pressureSamples) {
    pressureSamples = new(std::nothrow) PressureSampleRecord[PRESSURE_SAMPLES_MAX];
  }

  pressureSamplesIndex = 0;
  pressureSamplesCount = 0;
  pressureLastSampleMillis = 0;

  if (!pressureSamples) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Sem memoria para log");
    }
    volumeDeterminationActive = false;
    return false;
  }

  char line1[32];
  char line2[32];
  snprintf(line1, sizeof(line1), "Volume: START");
  snprintf(line2, sizeof(line2), "P ini: %.2f bar", volumeStartPressure);
  showVolumeStatus(line1, line2, "Waiting...");

  if (reason && reasonSize > 0) {
    snprintf(reason, reasonSize, "Process started successfully");
  }
  return true;
}

bool isVolumeDeterminationActive() {
  return volumeDeterminationActive;
}

void pressureControl() {
  if (beerSG == 0) {
    beerSG = BatchData.batchOG;
    beerPlato = SGToPlato(beerSG);
  }

  readPressure();
  if (!processPressureRelays()) {
    static unsigned long lastPressureCheckMillis = 0;
    if (MILLISDIFF(lastPressureCheckMillis, 1000)) {
      lastPressureCheckMillis = millis();
      processPressure(false);
    }
  }

  if (!pressureSensorUnstable) {
    if (volumeDeterminationActive) {
      if (ControlData.pressure > volumeTargetPressure) 
        pressureRelief(true);
    }
    else 
      if (SetPointData.setPointPressure > 0.0f && 
        ControlData.pressure < 2.5f &&
        ControlData.pressure > (SetPointData.setPointPressure / sqrt(pressureDropFactor))) 
          pressureRelief(false);
  }

  if (!pressureDumpInProgress && volumeDeterminationActive && pressureSamples) {
    const unsigned long now = millis();
    if (pressureLastSampleMillis == 0 || (now - pressureLastSampleMillis) >= PRESSURE_SAMPLE_MIN_MS) {
      if (pressureSamplesCount < PRESSURE_SAMPLES_MAX) {
        pressureLastSampleMillis = now;
        PressureSampleRecord &sample = pressureSamples[pressureSamplesIndex];
        char tempTs[32] = "";
        NTPFormatedDateTime(tempTs);
        const size_t len = strlen(tempTs);
        if (len >= 5) {
          strncpy(sample.timestamp, tempTs + (len - 5), sizeof(sample.timestamp) - 1);
          sample.timestamp[sizeof(sample.timestamp) - 1] = '\0';
        } else {
          strncpy(sample.timestamp, "00:00", sizeof(sample.timestamp));
          sample.timestamp[sizeof(sample.timestamp) - 1] = '\0';
        }
        sample.millisStamp = now;
        sample.pressure = ControlData.pressure;

        pressureSamplesIndex++;
        pressureSamplesCount = pressureSamplesIndex;
      }
    }
  }
}

char *getPressureControlStatus(char *st) {
  char tmp[160];
  st[0] = '\0';

  if (pressureSensorConnected) {
    snprintf(tmp, sizeof(tmp), "INA219 Pressure Sensor: OK%s<br>",
             pressureSensorUnstable ? " [INSTÁVEL]" : "");
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Instability: bad=%u good=%u<br>", pressureBadCount, pressureGoodCount);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Atmospheric pressure: %.3f bar<br>Current reading: %.2f mA<br>Calculated pressure: %.3f bar<br>",
             Patm, ina219.getCurrent_mA(), ControlData.pressure);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Pressure drop factor (%%): %.3f<br>Headspace volume: %.2f L<br>Beer volume: %.2f L<br>",
             pressureDropFactor * 100, headSpaceVolume, beerVolume);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Beer SG: %.4f<br>Beer ABV: %.2f%%<br>", beerSG, beerABV);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Bus voltage: %.2f V<br>Shunt voltage: %.2f mV<br>Power: %.2f mW<br>",
             ina219.getBusVoltage_V(), ina219.getShuntVoltage_mV(), ina219.getPower_mW());
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Relief count: %lu<br>Ejected CO2 mols: %.3f<br>Dissolved CO2 mols: %.3f<br>",
             (unsigned long)CountersData.totalReliefCount, CountersData.totalMolsEjected, dissolvedCO2Mols);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Headspace CO2 mols: %.3f<br>Total CO2 mols: %.3f<br>Total CO2 mass: %.2f g<br>",
             headSpaceCO2Mols, CountersData.totalCO2MolsProduced, CO2Mass());
    strncat(st, tmp, 2000);
  } else {
    snprintf(tmp, sizeof(tmp), "INA219 Pressure Sensor: DISCONNECTED<br>Atmospheric pressure: %.3f bar<br>", Patm);
    strncat(st, tmp, 2000);
  }

  // --- Diagnóstico de relés ---
  unsigned long now = millis();
  strncat(st, "<br>--- Relay cycle ---<br>", 2000);
  if (!timeToFinishRelief) {
    strncat(st, "Cycle: IDLE<br>", 2000);
  } else {
    strncat(st, "Cycle: ACTIVE<br>", 2000);
    if (timeToOpenTransfer) {
      snprintf(tmp, sizeof(tmp), "Stage: waiting to open transfer (in %ld ms)<br>",
               (long)(timeToOpenTransfer - now));
    } else if (timeToCloseTransfer) {
      snprintf(tmp, sizeof(tmp), "Stage: transfer OPEN - closes in %ld ms<br>",
               (long)(timeToCloseTransfer - now));
    } else if (timeToOpenRelief) {
      snprintf(tmp, sizeof(tmp), "Stage: waiting to open relief (in %ld ms)<br>",
               (long)(timeToOpenRelief - now));
    } else if (timeToRegisterPressure_) {
      snprintf(tmp, sizeof(tmp), "Stage: relief OPEN - measuring pressure in %ld ms<br>",
               (long)(timeToRegisterPressure_ - now));
    } else if (timeToCloseRelief) {
      snprintf(tmp, sizeof(tmp), "Stage: relief OPEN - closes in %ld ms<br>",
               (long)(timeToCloseRelief - now));
    } else {
      snprintf(tmp, sizeof(tmp), "Stage: finishing (cooldown %ld ms)<br>",
               (long)(timeToFinishRelief - now));
    }
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp),
             "timeToOpenTransfer=%lu timeToCloseTransfer=%lu<br>"
             "timeToOpenRelief=%lu timeToCloseRelief=%lu<br>"
             "timeToFinishRelief=%lu timeToRegister=%lu<br>",
             timeToOpenTransfer, timeToCloseTransfer,
             timeToOpenRelief, timeToCloseRelief,
             timeToFinishRelief, timeToRegisterPressure_);
    strncat(st, tmp, 2000);
  }

  return st;
}
