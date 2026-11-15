#include "config.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "DisplayDiagnostic";
static SemaphoreHandle_t transfer_done;
static esp_lcd_panel_handle_t tft;
static uint16_t* stripe;
static i2c_master_bus_handle_t oled_bus;
static i2c_master_dev_handle_t oled;

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* event,
                       void* user) {
    (void)io;
    (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user, &wake);
    return wake == pdTRUE;
}

static void set_output(gpio_num_t pin, int level) {
    gpio_set_level(pin, level);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

static void init_tft(void) {
    ESP_LOGI(TAG, "TFT CS=%d DC=%d RST=%d MOSI=%d CLK=%d BL=%d; SPI3 10MHz", DISPLAY_CS_PIN,
             DISPLAY_DC_PIN, DISPLAY_RST_PIN, DISPLAY_MOSI_PIN, DISPLAY_CLK_PIN,
             DISPLAY_BACKLIGHT_PIN);
    set_output(DISPLAY_BACKLIGHT_PIN, !DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    transfer_done = xSemaphoreCreateBinary();
    stripe = heap_caps_malloc(DISPLAY_WIDTH * 20 * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(transfer_done && stripe ? ESP_OK : ESP_ERR_NO_MEM);
    spi_bus_config_t bus = {
        .mosi_io_num = DISPLAY_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = DISPLAY_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * 20 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = DISPLAY_CS_PIN,
        .dc_gpio_num = DISPLAY_DC_PIN,
        .spi_mode = DISPLAY_SPI_MODE,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 1,
        .on_color_trans_done = color_done,
        .user_ctx = transfer_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io));
    esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = DISPLAY_RST_PIN,
        .rgb_ele_order = DISPLAY_RGB_ORDER,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel, &tft));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(tft));
    ESP_ERROR_CHECK(esp_lcd_panel_init(tft));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(tft, DISPLAY_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(tft, true));
}

static void draw_tft(unsigned phase) {
    // Cycle both controller axes to distinguish an incorrect RAM gap/rotation
    // from a panel which never receives data. No LVGL or saved settings.
    static const int gaps[][2] = {{0, 0}, {DISPLAY_PANEL_GAP_X, DISPLAY_PANEL_GAP_Y}, {0, 80}};
    static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    unsigned mode = phase % 6;
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(tft, mode >= 3));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(tft, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(tft, gaps[mode % 3][0], gaps[mode % 3][1]));
    for (int y = 0; y < DISPLAY_HEIGHT; y += 20) {
        uint16_t color = colors[((y / 60) + phase) % 4];
        color = (uint16_t)((color << 8) | (color >> 8));
        for (int x = 0; x < DISPLAY_WIDTH * 20; ++x)
            stripe[x] = color;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(tft, 0, y, DISPLAY_WIDTH, y + 20, stripe));
        ESP_ERROR_CHECK(
            xSemaphoreTake(transfer_done, pdMS_TO_TICKS(500)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
    }
    ESP_LOGI(TAG,
             "TFT phase=%u swap=%d gap=(%d,%d): all SPI transfers completed; check visible bars",
             phase, mode >= 3, gaps[mode % 3][0], gaps[mode % 3][1]);
}

static void init_oled(void) {
    gpio_config_t released = {
        .pin_bit_mask = (1ULL << 2) | (1ULL << 1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&released));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "OLED released inputs: SDA2=%d SCL1=%d", gpio_get_level(2), gpio_get_level(1));
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = GPIO_NUM_2,
        .scl_io_num = GPIO_NUM_1,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &oled_bus));
    for (unsigned addr = 0x3c; addr <= 0x3d; ++addr) {
        esp_err_t err = i2c_master_probe(oled_bus, addr, 50);
        ESP_LOGI(TAG, "OLED probe 0x%02x: %s SDA=%d SCL=%d", addr, esp_err_to_name(err),
                 gpio_get_level(2), gpio_get_level(1));
        if (err != ESP_OK)
            continue;
        i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 100000};
        ESP_ERROR_CHECK(i2c_master_bus_add_device(oled_bus, &dev, &oled));
        // SSD1306 128x32, internal charge pump, full RAM display. Every command
        // has a finite timeout, including when the bus fails after a successful probe.
        const uint8_t init[] = {0x00, 0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
                                0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x02, 0x81,
                                0xff, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf};
        err = i2c_master_transmit(oled, init, sizeof(init), 100);
        ESP_LOGI(TAG, "OLED initialization: %s", esp_err_to_name(err));
        if (err != ESP_OK) {
            i2c_master_bus_rm_device(oled);
            oled = NULL;
        } else {
            const uint8_t force_all[] = {0x00, 0xa5};
            err = i2c_master_transmit(oled, force_all, sizeof(force_all), 100);
            ESP_LOGI(TAG, "OLED force-all pixels at maximum contrast: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));
            const uint8_t restore_ram[] = {0x00, 0xa4};
            if (err == ESP_OK)
                err = i2c_master_transmit(oled, restore_ram, sizeof(restore_ram), 100);
            if (err != ESP_OK) {
                i2c_master_bus_rm_device(oled);
                oled = NULL;
            }
        }
        break;
    }
}

static void draw_oled(unsigned phase) {
    if (!oled)
        return;
    uint8_t position[] = {0x00, 0x21, 0, 127, 0x22, 0, 3};
    uint8_t pixels[513];
    pixels[0] = 0x40;
    for (unsigned i = 0; i < 512; ++i)
        pixels[i + 1] = ((i / 8 + phase) & 1) ? 0xff : 0x00;
    esp_err_t err = i2c_master_transmit(oled, position, sizeof(position), 100);
    if (err == ESP_OK)
        err = i2c_master_transmit(oled, pixels, sizeof(pixels), 100);
    ESP_LOGI(TAG, "OLED stripe pattern: %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(oled);
        oled = NULL;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "RAM-ONLY display isolation: no flash writes, NVS, SD, D1, Wi-Fi, LVGL or audio");
    set_output(STEPPER_INT1_GPIO, 0);
    set_output(STEPPER_INT2_GPIO, 0);
    set_output(STEPPER_INT3_GPIO, 0);
    set_output(STEPPER_INT4_GPIO, 0);
    set_output(SD_CS_PIN, 1);
    // Allow the host to attach at the normal console baud after load-ram.
    vTaskDelay(pdMS_TO_TICKS(3000));
    init_tft();
    draw_tft(0);
    init_oled();
    for (unsigned phase = 0;; ++phase) {
        draw_tft(phase);
        draw_oled(phase);
        if (!oled)
            ESP_LOGW(TAG, "OLED unavailable: SDA2=%d SCL1=%d", gpio_get_level(2),
                     gpio_get_level(1));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
