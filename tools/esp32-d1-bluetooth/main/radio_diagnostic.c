#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

void app_main(void) {
    const char *tag = "D1_DIAG";
    ESP_LOGI(tag, "Application started");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t ap = {.ap = {
        .ssid = "D1-DIAG-WIFI",
        .channel = 1,
        .authmode = WIFI_AUTH_OPEN,
        .max_connection = 2,
    }};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(tag, "Wi-Fi AP ready: D1-DIAG-WIFI");

    esp_bt_controller_config_t bt = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("D1-DIAG-BT"));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
    while (true) {
        ESP_LOGI(tag, "Alive: Wi-Fi AP and Classic Bluetooth ready");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
