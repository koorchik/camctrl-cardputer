#include "logic/web_text.h"

#include <cstdio>
#include <cstring>

namespace {
int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool append(char* out, size_t outSize, size_t& n, const char* s) {
    const size_t k = strlen(s);
    if (n + k + 1 > outSize) {
        out[n] = '\0';
        return false;
    }
    memcpy(out + n, s, k);
    n += k;
    out[n] = '\0';
    return true;
}
}  // namespace

bool urlDecode(const char* src, size_t len, char* out, size_t outSize) {
    if (outSize == 0) return false;
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= len) return false;
            const int hi = hexVal(src[i + 1]);
            const int lo = hexVal(src[i + 2]);
            if (hi < 0 || lo < 0) return false;
            c = (char)(hi * 16 + lo);
            i += 2;
        }
        if (n + 1 >= outSize) return false;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

bool formField(const char* body, const char* key, char* out, size_t outSize) {
    if (!body || !key || outSize == 0) return false;
    const size_t keyLen = strlen(key);
    const char* p = body;
    while (*p) {
        const char* end = strchr(p, '&');
        const size_t pairLen = end ? (size_t)(end - p) : strlen(p);
        const char* eq = (const char*)memchr(p, '=', pairLen);
        const size_t nameLen = eq ? (size_t)(eq - p) : pairLen;
        if (nameLen == keyLen && memcmp(p, key, keyLen) == 0) {
            if (!eq) {
                out[0] = '\0';
                return true;
            }
            return urlDecode(eq + 1, pairLen - nameLen - 1, out, outSize);
        }
        if (!end) break;
        p = end + 1;
    }
    return false;
}

bool htmlEscape(const char* in, char* out, size_t outSize) {
    if (outSize == 0) return false;
    size_t n = 0;
    out[0] = '\0';
    for (; *in; ++in) {
        const char* rep = nullptr;
        char one[2] = {*in, '\0'};
        switch (*in) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = "&quot;"; break;
            case '\'': rep = "&#39;"; break;
            default: rep = one; break;
        }
        if (!append(out, outSize, n, rep)) return false;
    }
    return true;
}

bool jsonEscape(const char* in, char* out, size_t outSize) {
    if (outSize == 0) return false;
    size_t n = 0;
    out[0] = '\0';
    for (; *in; ++in) {
        char buf[8];
        const unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            buf[0] = '\\';
            buf[1] = (char)c;
            buf[2] = '\0';
        } else if (c < 0x20) {
            snprintf(buf, sizeof(buf), "\\u%04x", c);
        } else {
            buf[0] = (char)c;
            buf[1] = '\0';
        }
        if (!append(out, outSize, n, buf)) return false;
    }
    return true;
}
