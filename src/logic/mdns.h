#pragma once

#include <cstddef>
#include <cstdint>

// Minimal mDNS codec for the fallback responder: answers "<host>.local" A
// queries on the USB device's behalf. Pure logic. IPs are host-order.

constexpr uint16_t MDNS_PORT = 5353;
constexpr uint16_t DNS_TYPE_A = 1;
constexpr uint16_t DNS_TYPE_ANY = 255;
constexpr size_t MDNS_NAME_MAX = 255;

// Reads a (possibly compressed) DNS name at `*off` as dotted text and moves
// `*off` past it. Returns false on malformed input.
bool dnsReadName(const uint8_t* pkt, size_t len, size_t* off, char* out, size_t outSize);

// True when `pkt` is a query with a question for A or ANY of "<host>.local".
// `unicastReply` is set when that question has the QU bit.
bool mdnsQueryAsksFor(const uint8_t* pkt, size_t len, const char* host, bool* unicastReply);

// True when `pkt` is a response carrying an A record for "<host>.local".
bool mdnsResponseAnswers(const uint8_t* pkt, size_t len, const char* host);

// Builds an authoritative response with one A record (cache-flush set).
size_t mdnsBuildAnswer(const char* host, uint32_t ip, uint32_t ttl, uint8_t* out, size_t cap);

// Builds a query for the A record of "<host>.local".
size_t mdnsBuildQuery(const char* host, uint8_t* out, size_t cap);
