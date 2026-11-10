#include "bridge.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_private/wifi.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "logic/forwarder.h"
#include "logic/frame_info.h"
#include "net_config.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"
#include "tusb.h"

namespace {
const char* TAG = "bridge";

Forwarder* s_fwd = nullptr;
portMUX_TYPE s_fwdLock = portMUX_INITIALIZER_UNLOCKED;
esp_netif_t* s_netif = nullptr;
std::atomic<bool> s_apUp{false};
std::atomic<bool> s_usbUp{false};
std::atomic<int> s_clients{0};
std::atomic<uint32_t> s_deviceSeenIp{0};
std::atomic<uint32_t> s_usbToWifi{0}, s_wifiToUsb{0}, s_toLocal{0}, s_fromLocal{0}, s_drops{0};

uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

PortMask route(Port in, const uint8_t* frame, size_t len) {
    portENTER_CRITICAL(&s_fwdLock);
    const PortMask m = s_fwd->route(in, frame, len, nowMs());
    portEXIT_CRITICAL(&s_fwdLock);
    return m;
}

// Frames for USB go through our own queue: tinyusb_net_send_async() may block
// on TinyUSB's event queue, which must never stall the Wi-Fi task or lwIP.
struct UsbFrame {
    void* buf;
    uint16_t len;
};
constexpr int USB_TX_QUEUE_LEN = 16;
QueueHandle_t s_usbTxQueue = nullptr;

// Without VBUS sensing a pulled cable looks like a bus suspend, so "mounted
// and not suspended" is the only reliable sign of a live link.
bool usbLinkUp() {
    return tud_mounted() && !tud_suspended();
}

void sendUsb(const void* frame, size_t len) {
    if (!usbLinkUp()) return;
    void* buf = malloc(len);
    if (!buf) {
        ++s_drops;
        return;
    }
    memcpy(buf, frame, len);
    const UsbFrame f{buf, (uint16_t)len};
    if (xQueueSend(s_usbTxQueue, &f, 0) != pdTRUE) {
        free(buf);
        ++s_drops;
    }
}

void usbTxTask(void*) {
    UsbFrame f;
    while (true) {
        if (xQueueReceive(s_usbTxQueue, &f, portMAX_DELAY) != pdTRUE) continue;
        // On success and on TinyUSB's own drop path usbFreeTx() frees the buffer.
        if (!tud_mounted() || tinyusb_net_send_async(f.buf, f.len, f.buf) != ESP_OK) {
            free(f.buf);
            ++s_drops;
        }
    }
}

void sendWifi(const void* frame, size_t len) {
    if (!s_apUp || s_clients <= 0) return;
    if (esp_wifi_internal_tx(WIFI_IF_AP, const_cast<void*>(frame), (uint16_t)len) != ESP_OK) {
        ++s_drops;
    }
}

void deliverLocal(const void* frame, size_t len) {
    void* buf = malloc(len);
    if (!buf) {
        ++s_drops;
        return;
    }
    memcpy(buf, frame, len);
    ++s_toLocal;
    esp_netif_receive(s_netif, buf, len, buf);  // takes ownership
}

// --- the adapter's own interface -----------------------------------------
esp_err_t localTransmit(void* /*h*/, void* buffer, size_t len) {
    ++s_fromLocal;
    const PortMask m = route(PORT_LOCAL, (const uint8_t*)buffer, len);
    if (m & portBit(PORT_USB)) sendUsb(buffer, len);
    if (m & portBit(PORT_WIFI)) sendWifi(buffer, len);
    return ESP_OK;
}

void localFreeRx(void* /*h*/, void* buffer) {
    free(buffer);
}

// --- USB (device) ---------------------------------------------------------
esp_err_t usbRx(void* buffer, uint16_t len, void* /*ctx*/) {
    const uint8_t* f = (const uint8_t*)buffer;
    uint32_t ip = 0;
    if (frameIpv4Sender(f, len, &ip)) s_deviceSeenIp = ip;
    const PortMask m = route(PORT_USB, f, len);
    if (m & portBit(PORT_WIFI)) {
        ++s_usbToWifi;
        sendWifi(f, len);
    }
    if (m & portBit(PORT_LOCAL)) deliverLocal(f, len);
    return ESP_OK;
}

void usbFreeTx(void* buffer, void* /*ctx*/) {
    free(buffer);
}

void usbEvent(tinyusb_event_t* event, void* /*arg*/) {
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            s_usbUp = true;
            ESP_LOGI(TAG, "USB host attached");
            break;
        case TINYUSB_EVENT_SUSPENDED:
            ESP_LOGI(TAG, "USB bus suspended (cable pulled?)");
            break;
        case TINYUSB_EVENT_RESUMED:
            ESP_LOGI(TAG, "USB bus resumed");
            break;
        case TINYUSB_EVENT_DETACHED:
            s_usbUp = false;
            s_deviceSeenIp = 0;
            portENTER_CRITICAL(&s_fwdLock);
            s_fwd->forgetPort(PORT_USB);
            portEXIT_CRITICAL(&s_fwdLock);
            ESP_LOGI(TAG, "USB host detached");
            break;
        default:
            break;
    }
}

// --- Wi-Fi AP (phones) ----------------------------------------------------
esp_err_t wifiRx(void* buffer, uint16_t len, void* eb) {
    const uint8_t* f = (const uint8_t*)buffer;
    const PortMask m = route(PORT_WIFI, f, len);
    if (m & portBit(PORT_USB)) {
        ++s_wifiToUsb;
        sendUsb(f, len);
    }
    if (m & portBit(PORT_LOCAL)) deliverLocal(f, len);
    esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

void updateClientCount() {
    wifi_sta_list_t list;
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) s_clients = list.num;
}

void wifiEvent(void*, esp_event_base_t, int32_t id, void* data) {
    switch (id) {
        case WIFI_EVENT_AP_START:
            esp_wifi_internal_reg_rxcb(WIFI_IF_AP, wifiRx);
            s_apUp = true;
            ESP_LOGI(TAG, "Wi-Fi AP started");
            break;
        case WIFI_EVENT_AP_STOP:
            s_apUp = false;
            esp_wifi_internal_reg_rxcb(WIFI_IF_AP, nullptr);
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            auto* e = (wifi_event_ap_staconnected_t*)data;
            ESP_LOGI(TAG, "station " MACSTR " joined", MAC2STR(e->mac));
            updateClientCount();
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            auto* e = (wifi_event_ap_stadisconnected_t*)data;
            ESP_LOGI(TAG, "station " MACSTR " left", MAC2STR(e->mac));
            updateClientCount();
            break;
        }
        default:
            break;
    }
}

esp_err_t startLocalNetif(const uint8_t mac[6]) {
    esp_netif_ip_info_t ip = {};
    ip.ip.addr = esp_netif_htonl(ADAPTER_IP);
    ip.netmask.addr = esp_netif_htonl(NETMASK);
    ip.gw.addr = 0;

    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.flags = ESP_NETIF_FLAG_AUTOUP;
    base.ip_info = &ip;
    base.if_key = "BRIDGE";
    base.if_desc = "bridge";
    base.route_prio = 100;
    esp_netif_config_t cfg = {};
    cfg.base = &base;
    cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    s_netif = esp_netif_new(&cfg);
    if (!s_netif) return ESP_FAIL;

    esp_netif_driver_ifconfig_t drv = {};
    drv.handle = (void*)1;
    drv.transmit = localTransmit;
    drv.driver_free_rx_buffer = localFreeRx;
    ESP_ERROR_CHECK(esp_netif_set_driver_config(s_netif, &drv));
    ESP_ERROR_CHECK(esp_netif_set_mac(s_netif, const_cast<uint8_t*>(mac)));
    esp_netif_action_start(s_netif, nullptr, 0, nullptr);
    esp_netif_set_ip_info(s_netif, &ip);
    esp_netif_action_connected(s_netif, nullptr, 0, nullptr);
    esp_netif_set_default_netif(s_netif);
    return ESP_OK;
}

esp_err_t startWifiAp(const Settings& s) {
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t ap = {};
    const size_t ssidLen = strlen(s.ssid);
    memcpy(ap.ap.ssid, s.ssid, ssidLen);
    ap.ap.ssid_len = (uint8_t)ssidLen;
    snprintf((char*)ap.ap.password, sizeof(ap.ap.password), "%s", s.password);
    ap.ap.channel = s.channel;
    ap.ap.max_connection = AP_MAX_CLIENTS;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return esp_wifi_start();
}

// Hot-plug fix: when the board is already running (on battery) and the cable
// is plugged in later, some hosts (the Blackmagic camera) never enumerate us,
// while a cold start powered by the cable works. And a pulled cable only shows
// up as a bus suspend. So whenever the link has been down (not configured, or
// suspended) for a few seconds: treat the device as gone and briefly drop and
// restore the D+ pull-up, like re-plugging the cable. Idle while the link is up.
constexpr uint32_t USB_DOWN_MS = 3000;
constexpr uint32_t USB_REATTACH_UNMOUNTED_MS = 5000;
constexpr uint32_t USB_REATTACH_SUSPENDED_MS = 10000;

void usbWatchTask(void*) {
    uint32_t since = nowMs();
    uint32_t attempts = 0;
    bool forgotten = false;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (usbLinkUp()) {
            since = nowMs();
            attempts = 0;
            forgotten = false;
            continue;
        }
        const uint32_t down = nowMs() - since;
        if (!forgotten && down >= USB_DOWN_MS) {
            forgotten = true;
            s_usbUp = false;
            s_deviceSeenIp = 0;
            portENTER_CRITICAL(&s_fwdLock);
            s_fwd->forgetPort(PORT_USB);
            portEXIT_CRITICAL(&s_fwdLock);
        }
        if (down < (tud_mounted() ? USB_REATTACH_SUSPENDED_MS : USB_REATTACH_UNMOUNTED_MS)) continue;
        if (attempts < 3 || attempts % 60 == 0) {
            ESP_LOGI(TAG, "USB link down for %lus, re-attaching", (unsigned long)(down / 1000));
        }
        ++attempts;
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        tud_connect();
        since = nowMs();
    }
}

// Same as esp_tinyusb's default NCM-only configuration (interfaces 0/1,
// endpoints 1/2, MAC string index 5 from its default string table) but
// WITHOUT remote wakeup. With remote wakeup advertised, the camera's Linux
// autosuspends the idle adapter and frames for it are lost while suspended;
// without it, usbnet keeps an open interface awake.
enum { ITF_NET = 0, ITF_NET_DATA, ITF_TOTAL };
constexpr uint8_t EP_NET_NOTIF = 1;
constexpr uint8_t EP_NET_DATA = 2;
constexpr uint8_t STRID_NET_IF = 4;
constexpr uint8_t STRID_NET_MAC = 5;
constexpr uint16_t CONFIG_TOTAL_LEN = TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN;
const uint8_t kFsConfig[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_NCM_DESCRIPTOR(ITF_NET, STRID_NET_IF, STRID_NET_MAC, (0x80 | EP_NET_NOTIF), 64,
                           EP_NET_DATA, (0x80 | EP_NET_DATA), 64, CFG_TUD_NET_MTU),
};

esp_err_t startUsb() {
    esp_log_level_set("tusb_net", ESP_LOG_ERROR);  // per-drop warnings would stall USB
    s_usbTxQueue = xQueueCreate(USB_TX_QUEUE_LEN, sizeof(UsbFrame));
    xTaskCreate(usbTxTask, "usb_tx", 3072, nullptr, 6, nullptr);
    tinyusb_config_t tcfg = TINYUSB_DEFAULT_CONFIG(usbEvent, nullptr);
    tcfg.descriptor.full_speed_config = kFsConfig;
    ESP_ERROR_CHECK(tinyusb_driver_install(&tcfg));
    tinyusb_net_config_t ncfg = {};
    esp_read_mac(ncfg.mac_addr, ESP_MAC_ETH);
    ncfg.on_recv_callback = usbRx;
    ncfg.free_tx_buffer = usbFreeTx;
    const esp_err_t e = tinyusb_net_init(&ncfg);
    xTaskCreate(usbWatchTask, "usb_watch", 2560, nullptr, 3, nullptr);
    return e;
}
}  // namespace

void ipToStr(uint32_t ip, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 0xFF),
             (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
}

esp_err_t bridgeStart(const Settings& settings) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    s_fwd = new Forwarder(mac);
    ESP_ERROR_CHECK(startLocalNetif(mac));
    ESP_ERROR_CHECK(startWifiAp(settings));
    ESP_ERROR_CHECK(startUsb());
    ESP_LOGI(TAG, "bridge up: %s, local " MACSTR, settings.ssid, MAC2STR(mac));
    return ESP_OK;
}

void bridgeStatus(BridgeStatus& out) {
    out.usbMounted = usbLinkUp();
    out.apStarted = s_apUp;
    out.wifiClients = s_clients;
    out.deviceSeenIp = s_deviceSeenIp;
    out.usbToWifi = s_usbToWifi;
    out.wifiToUsb = s_wifiToUsb;
    out.toLocal = s_toLocal;
    out.fromLocal = s_fromLocal;
    out.drops = s_drops;
}

bool bridgeMacOnUsb(const uint8_t mac[6]) {
    if (!s_fwd) return false;
    Port p = PORT_COUNT;
    portENTER_CRITICAL(&s_fwdLock);
    const bool found = s_fwd->lookup(mac, p, nowMs());
    portEXIT_CRITICAL(&s_fwdLock);
    return found && p == PORT_USB;
}
