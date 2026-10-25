#ifndef DEVICE_BRIDGE_CLIENT_H_
#define DEVICE_BRIDGE_CLIENT_H_

#include <atomic>
#include <functional>
#include <string>

class DeviceBridgeClient {
public:
    using CommandHandler =
        std::function<bool(const std::string&, std::string*, std::string*)>;

    explicit DeviceBridgeClient(CommandHandler command_handler);
    void Start();
    bool IsConnected() const { return connected_.load(std::memory_order_acquire); }

private:
    static void TaskEntry(void* context);
    void Run();
    void HandleMessage(void* socket, const char* data, size_t length, bool binary);

    CommandHandler command_handler_;
    bool started_ = false;
    std::atomic_bool connected_{false};
};

#endif  // DEVICE_BRIDGE_CLIENT_H_
