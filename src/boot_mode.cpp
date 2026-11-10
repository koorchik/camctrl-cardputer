#include "boot_mode.h"

#include <cstring>

#include "esp_private/periph_ctrl.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/usb_wrap_ll.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logic/boot_guard.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/io_mux_reg.h"
#include "soc/usb_serial_jtag_reg.h"

namespace {
const char* TAG = "boot";
RTC_NOINIT_ATTR BootGuardState s_guard;
bool s_safe = false;

ResetKind resetKind() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return ResetKind::PowerOn;
        case ESP_RST_BROWNOUT:  // e.g. Wi-Fi start on weak USB power: treat as a crash
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return ResetKind::Crash;
        case ESP_RST_SW:
            return ResetKind::Software;
        default:
            return ResetKind::Other;
    }
}
}  // namespace

// Stops the USB-OTG controller. A software reset only resets the CPU, so
// without this the OTG block keeps its clock and pads across esp_restart().
static void stopUsbOtg() {
    usb_wrap_ll_phy_enable_pad(&USB_WRAP, false);
    PERIPH_RCC_ATOMIC() {
        usb_wrap_ll_reset_register();
        usb_wrap_ll_enable_bus_clock(false);
    }
}

void bootModeReconnectSerialJtag() {
    // Route the internal PHY back to the serial/JTAG controller (the power-on
    // default). These RTC bits survive software resets, so after TinyUSB (or a
    // crash while it ran) the PHY would otherwise stay with USB-OTG.
    usb_wrap_ll_phy_enable_pad(&USB_WRAP, false);
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                        RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    // GPIO pull-ups on D-/D+ add to the USB pull-up and confuse the host
    // (gpio_reset_pin() turns them on). Make sure they are off.
    REG_CLR_BIT(IO_MUX_GPIO19_REG, FUN_PU);
    REG_CLR_BIT(IO_MUX_GPIO20_REG, FUN_PU);
    // Pads on, but every pull resistor forced off for a moment: the host sees
    // a disconnect, then a fresh device when the hardware pull-up returns.
    // Without usb_pad_enable the port stays dead (the Arduino core sets it too).
    usb_serial_jtag_ll_phy_enable_pad(true);
    const usb_serial_jtag_pull_override_vals_t off = {false, false, false, false};
    usb_serial_jtag_ll_phy_enable_pull_override(&off);
    vTaskDelay(pdMS_TO_TICKS(100));
    // Serial/JTAG registers survive esp_restart(), so the D+ pull-up value
    // must be put back (dp_pu = 1) before giving pull control back to the
    // hardware, or the host never sees the device again.
    const usb_serial_jtag_pull_override_vals_t normal = {true, false, false, false};
    usb_serial_jtag_ll_phy_enable_pull_override(&normal);
    usb_serial_jtag_ll_phy_disable_pull_override();
}

bool bootModeIsSafe() {
    uint32_t firmwareId = 0;
    if (const esp_app_desc_t* d = esp_app_get_description()) memcpy(&firmwareId, d->app_elf_sha256, sizeof(firmwareId));
    s_safe = bootGuardOnBoot(s_guard, resetKind(), firmwareId);
    ESP_LOGI(TAG, "reset reason %d, crashes %lu -> %s", (int)esp_reset_reason(),
             (unsigned long)s_guard.crashCount, s_safe ? "SAFE MODE" : "normal");
    if (s_safe) {
        // After a crash the OTG block may still be clocked and own the PHY.
        bootModeReconnectSerialJtag();
        stopUsbOtg();
    }
    return s_safe;
}

void bootModeMarkStable() {
    bootGuardMarkStable(s_guard);
}

void bootModeRestartSafe() {
    ESP_LOGW(TAG, "restarting into safe mode");
    bootGuardRequestSafe(s_guard);
    bootModeReconnectSerialJtag();
    stopUsbOtg();
    esp_restart();
}

void bootModeRestartNormal() {
    ESP_LOGW(TAG, "restarting");
    bootModeReconnectSerialJtag();
    stopUsbOtg();
    esp_restart();
}

void bootModeRestartDownload() {
    ESP_LOGW(TAG, "restarting into the ROM download mode");
    bootModeReconnectSerialJtag();
    stopUsbOtg();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
