#include "display.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if BOARD_CARDPUTER
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "fonts/glcdfont.h"

namespace {
const char* TAG = "display";
// Cardputer ADV: ST7789V2 135x240 on SPI3 (pins as in M5GFX's autodetect).
constexpr gpio_num_t PIN_MOSI = GPIO_NUM_35;
constexpr gpio_num_t PIN_SCLK = GPIO_NUM_36;
constexpr gpio_num_t PIN_CS = GPIO_NUM_37;
constexpr gpio_num_t PIN_DC = GPIO_NUM_34;
constexpr gpio_num_t PIN_RST = GPIO_NUM_33;
// Backlight; also powers the RGB LED. Held high = full brightness.
constexpr gpio_num_t PIN_BL = GPIO_NUM_38;
constexpr int W = 240;
constexpr int H = 135;
constexpr int LINE_H = 19;
constexpr int BAND_ROWS = 27;  // 135 = 5 bands

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb = nullptr;
DisplayModel s_shown;
bool s_hasShown = false;

inline uint16_t be(uint16_t c) {
    return (uint16_t)((c >> 8) | (c << 8));
}

void drawChar(int x, int y, char ch, uint16_t color, int scale) {
    const unsigned char c = (unsigned char)ch;
    for (int col = 0; col < 5; ++col) {
        const uint8_t bits = glcdfont[c * 5 + col];
        for (int row = 0; row < 8; ++row) {
            if (!(bits & (1 << row))) continue;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = x + col * scale + dx;
                    const int py = y + row * scale + dy;
                    if (px >= 0 && px < W && py >= 0 && py < H) s_fb[py * W + px] = be(color);
                }
            }
        }
    }
}

void render(const DisplayModel& m) {
    memset(s_fb, 0, W * H * sizeof(uint16_t));
    for (int i = 0; i < DISPLAY_LINES; ++i) {
        const DisplayLine& l = m.lines[i];
        const size_t len = strlen(l.text);
        const int scale = len * 12 <= (size_t)W ? 2 : 1;
        const int y = i * LINE_H + (scale == 2 ? 1 : 5);
        for (size_t k = 0; k < len; ++k) drawChar((int)k * 6 * scale, y, l.text[k], l.color, scale);
    }
    for (int y = 0; y < H; y += BAND_ROWS) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, W, y + BAND_ROWS, s_fb + y * W);
    }
}
}  // namespace

void displayInit() {
    gpio_reset_pin(PIN_BL);
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 1);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = PIN_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = W * BAND_ROWS * 2 + 16;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi init failed");
        return;
    }
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t ioc = {};
    ioc.cs_gpio_num = PIN_CS;
    ioc.dc_gpio_num = PIN_DC;
    ioc.spi_mode = 0;
    ioc.pclk_hz = 40 * 1000 * 1000;
    ioc.trans_queue_depth = 8;
    ioc.lcd_cmd_bits = 8;
    ioc.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &ioc, &io) != ESP_OK) return;
    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = PIN_RST;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    pc.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7789(io, &pc, &s_panel) != ESP_OK) return;
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);
    esp_lcd_panel_set_gap(s_panel, 40, 53);
    esp_lcd_panel_disp_on_off(s_panel, true);
    s_fb = (uint16_t*)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_fb) ESP_LOGE(TAG, "no memory for framebuffer");
}

void displayShow(const DisplayModel& model) {
    if (!s_panel || !s_fb) return;
    if (s_hasShown && memcmp(&model, &s_shown, sizeof(model)) == 0) return;
    s_shown = model;
    s_hasShown = true;
    render(model);
}
#else
void displayInit() {}
void displayShow(const DisplayModel&) {}
#endif

void displaySetLine(DisplayModel& m, int line, uint16_t color, const char* fmt, ...) {
    if (line < 0 || line >= DISPLAY_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.lines[line].text, sizeof(m.lines[line].text), fmt, ap);
    va_end(ap);
    m.lines[line].color = color;
}
