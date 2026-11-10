#include "hw/keyboard.h"

#include <atomic>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
const char* TAG = "keyboard";

// Cardputer ADV internal I2C bus and TCA8418 (see M5Cardputer/M5Unified).
constexpr gpio_num_t PIN_SDA = GPIO_NUM_8;
constexpr gpio_num_t PIN_SCL = GPIO_NUM_9;
constexpr gpio_num_t PIN_INT = GPIO_NUM_11;
constexpr uint16_t TCA_ADDR = 0x34;

constexpr uint8_t REG_CFG = 0x01;
constexpr uint8_t REG_INT_STAT = 0x02;
constexpr uint8_t REG_KEY_LCK_EC = 0x03;
constexpr uint8_t REG_KEY_EVENT_A = 0x04;
constexpr uint8_t REG_GPIO_INT_STAT_1 = 0x11;
constexpr uint8_t REG_GPIO_INT_EN_1 = 0x1A;
constexpr uint8_t REG_KP_GPIO_1 = 0x1D;
constexpr uint8_t REG_KP_GPIO_2 = 0x1E;
constexpr uint8_t REG_GPI_EM_1 = 0x20;
constexpr uint8_t REG_GPIO_DIR_1 = 0x23;
constexpr uint8_t REG_GPIO_INT_LVL_1 = 0x26;
constexpr uint8_t CFG_KE_IEN = 0x01;
constexpr uint8_t CFG_GPI_IEN = 0x02;

i2c_master_dev_handle_t s_dev = nullptr;
QueueHandle_t s_queue = nullptr;
SemaphoreHandle_t s_irq = nullptr;
std::atomic<int> s_shiftCount{0};

bool writeReg(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50) == ESP_OK;
}

bool readReg(uint8_t reg, uint8_t& val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, &val, 1, 50) == ESP_OK;
}

void IRAM_ATTR onInt(void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_irq, &woken);
    if (woken) portYIELD_FROM_ISR();
}

bool configure() {
    // Same setup as M5Cardputer's TCA8418 reader: all pins inputs with
    // events, then a 7 x 8 keypad matrix, flush, enable key interrupts.
    for (uint8_t i = 0; i < 3; ++i) {
        if (!writeReg(REG_GPIO_DIR_1 + i, 0x00)) return false;
        writeReg(REG_GPI_EM_1 + i, 0xFF);
        writeReg(REG_GPIO_INT_LVL_1 + i, 0x00);
        writeReg(REG_GPIO_INT_EN_1 + i, 0xFF);
    }
    writeReg(REG_KP_GPIO_1, 0x7F);  // 7 rows
    writeReg(REG_KP_GPIO_2, 0xFF);  // 8 columns
    uint8_t ev = 0;
    for (int i = 0; i < 20 && readReg(REG_KEY_EVENT_A, ev) && ev; ++i) {
    }
    for (uint8_t i = 0; i < 3; ++i) readReg(REG_GPIO_INT_STAT_1 + i, ev);
    writeReg(REG_INT_STAT, 0x03);
    uint8_t cfg = 0;
    readReg(REG_CFG, cfg);
    return writeReg(REG_CFG, cfg | CFG_KE_IEN | CFG_GPI_IEN);
}

void drain() {
    uint8_t raw = 0;
    // The FIFO holds up to 10 events; read until empty.
    for (int i = 0; i < 16; ++i) {
        if (!readReg(REG_KEY_EVENT_A, raw) || raw == 0) break;
        KeyEvent e;
        if (!tcaDecode(raw, e)) continue;
        if (e.key.code == KeyCode::Shift) {
            s_shiftCount += e.pressed ? 1 : -1;
            if (s_shiftCount < 0) s_shiftCount = 0;
        }
        xQueueSend(s_queue, &e, 0);
    }
    writeReg(REG_INT_STAT, 0x03);
}

void task(void*) {
    while (true) {
        // Interrupt-driven, with a slow poll as a safety net.
        xSemaphoreTake(s_irq, pdMS_TO_TICKS(100));
        drain();
    }
}
}  // namespace

QueueHandle_t keyboardStart() {
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = PIN_SDA;
    bus.scl_io_num = PIN_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t busHandle = nullptr;
    if (i2c_new_master_bus(&bus, &busHandle) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        return nullptr;
    }
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = TCA_ADDR;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(busHandle, &dev, &s_dev) != ESP_OK || !configure()) {
        ESP_LOGE(TAG, "TCA8418 not found");
        return nullptr;
    }
    s_queue = xQueueCreate(32, sizeof(KeyEvent));
    s_irq = xSemaphoreCreateBinary();
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_INT;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_INT, onInt, nullptr);
    xTaskCreate(task, "keyboard", 3072, nullptr, 6, nullptr);
    ESP_LOGI(TAG, "TCA8418 keyboard ready");
    return s_queue;
}

bool keyboardShiftHeld() {
    return s_shiftCount > 0;
}
