/*// ANÁLISE DE PROBLEMAS E MELHORIAS PARA sendSerial2
// Arquivo: GambainoCommon_Fixed.cpp
//
// PROBLEMAS IDENTIFICADOS:
// 1. Buffer de 2049 bytes no stack - muito grande, pode causar stack overflow
// 2. Falta de proteção contra chamadas concorrentes
// 3. Chamadas frequentes em contexto de performance crítica
// 4. Falta de verificação de estado do Serial2
// 5. Possível blocking em Serial2.write() causando watchdog

#include "GambainoCommon.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Buffer estático para evitar stack overflow
static char sendBuffer[MAXPACKETSIZE + 1];
// Mutex para proteger contra chamadas concorrentes
static SemaphoreHandle_t serial2Mutex = NULL;
// Counter para detectar chamadas muito frequentes
static unsigned long lastSendTime = 0;
static int sendCount = 0;

// Versão melhorada da função sendSerial2
void sendSerial2_IMPROVED(const char packetType, const char *fmt, ...) {
  // Proteção contra chamadas muito frequentes (anti-flood)
  unsigned long now = millis();
  if (now - lastSendTime < 50) { // Mínimo 50ms entre chamadas
    sendCount++;
    if (sendCount > 10) {
      // Muitas chamadas em sequência, pode estar causando problemas
      Serial.println("WARNING: sendSerial2 flooding detected, skipping");
      return;
    }
  } else {
    sendCount = 0;
  }
  lastSendTime = now;

  // Inicializa mutex se necessário
  if (serial2Mutex == NULL) {
    serial2Mutex = xSemaphoreCreateMutex();
    if (serial2Mutex == NULL) {
      Serial.println("ERROR: Failed to create Serial2 mutex");
      return;
    }
  }

  // Tenta obter o mutex (timeout de 100ms para evitar travamento)
  if (xSemaphoreTake(serial2Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("WARNING: Serial2 mutex timeout");
    return;
  }

  // Verifica se há espaço no buffer de transmissão do Serial2
  if (Serial2.availableForWrite() < 50) {
    // Buffer quase cheio, libera mutex e sai para evitar blocking
    xSemaphoreGive(serial2Mutex);
    Serial.println("WARNING: Serial2 TX buffer full");
    return;
  }

  // Formata string no buffer estático
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(sendBuffer, sizeof(sendBuffer), fmt, args);
  va_end(args);

  // Verificações de segurança
  if (len <= 0 || len >= (int)sizeof(sendBuffer)) {
    xSemaphoreGive(serial2Mutex);
    Serial.println("ERROR: vsnprintf failed or buffer overflow");
    return;
  }

  uint16_t length = (uint16_t)len;
  if (length > MAXPACKETSIZE) {
    xSemaphoreGive(serial2Mutex);
    Serial.println("ERROR: Packet size exceeds MAXPACKETSIZE");
    return;
  }

  // Watchdog feed antes de operações que podem demorar
  esp_task_wdt_reset();

  // Tag
  switch (packetType) {
    case SENTINELPACKET:        Serial2.write(kTagSntnl); break;
    case LOGPACKET:             Serial2.write(kTagLogPk); break;
    case RCCOMMANDPACKET:       Serial2.write(kTagCmdPk); break;
    case INSTABILITYMSGPACKET:  Serial2.write(kTagInstb); break;
    default: 
      xSemaphoreGive(serial2Mutex);
      Serial.println("ERROR: Unknown packet type");
      return;
  }

  // Length (payload only)
  Serial2.write((uint8_t)(length >> 8));
  Serial2.write((uint8_t)(length & 0xFF));

  // Payload em chunks para evitar blocking longo
  const int CHUNK_SIZE = 64;
  for (int i = 0; i < length; i += CHUNK_SIZE) {
    int chunkLen = min(CHUNK_SIZE, length - i);
    Serial2.write((uint8_t *)(sendBuffer + i), chunkLen);
    
    // Yield entre chunks para não travar sistema
    if (i + CHUNK_SIZE < length) {
      yield();
      esp_task_wdt_reset();
    }
  }

  // CRC16 (over payload)
  uint16_t crc = crc16_ccitt_false((uint8_t*)sendBuffer, length);
  Serial2.write((uint8_t)(crc >> 8));      // CRC MSB
  Serial2.write((uint8_t)(crc & 0xFF));    // CRC LSB

  // Força envio imediato se possível
  Serial2.flush();

  // Libera mutex
  xSemaphoreGive(serial2Mutex);

  // Feed watchdog após operação completa
  esp_task_wdt_reset();
}

// MELHORIAS ADICIONAIS RECOMENDADAS:

// 1. Adicionar timeout nas chamadas Serial2.write()
// 2. Implementar buffer circular para casos de alta frequência
// 3. Mover para task dedicada com queue se necessário
// 4. Adicionar estatísticas de uso para debug
// 5. Considerar usar DMA para transmissão Serial

// USAGE:
// Substituir todas as chamadas de sendSerial2() por sendSerial2_IMPROVED()
// ou renomear a função original e usar esta implementação


A antiga era

void sendSerial2(const char packetType, const char *fmt, ...) {
  char buf[MAXPACKETSIZE + 1];

  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  uint16_t length = (uint16_t)strlen(buf);
  if (length == 0 || length > MAXPACKETSIZE) return;

  // Tag
  switch (packetType) {
    case SENTINELPACKET:        Serial2.write(kTagSntnl); break;
    case LOGPACKET:             Serial2.write(kTagLogPk); break;
    case RCCOMMANDPACKET:       Serial2.write(kTagCmdPk); break;
    case INSTABILITYMSGPACKET:  Serial2.write(kTagInstb); break;
    default: return;
  }

  // Length (payload only)
  Serial2.write((uint8_t)(length >> 8));
  Serial2.write((uint8_t)(length & 0xFF));

  // Payload
  Serial2.write((uint8_t *)buf, length);

  // CRC16 (over payload)
  uint16_t crc = crc16_ccitt_false((uint8_t*)buf, length);
  Serial2.write((uint8_t)(crc >> 8));      // CRC MSB
  Serial2.write((uint8_t)(crc & 0xFF));    // CRC LSB
}




*/