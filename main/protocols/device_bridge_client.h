#ifndef DEVICE_BRIDGE_CLIENT_H_
#define DEVICE_BRIDGE_CLIENT_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class DeviceBridgeClient {
public:
    struct Config {
        std::string uri;
        std::string device_id;
        std::string token;
    };
    // Shared validation/storage contract for local provisioning. Never logs credentials.
    static bool ValidateConfig(const Config& config);
    static bool ParseConfig(const char* json, Config* config);
    static bool SaveConfig(const Config& config);
    using CommandHandler = std::function<bool(const std::string&, std::string*, std::string*)>;

    explicit DeviceBridgeClient(CommandHandler command_handler);
    void Start();
    bool IsConnected() const { return connected_.load(std::memory_order_acquire); }
    // Call only from a separate worker, never from CommandHandler/the bridge
    // receive loop. A single bounded request shares the existing TLS socket.
    bool Request(const std::string& payload, std::string* result, std::string* error,
                 uint32_t timeout_ms = 20000);

private:
    static void TaskEntry(void* context);
    void Run();
    void HandleMessage(void* socket, const char* data, size_t length, bool binary);
    void SendRequest(void* socket);
    void FailRequest(const char* error);
    struct RpcRequest;
    std::mutex request_mutex_;
    std::shared_ptr<RpcRequest> request_;
    void* wake_queue_ = nullptr;
    uint32_t request_sequence_ = 0;

    CommandHandler command_handler_;
    Config config_;
    bool started_ = false;
    std::atomic_bool connected_{false};
};

#endif  // DEVICE_BRIDGE_CLIENT_H_
