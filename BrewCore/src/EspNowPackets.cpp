#include "EspNowPackets.h"
#include "GambainoCommon.h"
#include <Variables.h>
#include <Status.h>
#include <UI.h>

unsigned long lastFermTempMs     = 0;  // millis of last FERMERTEMPPACKET received
static int           lastFermTempPovIdx = -1; // which Povoto last sent a temp

void sendTransferStartPacket() {
  int idx = int(RcpTargetFermenter) - 1;  // RcpTargetFermenter is 1-based
  if (idx >= 0 && idx < MAXFMTS) {
    char payload[32];
    snprintf(payload, sizeof(payload), "%.0f,%.1f", float(RcpBatchNum), float(RcpInoculationTemp));
    sendEspNow(peerPovotos[idx].mac, 0, false, (uint8_t)TRANSFERSTARTPACKET, payload);
    Serial.printf("TransferStart->Povoto[%d]: %s\n", idx, payload);
  }
  lastFermTempMs = millis();
}

void sendTransferEndPacket() {
  int idx = int(RcpTargetFermenter) - 1;  // RcpTargetFermenter is 1-based
  if (idx >= 0 && idx < MAXFMTS) {
    sendEspNow(peerPovotos[idx].mac, 0, false, (uint8_t)TRANSFERENDPACKET, "end");
    Serial.printf("TransferEnd->Povoto[%d]\n", idx);
  }
}

void brewCoreHandleFermTemp(char type, const char *payload) {
  if (type != FERMERTEMPPACKET) return;
  char buf[32];
  strncpy(buf, payload, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char *comma = strchr(buf, ',');
  if (!comma) return;
  *comma = '\0';
  int   idx  = atoi(buf);
  float temp = atof(comma + 1);
  FermenterTemp = temp;
  lastFermTempMs     = millis();
  lastFermTempPovIdx = idx;
  Serial.printf("FermTemp Povoto[%d]: %.1f C\n", idx, temp);
  // If BrewCore is in standby, the Povoto didn't get the transfer-end — resend it.
  // idx is 1-based (= FMTData.PovotoNum), peerPovotos[] is 0-based.
  if (int(Status) == STANDBY && idx >= 1 && idx <= MAXFMTS) {
    sendEspNow(peerPovotos[idx-1].mac, 0, false, (uint8_t)TRANSFERENDPACKET, "end");
    Serial.printf("STANDBY: resent TransferEnd to Povoto[%d]\n", idx);
  }
}


void checkFermTempTimeout() {
  if (lastFermTempMs != 0 && MILLISDIFF(lastFermTempMs, 2UL*60*1000)) {
    lastFermTempMs = millis();  // reset so alert fires every 2 min, not every cycle
    setAlertMessage("No fermenter temperature from Povoto for 2 min - retrying");
    sendTransferStartPacket();
  }
}

void sendEnvTempPacket() {
  static unsigned long lastSentMs = 0;
  if (lastSentMs != 0 && !MILLISDIFF(lastSentMs, 5*60UL*1000)) return;
  lastSentMs = millis();
  static const uint8_t broadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  char payload[16];
  snprintf(payload, sizeof(payload), "%.1f", float(EnvironmentTemp));
  sendEspNow(broadcastMac, 0, false, (uint8_t)ENVTEMPPACKET, payload);
}
