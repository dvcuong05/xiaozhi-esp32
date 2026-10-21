#ifndef DEVICE_BRIDGE_CLIENT_H_
#define DEVICE_BRIDGE_CLIENT_H_

#include <functional>
#include <string>

class DeviceBridgeClient {
public:
    using CommandHandler =
        std::function<bool(const std::string&, std::string*, std::string*)>;

    explicit DeviceBridgeClient(CommandHandler command_handler);
    void Start();

private:
    static void TaskEntry(void* context);
    void Run();
    void HandleMessage(void* socket, const char* data, size_t length, bool binary);

    CommandHandler command_handler_;
    bool started_ = false;
};

#endif  // DEVICE_BRIDGE_CLIENT_H_
