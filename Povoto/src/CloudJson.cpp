#include "CloudJson.h"

#include <stdlib.h>
#include <string.h>

namespace {
// End of the string that starts at the opening quote (points past the
// closing quote), or nullptr.
const char *skipString(const char *p) {
  for (++p; *p; ++p) {
    if (*p == '\\') {
      if (!*++p) return nullptr;
    }
    else if (*p == '"') return p + 1;
  }
  return nullptr;
}

const char *skipSpaces(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  return p;
}

// End of the value that starts at p (object, string or scalar), or nullptr.
const char *skipValue(const char *p) {
  if (*p == '"') return skipString(p);
  if (*p == '{' || *p == '[') {
    int depth = 0;
    for (; *p; ++p) {
      if (*p == '"') {
        p = skipString(p);
        if (!p) return nullptr;
        --p;
      }
      else if (*p == '{' || *p == '[') ++depth;
      else if (*p == '}' || *p == ']') {
        if (--depth == 0) return p + 1;
      }
    }
    return nullptr;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ') ++p;
  return p;
}

void appendUtf8(char *out, size_t size, size_t &used, unsigned long code) {
  char bytes[4];
  size_t count;
  if (code < 0x80) { bytes[0] = (char)code; count = 1; }
  else if (code < 0x800) {
    bytes[0] = (char)(0xC0 | (code >> 6));
    bytes[1] = (char)(0x80 | (code & 0x3F));
    count = 2;
  }
  else {
    bytes[0] = (char)(0xE0 | (code >> 12));
    bytes[1] = (char)(0x80 | ((code >> 6) & 0x3F));
    bytes[2] = (char)(0x80 | (code & 0x3F));
    count = 3;
  }
  for (size_t i = 0; i < count && used + 1 < size; ++i) out[used++] = bytes[i];
}
} // namespace

const char *jsonFind(const char *json, const char *key) {
  const char *p = skipSpaces(json);
  if (*p != '{') return nullptr;
  const size_t keyLength = strlen(key);
  ++p;
  while (true) {
    p = skipSpaces(p);
    if (*p != '"') return nullptr;
    const char *nameStart = p + 1;
    const char *nameEnd = skipString(p);
    if (!nameEnd) return nullptr;
    const bool match = (size_t)(nameEnd - 1 - nameStart) == keyLength &&
                       !strncmp(nameStart, key, keyLength);
    p = skipSpaces(nameEnd);
    if (*p != ':') return nullptr;
    p = skipSpaces(p + 1);
    if (match) return p;
    p = skipValue(p);
    if (!p) return nullptr;
    p = skipSpaces(p);
    if (*p != ',') return nullptr;
    ++p;
  }
}

bool jsonNumber(const char *json, const char *key, float &value, bool allowMissing) {
  const char *p = jsonFind(json, key);
  if (!p) {
    value = NAN;
    return allowMissing;
  }
  if (!strncmp(p, "null", 4)) {
    value = NAN;
    return true;
  }
  char *end;
  const double parsed = strtod(p, &end);
  if (end == p || !isfinite(parsed)) return false;
  value = (float)parsed;
  return true;
}

bool jsonInteger(const char *json, const char *key, long &value) {
  const char *p = jsonFind(json, key);
  if (!p) return false;
  char *end;
  value = strtol(p, &end, 10);
  return end != p;
}

bool jsonString(const char *json, const char *key, char *out, size_t size) {
  const char *p = jsonFind(json, key);
  if (!p || *p != '"' || size == 0) return false;
  size_t used = 0;
  for (++p; *p && *p != '"'; ++p) {
    if (*p != '\\') {
      if (used + 1 < size) out[used++] = *p;
      continue;
    }
    ++p;
    switch (*p) {
      case 'n': case 't': case 'r': case 'b': case 'f':
        if (used + 1 < size) out[used++] = ' ';
        break;
      case 'u': {
        char hex[5] = {0};
        for (int i = 0; i < 4; ++i) {
          if (!p[1 + i]) return false;
          hex[i] = p[1 + i];
        }
        appendUtf8(out, size, used, strtoul(hex, nullptr, 16));
        p += 4;
        break;
      }
      case '\0':
        return false;
      default: // \" \\ \/
        if (used + 1 < size) out[used++] = *p;
    }
  }
  out[used] = '\0';
  return *p == '"';
}

bool jsonObject(const char *json, const char *key, char *out, size_t size) {
  const char *p = jsonFind(json, key);
  if (!p || *p != '{') return false;
  const char *end = skipValue(p);
  if (!end || (size_t)(end - p) >= size) return false;
  memcpy(out, p, end - p);
  out[end - p] = '\0';
  return true;
}
