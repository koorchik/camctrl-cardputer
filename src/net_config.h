#pragma once

#include "logic/dhcp_server.h"

// The adapter's network: one /24 shared by the USB device and Wi-Fi clients.
constexpr uint32_t ADAPTER_IP = ipv4(192, 168, 73, 1);
constexpr uint32_t NETMASK = ipv4(255, 255, 255, 0);
constexpr uint32_t DEVICE_IP = ipv4(192, 168, 73, 2);
constexpr uint32_t POOL_START = ipv4(192, 168, 73, 10);
constexpr uint32_t POOL_END = ipv4(192, 168, 73, 50);
constexpr uint32_t LEASE_SECS = 3600;
constexpr int AP_MAX_CLIENTS = 4;

// "a.b.c.d" for a host-order address.
void ipToStr(uint32_t ip, char out[16]);
