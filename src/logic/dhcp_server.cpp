#include "logic/dhcp_server.h"

#include <cstring>

namespace {
constexpr size_t OFF_OP = 0;
constexpr size_t OFF_HTYPE = 1;
constexpr size_t OFF_HLEN = 2;
constexpr size_t OFF_XID = 4;
constexpr size_t OFF_FLAGS = 10;
constexpr size_t OFF_CIADDR = 12;
constexpr size_t OFF_YIADDR = 16;
constexpr size_t OFF_GIADDR = 24;
constexpr size_t OFF_CHADDR = 28;
constexpr size_t OFF_MAGIC = 236;
constexpr size_t OFF_OPTIONS = 240;
constexpr uint8_t MAGIC[4] = {0x63, 0x82, 0x53, 0x63};

constexpr uint8_t OPT_PAD = 0;
constexpr uint8_t OPT_SUBNET = 1;
constexpr uint8_t OPT_ROUTER = 3;
constexpr uint8_t OPT_HOSTNAME = 12;
constexpr uint8_t OPT_REQUESTED_IP = 50;
constexpr uint8_t OPT_LEASE_TIME = 51;
constexpr uint8_t OPT_MSG_TYPE = 53;
constexpr uint8_t OPT_SERVER_ID = 54;
constexpr uint8_t OPT_END = 255;

uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

bool expired(const DhcpLease& l, uint32_t nowSec) {
    return (int32_t)(l.expiresSec - nowSec) <= 0;
}

struct OptWriter {
    uint8_t* p;
    size_t cap;
    size_t len;
    bool ok;
    void put(uint8_t code, const uint8_t* v, uint8_t n) {
        if (len + 2u + n > cap) {
            ok = false;
            return;
        }
        p[len++] = code;
        p[len++] = n;
        memcpy(p + len, v, n);
        len += n;
    }
    void put8(uint8_t code, uint8_t v) { put(code, &v, 1); }
    void put32(uint8_t code, uint32_t v) {
        uint8_t b[4];
        wr32(b, v);
        put(code, b, 4);
    }
};
}  // namespace

int dhcpFindOption(const uint8_t* pkt, size_t len, uint8_t code, const uint8_t** value) {
    if (len < OFF_OPTIONS || memcmp(pkt + OFF_MAGIC, MAGIC, 4) != 0) return -1;
    size_t i = OFF_OPTIONS;
    while (i < len) {
        const uint8_t c = pkt[i];
        if (c == OPT_END) break;
        if (c == OPT_PAD) {
            ++i;
            continue;
        }
        if (i + 1 >= len) break;
        const uint8_t n = pkt[i + 1];
        if (i + 2 + n > len) break;
        if (c == code) {
            if (value) *value = pkt + i + 2;
            return n;
        }
        i += 2u + n;
    }
    return -1;
}

bool dhcpParse(const uint8_t* in, size_t len, DhcpRequestInfo& info) {
    memset(&info, 0, sizeof(info));
    if (!in || len < OFF_OPTIONS) return false;
    if (in[OFF_OP] != 1 || in[OFF_HTYPE] != 1 || in[OFF_HLEN] != 6) return false;
    if (memcmp(in + OFF_MAGIC, MAGIC, 4) != 0) return false;
    const uint8_t* v = nullptr;
    if (dhcpFindOption(in, len, OPT_MSG_TYPE, &v) != 1) return false;
    info.type = v[0];
    info.xid = rd32(in + OFF_XID);
    memcpy(info.mac, in + OFF_CHADDR, 6);
    info.ciaddr = rd32(in + OFF_CIADDR);
    if (dhcpFindOption(in, len, OPT_REQUESTED_IP, &v) == 4) info.requestedIp = rd32(v);
    if (dhcpFindOption(in, len, OPT_SERVER_ID, &v) == 4) info.serverId = rd32(v);
    const int n = dhcpFindOption(in, len, OPT_HOSTNAME, &v);
    if (n > 0) {
        size_t k = 0;
        for (int j = 0; j < n && k < DHCP_HOSTNAME_MAX; ++j) {
            const char c = (char)v[j];
            if (c >= 0x20 && c < 0x7F) info.hostname[k++] = c;
        }
        info.hostname[k] = '\0';
    }
    return true;
}

DhcpServer::DhcpServer(const DhcpConfig& cfg) : cfg_(cfg) {
    clear();
}

void DhcpServer::clear() {
    memset(leases_, 0, sizeof(leases_));
}

DhcpLease* DhcpServer::findByMac(const uint8_t mac[6], bool device) {
    // A lease belongs to a (MAC, side) pair: a Wi-Fi client spoofing the
    // device's MAC never touches the device's lease, and vice versa.
    for (DhcpLease& l : leases_) {
        if (l.used && l.device == device && memcmp(l.mac, mac, 6) == 0) return &l;
    }
    return nullptr;
}

bool DhcpServer::ipTaken(uint32_t ip, const uint8_t mac[6], uint32_t nowSec) const {
    for (const DhcpLease& l : leases_) {
        if (l.used && l.ip == ip && !expired(l, nowSec) && memcmp(l.mac, mac, 6) != 0) return true;
    }
    return false;
}

uint32_t DhcpServer::pickIp(const uint8_t mac[6], bool device, uint32_t requested,
                            uint32_t nowSec) {
    if (device) return cfg_.deviceIp;
    DhcpLease* existing = findByMac(mac, false);
    if (existing) return existing->ip;
    if (requested >= cfg_.poolStart && requested <= cfg_.poolEnd &&
        !ipTaken(requested, mac, nowSec)) {
        return requested;
    }
    for (uint32_t ip = cfg_.poolStart; ip <= cfg_.poolEnd; ++ip) {
        if (!ipTaken(ip, mac, nowSec)) return ip;
    }
    return 0;
}

DhcpLease* DhcpServer::bind(const uint8_t mac[6], uint32_t ip, bool device, uint32_t nowSec,
                            const char* hostname) {
    DhcpLease* l = findByMac(mac, device);
    if (!l) {
        for (DhcpLease& e : leases_) {
            if (!e.used || expired(e, nowSec)) {
                l = &e;
                break;
            }
        }
    }
    if (!l) {
        l = &leases_[0];
        for (DhcpLease& e : leases_) {
            if ((int32_t)(e.expiresSec - l->expiresSec) < 0) l = &e;
        }
    }
    // Only one device lease: a new device MAC replaces the old one.
    if (device) {
        for (DhcpLease& e : leases_) {
            if (&e != l && e.used && e.device) e.used = false;
        }
    }
    l->used = true;
    l->device = device;
    memcpy(l->mac, mac, 6);
    l->ip = ip;
    l->expiresSec = nowSec + cfg_.leaseSecs;
    if (hostname && hostname[0]) {
        strncpy(l->hostname, hostname, DHCP_HOSTNAME_MAX);
        l->hostname[DHCP_HOSTNAME_MAX] = '\0';
    }
    return l;
}

size_t DhcpServer::handle(const uint8_t* in, size_t inLen, bool fromDevice, uint32_t nowSec,
                          uint8_t* out, size_t outCap) {
    DhcpRequestInfo info;
    if (!dhcpParse(in, inLen, info)) return 0;
    if (outCap < DHCP_MIN_PACKET) return 0;

    uint8_t replyType = 0;
    uint32_t yiaddr = 0;
    bool withLease = true;

    switch (info.type) {
        case DHCP_DISCOVER: {
            yiaddr = pickIp(info.mac, fromDevice, info.requestedIp, nowSec);
            if (!yiaddr) return 0;
            replyType = DHCP_OFFER;
            break;
        }
        case DHCP_REQUEST: {
            if (info.serverId && info.serverId != cfg_.serverIp) {
                // The client chose another server: forget anything we offered.
                return 0;
            }
            const uint32_t wanted = info.requestedIp ? info.requestedIp : info.ciaddr;
            const uint32_t ours = pickIp(info.mac, fromDevice, wanted, nowSec);
            if (ours && wanted == ours) {
                bind(info.mac, ours, fromDevice, nowSec, info.hostname);
                yiaddr = ours;
                replyType = DHCP_ACK;
            } else {
                replyType = DHCP_NAK;
                withLease = false;
            }
            break;
        }
        case DHCP_DECLINE:
        case DHCP_RELEASE: {
            if (info.serverId && info.serverId != cfg_.serverIp) return 0;
            DhcpLease* l = findByMac(info.mac, fromDevice);
            const uint32_t ip = info.type == DHCP_RELEASE ? info.ciaddr : info.requestedIp;
            if (l && l->ip == ip) l->used = false;
            return 0;
        }
        case DHCP_INFORM: {
            replyType = DHCP_ACK;
            withLease = false;
            break;
        }
        default:
            return 0;
    }

    memset(out, 0, outCap);
    out[OFF_OP] = 2;
    out[OFF_HTYPE] = 1;
    out[OFF_HLEN] = 6;
    memcpy(out + OFF_XID, in + OFF_XID, 4);
    memcpy(out + OFF_FLAGS, in + OFF_FLAGS, 2);
    if (info.type == DHCP_INFORM) memcpy(out + OFF_CIADDR, in + OFF_CIADDR, 4);
    wr32(out + OFF_YIADDR, yiaddr);
    memcpy(out + OFF_GIADDR, in + OFF_GIADDR, 4);
    memcpy(out + OFF_CHADDR, in + OFF_CHADDR, 16);
    memcpy(out + OFF_MAGIC, MAGIC, 4);

    OptWriter w{out + OFF_OPTIONS, outCap - OFF_OPTIONS - 1, 0, true};
    w.put8(OPT_MSG_TYPE, replyType);
    w.put32(OPT_SERVER_ID, cfg_.serverIp);
    if (replyType != DHCP_NAK) {
        if (withLease) w.put32(OPT_LEASE_TIME, cfg_.leaseSecs);
        w.put32(OPT_SUBNET, cfg_.netmask);
        // Only Wi-Fi clients get a gateway. The USB device (a camera, or a
        // computer used for setup) must never route its internet here.
        if (cfg_.router && !fromDevice) w.put32(OPT_ROUTER, cfg_.router);
    }
    if (!w.ok) return 0;
    out[OFF_OPTIONS + w.len] = OPT_END;
    const size_t len = OFF_OPTIONS + w.len + 1;
    return len < DHCP_MIN_PACKET ? DHCP_MIN_PACKET : len;
}

size_t DhcpServer::activeLeases(uint32_t nowSec, DhcpLease* out, size_t cap) const {
    size_t n = 0;
    for (const DhcpLease& l : leases_) {
        if (l.used && !expired(l, nowSec) && n < cap) out[n++] = l;
    }
    return n;
}

bool DhcpServer::deviceLease(uint32_t nowSec, DhcpLease& out) const {
    for (const DhcpLease& l : leases_) {
        if (l.used && l.device && !expired(l, nowSec)) {
            out = l;
            return true;
        }
    }
    return false;
}
