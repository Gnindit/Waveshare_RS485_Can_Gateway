#pragma once
#include <Arduino.h>

inline String jsonQuote(const String &value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t ch = value[i];
    if (ch == '\\' || ch == '"') { result += '\\'; result += char(ch); }
    else if (ch < 32) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
      result += escaped;
    } else result += char(ch);
  }
  return result + '"';
}
