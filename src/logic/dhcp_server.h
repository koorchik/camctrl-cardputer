#pragma once

#include <cstddef>
#include <cstdint>

// A tiny DHCP server (pure logic). The device (the client on the USB port)
// always gets `deviceIp`; Wi-Fi clients get addresses from the pool. Replies
// carry no router and no DNS option, so phones keep mobile data for the
// internet and use this network only for its own subnet.
//
// IPv4 addresses are host-order uint32_t (192.168.73.1 == 0xC0A84901).

constexpr uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
}

constexpr size_t DHCP_MAX_LEASES = 16;
constexpr size_t DHCP_MIN_PACKET = 300;
constexpr size_t DHCP_REPLY_CAP = 548;
constexpr size_t DHCP_HOSTNAME_MAX = 32;

enum DhcpMsgType : uint8_t {
    DHCP_DISCOVER = 1,
    DHCP_OFFER = 2,
    DHCP_REQUEST = 3,
    DHCP_DECLINE = 4,
    DHCP_ACK = 5,
    DHCP_NAK = 6,
    DHCP_RELEASE = 7,
    DHCP_INFORM = 8,
};

struct DhcpConfig {
    uint32_t serverIp;
    uint32_t netmask;
    uint32_t deviceIp;
    uint32_t poolStart;  // inclusive
    uint32_t poolEnd;    // inclusive
    uint32_t leaseSecs;
    uint32_t router;     // option 3 for Wi-Fi clients only, 0 = not sent
};

struct DhcpLease {
    bool used;
    bool device;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t expiresSec;
    char hostname[DHCP_HOSTNAME_MAX + 1];
};

class DhcpServer {
public:
    explicit DhcpServer(const DhcpConfig& cfg);

    // Handles one client message. `fromDevice` says the client MAC sits on the
    // USB port. Writes the reply into `out` and returns its length, or 0 when
    // nothing should be sent.
    size_t handle(const uint8_t* in, size_t inLen, bool fromDevice, uint32_t nowSec,
                  uint8_t* out, size_t outCap);

    // Active (bound, unexpired) leases.
    size_t activeLeases(uint32_t nowSec, DhcpLease* out, size_t cap) const;
    // The USB device's bound lease, if any.
    bool deviceLease(uint32_t nowSec, DhcpLease& out) const;
    void clear();

    const DhcpConfig& config() const { return cfg_; }

private:
    DhcpLease* findByMac(const uint8_t mac[6], bool device);
    bool ipTaken(uint32_t ip, const uint8_t mac[6], uint32_t nowSec) const;
    uint32_t pickIp(const uint8_t mac[6], bool device, uint32_t requested, uint32_t nowSec);
    DhcpLease* bind(const uint8_t mac[6], uint32_t ip, bool device, uint32_t nowSec,
                    const char* hostname);

    DhcpConfig cfg_;
    DhcpLease leases_[DHCP_MAX_LEASES];
};

// Parsed fields of a client message, exposed for tests and logging.
struct DhcpRequestInfo {
    uint8_t type;
    uint32_t xid;
    uint8_t mac[6];
    uint32_t ciaddr;
    uint32_t requestedIp;  // option 50, 0 if absent
    uint32_t serverId;     // option 54, 0 if absent
    char hostname[DHCP_HOSTNAME_MAX + 1];
};

bool dhcpParse(const uint8_t* in, size_t len, DhcpRequestInfo& info);

// Reads option `code` from a DHCP packet's options; returns its length or -1.
int dhcpFindOption(const uint8_t* pkt, size_t len, uint8_t code, const uint8_t** value);
