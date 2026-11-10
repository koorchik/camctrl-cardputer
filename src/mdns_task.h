#pragma once

#include <cstdint>

// Fallback mDNS responder: answers "<deviceHostname>.local" with the USB
// device's IP only while the device does not answer for itself. Off when the
// hostname is empty.
enum class MdnsState { Off, NoDevice, DeviceAnswers, AdapterAnswers };

void mdnsTaskStart(const char* deviceHostname);
MdnsState mdnsState();

// The USB device address the adapter knows: DHCP lease, else the address seen on
// USB if it is inside the adapter's subnet. 0 if unknown.
uint32_t deviceIpForClients();
