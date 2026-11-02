#include "features/firmware/robot_ota.h"
#include <esp_app_desc.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <cJSON.h>
#include <freertos/idf_additions.h>
#include <psa/crypto.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include "application.h"
#include "board.h"
#include "display.h"
#include "ota.h"

namespace kynangso {
namespace {
std::function<void()> before_update;
std::function<void(const char*, int)> show_progress;
std::string assets_digest;
std::string attempted_release;
std::string layout_digest;
std::atomic<bool> boot_ready{false};
std::atomic<bool> updating{false};
std::atomic<bool> last_failed{false};
std::string Hex(const uint8_t* bytes) {
    char output[65] = {};
    for (size_t i = 0; i < 32; ++i)
        snprintf(output + i * 2, 3, "%02x", bytes[i]);
    return output;
}
bool Call(cJSON* value, std::string* response = nullptr) {
    char* encoded = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (!encoded)
        return false;
    std::string request(encoded), result, error;
    cJSON_free(encoded);
    bool ok = Application::GetInstance().RequestService(request, &result, &error);
    if (response)
        *response = std::move(result);
    return ok;
}
bool Report(const char* state, int progress = 0, const std::string& layout = "") {
    cJSON* value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "op", "firmware.report");
    cJSON_AddStringToObject(value, "board", "kynangso-robot");
    cJSON_AddBoolToObject(value, "ota_capable", true);
    cJSON_AddStringToObject(value, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(value, "app_elf_sha256",
                            Hex(esp_app_get_description()->app_elf_sha256).c_str());
    if (!layout.empty())
        cJSON_AddStringToObject(value, "layout_sha256", layout.c_str());
    if (!assets_digest.empty())
        cJSON_AddStringToObject(value, "assets_sha256", assets_digest.c_str());
    if (!attempted_release.empty())
        cJSON_AddStringToObject(value, "target_release_id", attempted_release.c_str());
    cJSON_AddStringToObject(value, "state", state);
    cJSON_AddNumberToObject(value, "progress", progress);
    return Call(value);
}
void Confirm(void*) {
    {
        Ota ota;
        ota.MarkCurrentVersionValid();
    }

    auto* partition = static_cast<uint8_t*>(heap_caps_malloc(0xc00, MALLOC_CAP_INTERNAL));
    if (partition) {
        uint8_t hash[32];
        size_t hash_length = 0;
        if (esp_flash_read(esp_flash_default_chip, partition, CONFIG_PARTITION_TABLE_OFFSET,
                           0xc00) == ESP_OK &&
            psa_crypto_init() == PSA_SUCCESS &&
            psa_hash_compute(PSA_ALG_SHA_256, partition, 0xc00, hash, sizeof(hash), &hash_length) ==
                PSA_SUCCESS &&
            hash_length == 32)
            layout_digest = Hex(hash);
        heap_caps_free(partition);
    }
    auto* assets =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "assets");
    uint32_t asset_header[3] = {};
    if (assets && esp_partition_read(assets, 0, asset_header, sizeof(asset_header)) == ESP_OK &&
        asset_header[2] > 0 && asset_header[2] <= assets->size - sizeof(asset_header)) {
        auto* chunk = static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL));
        if (chunk) {
            psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
            bool ok = psa_crypto_init() == PSA_SUCCESS &&
                      psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
            size_t length = sizeof(asset_header) + asset_header[2];
            for (size_t offset = 0; ok && offset < length; offset += 4096) {
                size_t size = std::min<size_t>(4096, length - offset);
                ok = esp_partition_read(assets, offset, chunk, size) == ESP_OK &&
                     psa_hash_update(&operation, chunk, size) == PSA_SUCCESS;
            }
            uint8_t hash[32];
            size_t length_out = 0;
            if (ok && psa_hash_finish(&operation, hash, sizeof(hash), &length_out) == PSA_SUCCESS &&
                length_out == 32)
                assets_digest = Hex(hash);
            psa_hash_abort(&operation);
            heap_caps_free(chunk);
        }
    }
    boot_ready.store(true);
    vTaskDelete(nullptr);
}
struct DownloadJob {
    std::string url, version, sha;
    size_t size;
};
void Download(void* arg) {
    {
        std::unique_ptr<DownloadJob> job(static_cast<DownloadJob*>(arg));
        const bool reported_start = Report("downloading");
        if (show_progress)
            show_progress("Đang tải firmware", 0);
        int last_report = -10;
        bool success = reported_start && Application::GetInstance().UpgradeFirmware(
                                             job->url, job->version, job->sha, job->size,
                                             [&last_report](int progress) {
                                                 if (show_progress)
                                                     show_progress("Đang tải firmware", progress);
                                                 if (progress - last_report >= 10) {
                                                     last_report = progress;
                                                     Report("downloading", progress);
                                                 }
                                             });
        if (!success) {
            if (!reported_start)
                Application::GetInstance().Schedule(
                    []() { Application::GetInstance().SetDeviceState(kDeviceStateIdle); });
            last_failed.store(true);
            if (Report("failed"))
                last_failed.store(false);
            if (show_progress)
                show_progress("Cập nhật thất bại", 0);
            vTaskDelay(pdMS_TO_TICKS(4000));
            if (show_progress)
                show_progress(nullptr, -1);
        }
    }
    updating.store(false);
    vTaskDelete(nullptr);
}
void Worker(void*) {
    auto& app = Application::GetInstance();
    // Local startup confirmation is independent of the availability of cloud services.
    vTaskDelay(pdMS_TO_TICKS(45000));
    if (xTaskCreate(Confirm, "ota_confirm", 4096, nullptr, 2, nullptr) != pdPASS)
        ESP_LOGW("RobotOta", "Unable to confirm image; leaving rollback enabled");
    for (int i = 0; i < 500 && !boot_ready.load(); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    while (true) {
        if (boot_ready.load() && !updating.load() && app.IsDeviceBridgeConnected() &&
            !layout_digest.empty()) {
            if (!Report(last_failed.load() ? "failed" : "ready", 0, layout_digest)) {
                vTaskDelay(pdMS_TO_TICKS(60000));
                continue;
            }
            last_failed.store(false);
            cJSON* request = cJSON_CreateObject();
            cJSON_AddStringToObject(request, "op", "firmware.check");
            std::string response;
            if (Call(request, &response)) {
                std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(response.c_str()),
                                                                     cJSON_Delete);
                auto* install = cJSON_GetObjectItem(root.get(), "install");
                auto* url = cJSON_GetObjectItem(root.get(), "url");
                auto* sha = cJSON_GetObjectItem(root.get(), "sha256");
                auto* size = cJSON_GetObjectItem(root.get(), "size");
                auto* version = cJSON_GetObjectItem(root.get(), "version");
                auto* release = cJSON_GetObjectItem(root.get(), "release_id");
                if (cJSON_IsTrue(install) && cJSON_IsString(release) &&
                    std::strlen(release->valuestring) <= 128 && cJSON_IsString(url) &&
                    cJSON_IsString(sha) && cJSON_IsNumber(size) && size->valuedouble > 256 &&
                    size->valuedouble <= 0x3f0000 && cJSON_IsString(version) &&
                    std::string(url->valuestring)
                        .starts_with("https://robot.kynangso.com/device/firmware/")) {
                    attempted_release = release->valuestring;
                    auto admission = std::make_shared<std::atomic<int>>(0);
                    app.Schedule([admission]() {
                        int expected = 0;
                        if (!admission->compare_exchange_strong(expected, 2))
                            return;
                        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
                            admission->store(-1);
                            return;
                        }
                        if (before_update)
                            before_update();
                        Application::GetInstance().SetDeviceState(kDeviceStateUpgrading);
                        admission->store(1);
                    });
                    for (int i = 0; i < 100 && (admission->load() == 0 || admission->load() == 2);
                         ++i)
                        vTaskDelay(pdMS_TO_TICKS(50));
                    int expected = 0;
                    admission->compare_exchange_strong(expected, -1);
                    while (admission->load() == 2)
                        vTaskDelay(pdMS_TO_TICKS(10));
                    if (admission->load() == 1) {
                        auto* job = new (std::nothrow)
                            DownloadJob{url->valuestring, version->valuestring, sha->valuestring,
                                        static_cast<size_t>(size->valuedouble)};
                        updating.store(true);
                        last_failed.store(false);
                        if (!job || xTaskCreate(Download, "ota_download", 8192, job, 2, nullptr) !=
                                        pdPASS) {
                            delete job;
                            updating.store(false);
                            last_failed.store(true);
                            if (Report("failed"))
                                last_failed.store(false);
                            if (show_progress)
                                show_progress(nullptr, -1);
                            app.Schedule([]() {
                                Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                            });
                        }
                    } else
                        Report("waiting_idle");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(60000 + esp_random() % 15000));
    }
}
}  // namespace
void StartRobotOta(std::function<void()> callback, std::function<void(const char*, int)> progress) {
    before_update = std::move(callback);
    show_progress = std::move(progress);
    if (xTaskCreateWithCaps(Worker, "robot_ota", 6144, nullptr, 2, nullptr,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
        ESP_LOGE("RobotOta", "Cannot start OTA worker");
}
}  // namespace kynangso
