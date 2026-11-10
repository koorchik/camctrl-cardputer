#pragma once

#include <cstddef>
#include <cstdint>

// Layer-2 forwarding between the bridge ports. Pure logic: no ESP-IDF.
//
// Ports: the USB network interface (the device), the Wi-Fi access point (all
// stations share one port) and the adapter's own IP stack.
enum Port : uint8_t { PORT_USB = 0, PORT_WIFI = 1, PORT_LOCAL = 2, PORT_COUNT = 3 };

using PortMask = uint8_t;
constexpr PortMask portBit(Port p) { return (PortMask)(1u << p); }
constexpr PortMask ALL_PORTS = (1u << PORT_COUNT) - 1;

constexpr size_t ETH_HEADER_LEN = 14;
constexpr size_t FORWARDER_TABLE_SIZE = 32;
constexpr uint32_t FORWARDER_AGE_MS = 300000;

class Forwarder {
public:
    explicit Forwarder(const uint8_t localMac[6]);

    // Learns the frame's source on `in` and returns the ports the frame must
    // go out of. Never includes `in`. Group frames go everywhere else, frames
    // for the local MAC only to PORT_LOCAL, known unicast to its port and
    // unknown unicast to every other non-local port. Runts return 0.
    PortMask route(Port in, const uint8_t* frame, size_t len, uint32_t nowMs);

    // Where `mac` was last seen, if not aged out.
    bool lookup(const uint8_t mac[6], Port& port, uint32_t nowMs) const;

    // Drops every entry learned on `p` (e.g. the USB cable came out).
    void forgetPort(Port p);

private:
    struct Entry {
        uint8_t mac[6];
        Port port;
        bool used;
        uint32_t seenMs;
    };
    void learn(const uint8_t mac[6], Port p, uint32_t nowMs);
    const Entry* find(const uint8_t mac[6], uint32_t nowMs) const;

    uint8_t localMac_[6];
    Entry table_[FORWARDER_TABLE_SIZE];
};

bool macEqual(const uint8_t a[6], const uint8_t b[6]);
inline bool macIsGroup(const uint8_t mac[6]) { return (mac[0] & 0x01) != 0; }
