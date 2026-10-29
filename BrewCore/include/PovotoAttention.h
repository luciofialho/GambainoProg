#pragma once
#include <stddef.h>
#include <stdint.h>
void povotoAttentionBegin();
bool povotoAttentionReceive(const char *payload);
void requestAttentionLed(uint32_t durationMs, bool onlyIfIdle = false);
void attentionLedProcess();
void povotoAttentionStatus(char *out, size_t size);
