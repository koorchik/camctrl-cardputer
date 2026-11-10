#pragma once

#include <cstddef>

// Text helpers for the settings page. Pure logic.

// Finds `key` in an application/x-www-form-urlencoded body and writes its
// decoded value (NUL-terminated) into `out`. Returns false if the key is
// missing or the value does not fit.
bool formField(const char* body, const char* key, char* out, size_t outSize);

// Decodes %XX and '+' in place over [src, src+len) into `out`. Returns false
// on a bad escape or when the result does not fit.
bool urlDecode(const char* src, size_t len, char* out, size_t outSize);

// Escapes &, <, >, " and ' for HTML text and attribute values. Returns false
// when the result does not fit (out is then truncated but terminated).
bool htmlEscape(const char* in, char* out, size_t outSize);

// Escapes a string for a JSON string literal (without the quotes).
bool jsonEscape(const char* in, char* out, size_t outSize);
