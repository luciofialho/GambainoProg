#pragma once
#include <stddef.h>
#include <stdint.h>
void healthBegin();
void healthReceive(const char *payload, const uint8_t *mac);
void healthProcess();
void healthSendMail(); // LogSend only: SMTP and HTTPS never run concurrently.
void healthSerial(const char *payload);
void healthStatus(char *out, size_t size);
