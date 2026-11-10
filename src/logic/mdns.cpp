#include "logic/mdns.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
constexpr size_t HEADER_LEN = 12;
constexpr uint16_t FLAG_QR = 0x8000;
constexpr uint16_t CLASS_IN = 1;

uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

bool fullName(const char* host, char* out, size_t outSize) {
    const int n = snprintf(out, outSize, "%s.local", host);
    return n > 0 && (size_t)n < outSize;
}

// Writes "<host>.local" as DNS labels. Returns bytes written or 0.
size_t writeName(const char* host, uint8_t* out, size_t cap) {
    char name[MDNS_NAME_MAX + 1];
    if (!fullName(host, name, sizeof(name))) return 0;
    size_t n = 0;
    const char* p = name;
    while (*p) {
        const char* dot = strchr(p, '.');
        const size_t labelLen = dot ? (size_t)(dot - p) : strlen(p);
        if (labelLen == 0 || labelLen > 63 || n + 1 + labelLen + 1 > cap) return 0;
        out[n++] = (uint8_t)labelLen;
        memcpy(out + n, p, labelLen);
        n += labelLen;
        p += labelLen;
        if (*p == '.') ++p;
    }
    out[n++] = 0;
    return n;
}

bool skipRecord(const uint8_t* pkt, size_t len, size_t* off, bool question, char* name,
                size_t nameSize, uint16_t* type, uint16_t* cls) {
    if (!dnsReadName(pkt, len, off, name, nameSize)) return false;
    const size_t fixed = question ? 4 : 10;
    if (*off + fixed > len) return false;
    *type = rd16(pkt + *off);
    *cls = rd16(pkt + *off + 2);
    if (question) {
        *off += 4;
        return true;
    }
    const uint16_t rdlen = rd16(pkt + *off + 8);
    *off += 10;
    if (*off + rdlen > len) return false;
    *off += rdlen;
    return true;
}
}  // namespace

bool dnsReadName(const uint8_t* pkt, size_t len, size_t* off, char* out, size_t outSize) {
    if (outSize == 0) return false;
    size_t pos = *off;
    size_t n = 0;
    bool jumped = false;
    int hops = 0;
    out[0] = '\0';
    while (true) {
        if (pos >= len) return false;
        const uint8_t l = pkt[pos];
        if (l == 0) {
            if (!jumped) *off = pos + 1;
            break;
        }
        if ((l & 0xC0) == 0xC0) {
            if (pos + 1 >= len || ++hops > 16) return false;
            const size_t target = ((size_t)(l & 0x3F) << 8) | pkt[pos + 1];
            if (!jumped) *off = pos + 2;
            jumped = true;
            pos = target;
            continue;
        }
        if ((l & 0xC0) != 0 || pos + 1 + l > len) return false;
        if (n + (n ? 1 : 0) + l + 1 > outSize) return false;
        if (n) out[n++] = '.';
        memcpy(out + n, pkt + pos + 1, l);
        n += l;
        out[n] = '\0';
        pos += 1 + l;
    }
    return true;
}

bool mdnsQueryAsksFor(const uint8_t* pkt, size_t len, const char* host, bool* unicastReply) {
    if (!pkt || len < HEADER_LEN) return false;
    if (rd16(pkt + 2) & FLAG_QR) return false;
    char want[MDNS_NAME_MAX + 1];
    if (!fullName(host, want, sizeof(want))) return false;
    const uint16_t qd = rd16(pkt + 4);
    size_t off = HEADER_LEN;
    for (uint16_t i = 0; i < qd; ++i) {
        char name[MDNS_NAME_MAX + 1];
        uint16_t type = 0;
        uint16_t cls = 0;
        if (!skipRecord(pkt, len, &off, true, name, sizeof(name), &type, &cls)) return false;
        if ((cls & 0x7FFF) == CLASS_IN && (type == DNS_TYPE_A || type == DNS_TYPE_ANY) &&
            strcasecmp(name, want) == 0) {
            if (unicastReply) *unicastReply = (cls & 0x8000) != 0;
            return true;
        }
    }
    return false;
}

bool mdnsResponseAnswers(const uint8_t* pkt, size_t len, const char* host) {
    if (!pkt || len < HEADER_LEN) return false;
    if (!(rd16(pkt + 2) & FLAG_QR)) return false;
    char want[MDNS_NAME_MAX + 1];
    if (!fullName(host, want, sizeof(want))) return false;
    const uint16_t qd = rd16(pkt + 4);
    const uint32_t records = (uint32_t)rd16(pkt + 6) + rd16(pkt + 8) + rd16(pkt + 10);
    size_t off = HEADER_LEN;
    char name[MDNS_NAME_MAX + 1];
    uint16_t type = 0;
    uint16_t cls = 0;
    for (uint16_t i = 0; i < qd; ++i) {
        if (!skipRecord(pkt, len, &off, true, name, sizeof(name), &type, &cls)) return false;
    }
    for (uint32_t i = 0; i < records; ++i) {
        if (!skipRecord(pkt, len, &off, false, name, sizeof(name), &type, &cls)) return false;
        if (type == DNS_TYPE_A && (cls & 0x7FFF) == CLASS_IN && strcasecmp(name, want) == 0) {
            return true;
        }
    }
    return false;
}

size_t mdnsBuildAnswer(const char* host, uint32_t ip, uint32_t ttl, uint8_t* out, size_t cap) {
    if (cap < HEADER_LEN) return 0;
    memset(out, 0, HEADER_LEN);
    wr16(out + 2, 0x8400);  // response, authoritative
    wr16(out + 6, 1);       // one answer
    size_t n = HEADER_LEN;
    const size_t nameLen = writeName(host, out + n, cap - n);
    if (!nameLen || n + nameLen + 14 > cap) return 0;
    n += nameLen;
    wr16(out + n, DNS_TYPE_A);
    wr16(out + n + 2, 0x8000 | CLASS_IN);  // cache-flush
    out[n + 4] = (uint8_t)(ttl >> 24);
    out[n + 5] = (uint8_t)(ttl >> 16);
    out[n + 6] = (uint8_t)(ttl >> 8);
    out[n + 7] = (uint8_t)ttl;
    wr16(out + n + 8, 4);
    out[n + 10] = (uint8_t)(ip >> 24);
    out[n + 11] = (uint8_t)(ip >> 16);
    out[n + 12] = (uint8_t)(ip >> 8);
    out[n + 13] = (uint8_t)ip;
    return n + 14;
}

size_t mdnsBuildQuery(const char* host, uint8_t* out, size_t cap) {
    if (cap < HEADER_LEN) return 0;
    memset(out, 0, HEADER_LEN);
    wr16(out + 4, 1);
    size_t n = HEADER_LEN;
    const size_t nameLen = writeName(host, out + n, cap - n);
    if (!nameLen || n + nameLen + 4 > cap) return 0;
    n += nameLen;
    wr16(out + n, DNS_TYPE_A);
    wr16(out + n + 2, CLASS_IN);
    return n + 4;
}
