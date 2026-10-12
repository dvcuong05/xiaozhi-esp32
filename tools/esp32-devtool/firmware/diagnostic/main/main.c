#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_rom_gpio.h"
#include "soc/soc_caps.h"

static const char *TAG = "DIAGNOSTIC";

/**
 * Dynamically check if a GPIO pin is safe to probe without crashing the CPU
 * (must avoid SPI Flash/PSRAM bus pins and active Serial/JTAG console pins).
 */
static bool is_safe_gpio(int gpio) {
    if (!GPIO_IS_VALID_GPIO(gpio)) {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32
    // Flash SPI: GPIO 6-11, UART0 Console: GPIO 1, 3
    if (gpio >= 6 && gpio <= 11) return false;
    if (gpio == 1 || gpio == 3) return false;
    return true;

#elif CONFIG_IDF_TARGET_ESP32S3
    // Flash & Octal PSRAM: GPIO 26-37, UART0: GPIO 43, 44, USB-Serial-JTAG: GPIO 19, 20
    if (gpio >= 26 && gpio <= 37) return false;
    if (gpio == 43 || gpio == 44) return false;
    if (gpio == 19 || gpio == 20) return false;
    return true;

#elif CONFIG_IDF_TARGET_ESP32C3
    // Flash SPI: GPIO 12-17, UART0: GPIO 20, 21, USB-Serial-JTAG: GPIO 18, 19
    if (gpio >= 12 && gpio <= 17) return false;
    if (gpio == 20 || gpio == 21) return false;
    if (gpio == 18 || gpio == 19) return false;
    return true;

#elif CONFIG_IDF_TARGET_ESP32C6
    // Flash SPI: GPIO 24-30, UART0: GPIO 16, 17, USB-Serial-JTAG: GPIO 12, 13
    if (gpio >= 24 && gpio <= 30) return false;
    if (gpio == 16 || gpio == 17) return false;
    if (gpio == 12 || gpio == 13) return false;
    return true;

#else
    // Generic fallback: avoid typical flash & uart pins
    if (gpio >= 6 && gpio <= 11) return false;
    if (gpio == 1 || gpio == 3) return false;
    return true;
#endif
}

static bool is_input_only_gpio(int gpio) {
#if CONFIG_IDF_TARGET_ESP32
    // On ESP32 classic, GPIO 34-39 are GPI (input-only pad has no internal PU/PD)
    return (gpio >= 34 && gpio <= 39);
#else
    return false;
#endif
}

typedef struct {
    int gpio;
    const char* state;
    bool has_component;
    const char* detail;
} pin_result_t;

static pin_result_t results[SOC_GPIO_PIN_COUNT];
static int reported_count = 0;

static void probe_all_pins(void) {
    reported_count = 0;

    for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
        if (!is_safe_gpio(pin)) {
            continue;
        }

        int idx = reported_count++;
        results[idx].gpio = pin;

        // Reset pin to GPIO mode
        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);

        // Input-only pins (e.g. GPIO 34-39 on ESP32) lack internal pull-up / pull-down hardware
        if (is_input_only_gpio(pin)) {
            int lvl = gpio_get_level((gpio_num_t)pin);
            results[idx].state = lvl ? "input_high" : "input_low";
            results[idx].has_component = false;
            results[idx].detail = lvl ? "Input-only GPI (Reading HIGH)" : "Input-only GPI (Reading LOW, no internal PU/PD)";
            continue;
        }

        // Step 1: Internal Pull-Down
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLDOWN_ONLY);
        vTaskDelay(pdMS_TO_TICKS(2));
        int lvl_pd = gpio_get_level((gpio_num_t)pin);

        // Step 2: Internal Pull-Up
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
        vTaskDelay(pdMS_TO_TICKS(2));
        int lvl_pu = gpio_get_level((gpio_num_t)pin);

        // Step 3: Release to floating
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_FLOATING);

        // Analyze electrical response
        if (lvl_pd == 0 && lvl_pu == 1) {
            // Pin easily changes with internal 45k resistor -> Floating / unconnected
            results[idx].state = "floating";
            results[idx].has_component = false;
            results[idx].detail = "No wire or component (floating)";
        } else if (lvl_pd == 1 && lvl_pu == 1) {
            // Pin holds HIGH against pull-down -> External Pull-up or VCC wire
            results[idx].state = "tied_high";
            results[idx].has_component = true;
            results[idx].detail = "External Pull-Up / VCC wire connected";
        } else if (lvl_pd == 0 && lvl_pu == 0) {
            // Pin holds LOW against pull-up -> External Pull-down / GND / LED connected
            results[idx].state = "tied_low";
            results[idx].has_component = true;
            results[idx].detail = "External Pull-Down / GND / LED connected";
        } else {
            results[idx].state = "active_driven";
            results[idx].has_component = true;
            results[idx].detail = "Driven by active external signal";
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Dynamic Pin Diagnostic Engine for target '%s'...", CONFIG_IDF_TARGET);
    vTaskDelay(pdMS_TO_TICKS(500));

    while (1) {
        probe_all_pins();

        // Output JSON packet to Serial
        printf("DIAG:{\"chip\":\"%s\",\"event\":\"diagnostic_report\",\"pins\":[", CONFIG_IDF_TARGET);
        for (int i = 0; i < reported_count; i++) {
            printf("{\"gpio\":%d,\"state\":\"%s\",\"has_component\":%s,\"detail\":\"%s\"}%s",
                   results[i].gpio,
                   results[i].state,
                   results[i].has_component ? "true" : "false",
                   results[i].detail,
                   (i < reported_count - 1) ? "," : "");
        }
        printf("]}\n");

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
