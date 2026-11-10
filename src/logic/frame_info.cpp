#include "logic/frame_info.h"

namespace {
uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
}  // namespace

bool frameIpv4Sender(const uint8_t* frame, size_t len, uint32_t* ip) {
    if (!frame || len < 14) return false;
    const uint16_t type = (uint16_t)((frame[12] << 8) | frame[13]);
    uint32_t v = 0;
    if (type == 0x0800) {
        if (len < 14 + 20 || (frame[14] >> 4) != 4) return false;
        v = rd32(frame + 14 + 12);
    } else if (type == 0x0806) {
        if (len < 14 + 28) return false;
        v = rd32(frame + 14 + 14);
    } else {
        return false;
    }
    if (v == 0) return false;
    *ip = v;
    return true;
}

bool ipv4InSubnet(uint32_t ip, uint32_t net, uint32_t mask) {
    if ((ip & mask) != (net & mask)) return false;
    const uint32_t host = ip & ~mask;
    return host != 0 && host != (~mask);
}
