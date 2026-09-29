#include <Arduino.h>
#include <WiFi.h>
#include <IOTK_ESPAsyncServer.h> // Before IOTK_SimpleMail.h: keeps its dependencies in the graph.
#include <IOTK_SimpleMail.h>
#include "PovotoMail.h"

// SMTP (server, account, recipient) comes from IOTK_SimpleMail, as in
// BrewCore. sendSimpleMail() blocks for seconds, so it runs in its own task;
// a failure is only reported on Serial and never affects the control loop.

static constexpr size_t MAIL_SUBJECT_SIZE = 160;
static constexpr size_t MAIL_BODY_SIZE = 1536;
static constexpr int MAIL_QUEUE_LENGTH = 3;
static constexpr int MAIL_ATTEMPTS = 3;
static constexpr uint32_t MAIL_RETRY_DELAY_MS = 60000UL;
// TLS needs a large contiguous block; skip an attempt instead of failing inside it.
static constexpr uint32_t MAIL_MIN_FREE_BLOCK = 45000UL;

struct PovotoMail_t {
  char subject[MAIL_SUBJECT_SIZE];
  char body[MAIL_BODY_SIZE];
};

static QueueHandle_t mailQueue = nullptr;

static bool trySendMail(const PovotoMail_t &mail, int attempt) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[MAIL] attempt %d: WiFi not connected\n", attempt);
    return false;
  }
  const uint32_t freeBlock = ESP.getMaxAllocHeap();
  if (freeBlock < MAIL_MIN_FREE_BLOCK) {
    Serial.printf("[MAIL] attempt %d: not enough memory (%lu bytes)\n", attempt, (unsigned long)freeBlock);
    return false;
  }
  // SMTP requires CRLF line endings in the message body.
  String body(mail.body);
  body.replace("\r\n", "\n");
  body.replace("\n", "\r\n");
  const bool sent = sendSimpleMail(String(mail.subject), body, false);
  Serial.printf("[MAIL] attempt %d: %s\n", attempt, sent ? "sent" : "failed");
  return sent;
}

static void mailTask(void *) {
  static PovotoMail_t mail; // Too large for the task stack.
  for (;;) {
    if (xQueueReceive(mailQueue, &mail, portMAX_DELAY) != pdTRUE) continue;
    bool sent = false;
    for (int attempt = 1; attempt <= MAIL_ATTEMPTS && !sent; attempt++) {
      if (attempt > 1) vTaskDelay(pdMS_TO_TICKS(MAIL_RETRY_DELAY_MS));
      sent = trySendMail(mail, attempt);
    }
    if (!sent) Serial.printf("[MAIL] giving up: %s\n", mail.subject);
  }
}

static bool startMailTask() {
  if (mailQueue) return true;
  mailQueue = xQueueCreate(MAIL_QUEUE_LENGTH, sizeof(PovotoMail_t));
  if (!mailQueue) return false;
  // Core 0, low priority: the control loop runs on core 1. TLS needs a deep stack.
  if (xTaskCreatePinnedToCore(mailTask, "povotoMail", 16384, nullptr, 1, nullptr, 0) != pdPASS) {
    vQueueDelete(mailQueue);
    mailQueue = nullptr;
    return false;
  }
  return true;
}

bool queuePovotoMail(const char *subject, const String &body) {
  if (!startMailTask()) {
    Serial.println("[MAIL] could not start the mail task");
    return false;
  }
  static PovotoMail_t mail; // Only called from loop().
  strlcpy(mail.subject, subject, sizeof(mail.subject));
  strlcpy(mail.body, body.c_str(), sizeof(mail.body));
  if (xQueueSend(mailQueue, &mail, 0) != pdTRUE) {
    Serial.printf("[MAIL] queue full, dropped: %s\n", mail.subject);
    return false;
  }
  return true;
}
