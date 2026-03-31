#pragma once

// Fermenter transfer communication via ESP-NOW
void sendTransferStartPacket();
void sendTransferEndPacket();
void brewCoreHandleFermTemp(char type, const char *payload);
void checkFermTempTimeout();
void sendEnvTempPacket();  // broadcast EnvironmentTemp every 60s
