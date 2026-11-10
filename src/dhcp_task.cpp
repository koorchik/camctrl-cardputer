#include "dhcp_task.h"

#include <cstring>

#include "bridge.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "net_config.h"

namespace {
const char* TAG = "dhcp";
DhcpServer* s_server = nullptr;
SemaphoreHandle_t s_lock = nullptr;

uint32_t nowSec() {
    return (uint32_t)(esp_timer_get_time() / 1000000) + 1;
}

const char* typeName(uint8_t t) {
    switch (t) {
        case DHCP_DISCOVER: return "DISCOVER";
        case DHCP_OFFER: return "OFFER";
        case DHCP_REQUEST: return "REQUEST";
        case DHCP_DECLINE: return "DECLINE";
        case DHCP_ACK: return "ACK";
        case DHCP_NAK: return "NAK";
        case DHCP_RELEASE: return "RELEASE";
        case DHCP_INFORM: return "INFORM";
        default: return "?";
    }
}

const char* replyName(const uint8_t* pkt, size_t len) {
    const uint8_t* v = nullptr;
    return dhcpFindOption(pkt, len, 53, &v) == 1 ? typeName(v[0]) : "?";
}

void task(void*) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed");
        vTaskDelete(nullptr);
        return;
    }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(67);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind failed");
        close(sock);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "server listening");

    static uint8_t in[1500];
    static uint8_t out[DHCP_REPLY_CAP];
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(68);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    while (true) {
        const int n = recv(sock, in, sizeof(in), 0);
        if (n <= 0) continue;
        DhcpRequestInfo info;
        if (!dhcpParse(in, (size_t)n, info)) continue;
        const bool fromDevice = bridgeMacOnUsb(info.mac);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const size_t len = s_server->handle(in, (size_t)n, fromDevice, nowSec(), out, sizeof(out));
        xSemaphoreGive(s_lock);
        char ip[16];
        ipToStr(len ? ((uint32_t)out[16] << 24 | (uint32_t)out[17] << 16 |
                       (uint32_t)out[18] << 8 | out[19])
                    : 0,
                ip);
        const uint8_t* m = info.mac;
        ESP_LOGI(TAG, "%s from %02x:%02x:%02x:%02x:%02x:%02x (%s%s%s) -> %s %s",
                 typeName(info.type), m[0], m[1], m[2], m[3], m[4], m[5],
                 fromDevice ? "usb" : "wifi", info.hostname[0] ? ", " : "", info.hostname,
                 len ? replyName(out, len) : "no reply", len ? ip : "");
        if (len) sendto(sock, out, len, 0, (sockaddr*)&to, sizeof(to));
    }
}
}  // namespace

void dhcpTaskStart() {
    // No gateway and no DNS: tested on Android 17 (Galaxy S24 Ultra) that a
    // gateway is not needed to reach the adapter, and without one iOS keeps
    // mobile data for the internet. (The DhcpServer can send option 3 to
    // Wi-Fi clients if ever needed; the USB device never gets it.)
    static const DhcpConfig cfg = {ADAPTER_IP, NETMASK, DEVICE_IP, POOL_START, POOL_END, LEASE_SECS,
                                   0};
    s_server = new DhcpServer(cfg);
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(task, "dhcp", 4096, nullptr, 5, nullptr);
}

bool dhcpDeviceLease(DhcpLease& out) {
    if (!s_server) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_server->deviceLease(nowSec(), out);
    xSemaphoreGive(s_lock);
    return ok;
}

size_t dhcpLeases(DhcpLease* out, size_t cap) {
    if (!s_server) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t n = s_server->activeLeases(nowSec(), out, cap);
    xSemaphoreGive(s_lock);
    return n;
}
