#include "robot_bridge_provisioning.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include <driver/uart.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include "protocols/device_bridge_client.h"

namespace kynangso {
namespace {
#if CONFIG_ESP_CONSOLE_UART
constexpr auto kUart = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);

void Reply(const char* reply) {
    // Bypass esp_log_set_vprintf: credentials/commands never enter the web log worker.
    // stdio's lock serializes this fixed reply with ordinary console log output.
    flockfile(stdout);
    fprintf(stdout, "\nKNSBRIDGE %s\n", reply);
    fflush(stdout);
    funlockfile(stdout);
}

// Flash/NVS operations require an internal stack. Allocate that stack only
// while a deliberate provisioning command is being processed, not during media.
std::atomic_bool saving{false};
struct SaveJob {
    char json[1024];
};

void SaveWorker(void* context) {
    auto* job = static_cast<SaveJob*>(context);
    {
        DeviceBridgeClient::Config config;
        const bool valid = DeviceBridgeClient::ParseConfig(job->json, &config);
        memset(job, 0, sizeof(*job));
        heap_caps_free(job);
        if (!valid) {
            Reply("ERROR invalid_config");
        } else if (!DeviceBridgeClient::SaveConfig(config)) {
            Reply("ERROR storage");
        } else {
            Reply("ACK");
            uart_wait_tx_done(kUart, pdMS_TO_TICKS(1000));
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
    }
    saving.store(false);
    vTaskDelete(nullptr);
}

void SubmitConfig(const char* json) {
    bool expected = false;
    if (!saving.compare_exchange_strong(expected, true)) {
        Reply("ERROR busy");
        return;
    }
    auto* job = static_cast<SaveJob*>(
        heap_caps_calloc(1, sizeof(SaveJob), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (job) {
        memcpy(job->json, json, strlen(json) + 1);
        if (xTaskCreate(SaveWorker, "bridge_save", 4096, job, 2, nullptr) == pdPASS)
            return;
        memset(job, 0, sizeof(*job));
        heap_caps_free(job);
    }
    saving.store(false);
    Reply("ERROR busy");
}

bool ReadDeviceId(char* device_id, size_t capacity) {
    if (!device_id || capacity < 2)
        return false;
    // Pairing identity belongs to the hardware, independent of old WSS config.
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
        return false;
    const int written = snprintf(device_id, capacity, "kns-%02x%02x%02x%02x%02x%02x",
                                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return written > 0 && static_cast<size_t>(written) < capacity;
}

bool ValidPairingChallenge(const char* challenge) {
    const size_t length = challenge ? strlen(challenge) : 0;
    if (length < 32 || length > 64)
        return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = challenge[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

void ProvisioningWorker(void*) {
    char line[1024] = {};
    size_t used = 0;
    bool discard = false;
    TickType_t last_byte = xTaskGetTickCount();
    while (true) {
        uint8_t byte;
        const int count = uart_read_bytes(kUart, &byte, 1, pdMS_TO_TICKS(100));
        const auto now = xTaskGetTickCount();
        if (used && now - last_byte > pdMS_TO_TICKS(5000)) {
            // A timed-out fragment must not become a new command until the next newline.
            used = 0;
            discard = true;
        }
        if (count != 1)
            continue;
        last_byte = now;
        if (byte == '\n') {
            if (!discard && used) {
                if (line[used - 1] == '\r')
                    --used;
                line[used] = '\0';
                if (strcmp(line, "KNSBRIDGE PING") == 0) {
                    Reply("READY");
                } else if (strcmp(line, "KNSBRIDGE INFO") == 0) {
                    // Return only a stable public identity; never echo credential data.
                    char device_id[65] = {};
                    const bool known = ReadDeviceId(device_id, sizeof(device_id));
                    flockfile(stdout);
                    fprintf(stdout, "\nKNSBRIDGE ID %s\n", known ? device_id : "-");
                    fflush(stdout);
                    funlockfile(stdout);
                } else if (strncmp(line, "KNSBRIDGE PAIR ", 15) == 0) {
                    const char* challenge = line + 15;
                    char device_id[65] = {};
                    char reply[160] = {};
                    if (!ValidPairingChallenge(challenge) ||
                        !ReadDeviceId(device_id, sizeof(device_id))) {
                        Reply("ERROR pairing");
                    } else {
                        snprintf(reply, sizeof(reply), "PAIRED %.64s %.64s", challenge, device_id);
                        Reply(reply);
                        memset(reply, 0, sizeof(reply));
                    }
                    memset(device_id, 0, sizeof(device_id));
                } else if (strncmp(line, "KNSBRIDGE ", 10) == 0) {
                    SubmitConfig(line + 10);
                }
            }
            memset(line, 0, sizeof(line));
            used = 0;
            discard = false;
        } else if (!discard) {
            if (byte == 0 || used == sizeof(line) - 1) {
                // Silently drain the entire oversized/binary line, never parse fragments.
                memset(line, 0, sizeof(line));
                used = 0;
                discard = true;
            } else {
                line[used++] = static_cast<char>(byte);
            }
        }
    }
}
#endif
}  // namespace

void StartBridgeProvisioning() {
#if CONFIG_ESP_CONSOLE_UART
    static bool started = false;
    if (started)
        return;
    // Keep ROM/VFS console TX and pins as configured. Do not switch stdin to a
    // line editor (which could echo secrets), or attach a second UART consumer.
    if (uart_is_driver_installed(kUart) ||
        uart_driver_install(kUart, 1024, 0, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGW("BridgeProvision", "Console RX unavailable for local provisioning");
        return;
    }
    // Keep provisioning cache-safe: flash-related operations must never run
    // with their task stack in PSRAM.
    if (xTaskCreateWithCaps(ProvisioningWorker, "bridge_provision", 6144, nullptr, 2, nullptr,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        uart_driver_delete(kUart);
        ESP_LOGE("BridgeProvision", "Cannot start provisioning worker");
        return;
    }
    started = true;
#endif
}
}  // namespace kynangso
