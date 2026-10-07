#ifndef POVOTO_CLOUD_JSON_H
#define POVOTO_CLOUD_JSON_H

// Small JSON writer and reader for the cloud lines (CloudLog, CloudSync): flat
// objects, numbers, strings and one level of nested object.

#include <Arduino.h>
#include <math.h>

// Appends to a fixed buffer; `ok` turns false once anything was cut.
struct JsonOut {
  char *buf;
  size_t size;
  size_t used;
  bool ok;

  JsonOut(char *buffer, size_t capacity) : buf(buffer), size(capacity), used(0), ok(capacity > 0) {
    if (ok) buf[0] = '\0';
  }

  void raw(const char *text) {
    const size_t length = strlen(text);
    if (!ok || used + length >= size) {
      ok = false;
      return;
    }
    memcpy(buf + used, text, length + 1);
    used += length;
  }
  // A key after "{" gets no comma.
  void key(const char *name) {
    raw(used > 0 && buf[used - 1] != '{' ? ",\"" : "\"");
    raw(name);
    raw("\":");
  }
  void number(const char *name, float value, int decimals) {
    key(name);
    if (!isfinite(value)) {
      raw("null");
      return;
    }
    char text[24];
    snprintf(text, sizeof(text), "%.*f", decimals, value);
    raw(text);
  }
  void integer(const char *name, long value) {
    key(name);
    char text[16];
    snprintf(text, sizeof(text), "%ld", value);
    raw(text);
  }
  void unsignedInteger(const char *name, unsigned long value) {
    key(name);
    char text[16];
    snprintf(text, sizeof(text), "%lu", value);
    raw(text);
  }
  void string(const char *name, const char *value) {
    key(name);
    raw("\"");
    char one[2] = {0, 0};
    for (const char *c = value; *c; ++c) {
      if (*c == '"' || *c == '\\') {
        raw("\\");
      } else if ((unsigned char)*c < 0x20) {
        continue; // control characters never belong in a name
      }
      one[0] = *c;
      raw(one);
    }
    raw("\"");
  }
};

// Reader. Every function looks only at the keys of the outer object (a key of a
// nested object is not found), so "t" of the request and "t" of its data
// cannot be confused.

// Start of the value of key, or nullptr.
const char *jsonFind(const char *json, const char *key);
// Number; null (or missing, when allowMissing) gives NAN. False when invalid.
bool jsonNumber(const char *json, const char *key, float &value, bool allowMissing = true);
bool jsonInteger(const char *json, const char *key, long &value);
// String value, unescaped (\uXXXX becomes UTF-8). False when missing/invalid.
bool jsonString(const char *json, const char *key, char *out, size_t size);
// Raw text of a nested object value, braces included.
bool jsonObject(const char *json, const char *key, char *out, size_t size);

#endif
