#pragma once

#include <cstddef>
#include <cstdint>

// Reads the sender IPv4 address (host order) of an Ethernet frame carrying
// IPv4 or ARP. Returns false for other frames and for 0.0.0.0.
bool frameIpv4Sender(const uint8_t* frame, size_t len, uint32_t* ip);

// True when `ip` is inside `net`/`mask` and is not the network or broadcast address.
bool ipv4InSubnet(uint32_t ip, uint32_t net, uint32_t mask);
