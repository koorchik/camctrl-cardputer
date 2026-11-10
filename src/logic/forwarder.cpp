#include "logic/forwarder.h"

#include <cstring>

bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
    return memcmp(a, b, 6) == 0;
}

Forwarder::Forwarder(const uint8_t localMac[6]) {
    memcpy(localMac_, localMac, 6);
    memset(table_, 0, sizeof(table_));
}

const Forwarder::Entry* Forwarder::find(const uint8_t mac[6], uint32_t nowMs) const {
    for (const Entry& e : table_) {
        if (e.used && macEqual(e.mac, mac) && nowMs - e.seenMs < FORWARDER_AGE_MS) return &e;
    }
    return nullptr;
}

void Forwarder::learn(const uint8_t mac[6], Port p, uint32_t nowMs) {
    if (macIsGroup(mac) || macEqual(mac, localMac_)) return;
    Entry* slot = nullptr;
    for (Entry& e : table_) {
        if (e.used && macEqual(e.mac, mac)) {
            slot = &e;
            break;
        }
    }
    if (!slot) {
        Entry* oldest = &table_[0];
        for (Entry& e : table_) {
            if (!e.used || nowMs - e.seenMs >= FORWARDER_AGE_MS) {
                slot = &e;
                break;
            }
            if (nowMs - e.seenMs > nowMs - oldest->seenMs) oldest = &e;
        }
        if (!slot) slot = oldest;
    }
    memcpy(slot->mac, mac, 6);
    slot->port = p;
    slot->used = true;
    slot->seenMs = nowMs;
}

PortMask Forwarder::route(Port in, const uint8_t* frame, size_t len, uint32_t nowMs) {
    if (!frame || len < ETH_HEADER_LEN) return 0;
    const uint8_t* dst = frame;
    const uint8_t* src = frame + 6;
    if (in != PORT_LOCAL) learn(src, in, nowMs);

    const PortMask others = ALL_PORTS & ~portBit(in);
    if (macIsGroup(dst)) return others;
    if (macEqual(dst, localMac_)) return in == PORT_LOCAL ? 0 : portBit(PORT_LOCAL);
    if (const Entry* e = find(dst, nowMs)) return e->port == in ? 0 : portBit(e->port);
    return others & ~portBit(PORT_LOCAL);
}

bool Forwarder::lookup(const uint8_t mac[6], Port& port, uint32_t nowMs) const {
    const Entry* e = find(mac, nowMs);
    if (!e) return false;
    port = e->port;
    return true;
}

void Forwarder::forgetPort(Port p) {
    for (Entry& e : table_) {
        if (e.used && e.port == p) e.used = false;
    }
}
