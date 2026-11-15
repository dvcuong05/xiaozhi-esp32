#include "integration/robot_mcp_tools.h"
#include "sdkconfig.h"
#include "application.h"
#include "core/robot_controller.h"
#include "features/motion/robot_drive.h"
#include "features/storage/sd_storage.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include <wifi_manager.h>

static const char* TAG = "RobotMCP";

namespace {

// Legacy signed steps select forward/backward; each unit lasts delay_ms milliseconds.
static ToolResult MoveMotor(kynangso::RobotController& controller, const PropertyList& properties) {
    int steps = properties["steps"].value<int>();
    int delay_ms = 0;

    if (properties["delay_ms"].has_default_value()) {
        delay_ms = properties["delay_ms"].value<int>();
    }

    ESP_LOGI(TAG, "Moving DC motors: units=%d, delay=%dms", steps, delay_ms);
    if (!controller.Step(steps, delay_ms)) {
        return std::unexpected("Motor command failed");
    }
    return std::string("Motor command started");
}

static ToolResult StopMotor(kynangso::RobotController& controller, const PropertyList&) {
    ESP_LOGI(TAG, "Stopping DC motors");
    controller.Stop();

    return std::string("Motor stopped");
}

static ToolResult SetMotorDurationUnit(kynangso::RobotController& controller,
                                       const PropertyList& properties) {
    int delay_ms = properties["delay_ms"].value<int>();

    ESP_LOGI(TAG, "Setting motor duration unit: %dms", delay_ms);
    controller.SetDurationUnit(delay_ms);

    return std::string("Motor duration unit set");
}

static std::expected<RobotDrive::Direction, std::string> ParseDirection(const std::string& value) {
    if (value == "forward") {
        return RobotDrive::Direction::Forward;
    }
    if (value == "backward") {
        return RobotDrive::Direction::Backward;
    }
    if (value == "left") {
        return RobotDrive::Direction::Left;
    }
    if (value == "right") {
        return RobotDrive::Direction::Right;
    }
    return std::unexpected("Direction must be forward, backward, left, or right");
}

static ToolResult RobotMove(kynangso::RobotController& controller, const PropertyList& properties) {
    auto direction = ParseDirection(properties["direction"].value<std::string>());
    if (!direction) {
        return std::unexpected(direction.error());
    }
    const int duration_ms = properties["duration_ms"].value<int>();
    if (!controller.Move(*direction, duration_ms, 1)) {
        return std::unexpected("Robot movement could not be started");
    }
    return "Robot moving for " + std::to_string(duration_ms) + " ms";
}

static ToolResult StartLivelyRoutine(kynangso::RobotController& controller, const PropertyList&) {
    if (!controller.StartRoutine(RobotDrive::Routine::Lively)) {
        return std::unexpected("Robot is busy or the movement task is unavailable");
    }
    return "Lively routine started";
}

static ToolResult StartDanceRoutine(kynangso::RobotController& controller, const PropertyList&) {
    if (!controller.StartRoutine(RobotDrive::Routine::Dance)) {
        return std::unexpected("Robot is busy or the movement task is unavailable");
    }
    return "Dance routine started";
}

static ToolResult StopRobot(kynangso::RobotController& controller, const PropertyList&) {
    controller.Stop();
    return "Robot stopped";
}

static ToolResult SetRobotEmotion(kynangso::RobotController& controller,
                                  const PropertyList& properties) {
    const std::string emotion = properties["emotion"].value<std::string>();
    controller.SetEmotion(emotion);
    return "Robot emotion set to " + emotion;
}

static ToolResult SetRobotOledMode(kynangso::RobotController& controller,
                                   const PropertyList& properties) {
    const std::string mode = properties["mode"].value<std::string>();
    int mode_value = -1;
    if (mode == "clock") {
        mode_value = 0;
    } else if (mode == "marquee") {
        mode_value = 1;
    } else if (mode == "status") {
        mode_value = 2;
    } else if (mode == "off") {
        mode_value = 3;
    } else {
        return std::unexpected("OLED mode must be clock, marquee, status, or off");
    }
    if (!controller.SetOledMode(mode_value)) {
        return std::unexpected("Could not apply the secondary OLED mode");
    }
    return "Secondary OLED mode set to " + mode;
}

static ToolResult SetRobotOledMarqueeSpeed(kynangso::RobotController& controller,
                                           const PropertyList& properties) {
    const int speed = properties["speed"].value<int>();
    if (!controller.SetOledMarqueeSpeed(speed)) {
        return std::unexpected("Could not save the secondary OLED marquee speed");
    }
    static constexpr const char* kSpeedNames[] = {"", "slow", "medium", "fast", "very fast",
                                                  "maximum"};
    return std::string("Secondary OLED marquee speed set to ") + kSpeedNames[speed];
}

static ToolResult ShowRobotIp(kynangso::RobotController& controller, const PropertyList&) {
    const std::string ip = controller.ShowIp();
    if (ip.empty()) {
        return std::unexpected("Wi-Fi is not connected");
    }
    return "Robot local IP address is " + ip;
}

static ToolResult ShowRobotOledText(kynangso::RobotController& controller,
                                    const PropertyList& properties) {
    const std::string text = properties["text"].value<std::string>();
    const int duration_ms = properties["duration_ms"].value<int>();
    const int speed = properties["speed"].value<int>();
    if (text.empty()) {
        return std::unexpected("OLED text must not be empty");
    }
    if (!controller.ShowOledText(text, duration_ms, speed)) {
        return std::unexpected("Could not display text on the secondary OLED");
    }
    return "Custom OLED text display started";
}

static ToolResult ShowRobotPaymentQr(kynangso::RobotController& controller,
                                     const PropertyList& properties) {
    const int duration_ms = properties["duration_ms"].value<int>();
    if (!controller.ShowPaymentQr(duration_ms)) {
        return std::unexpected("Could not display the payment QR on the TFT");
    }
    return "Payment QR displayed on the TFT";
}

static ToolResult GetRobotStatus(kynangso::RobotController& controller, const PropertyList&) {
    const auto& wifi = WifiManager::GetInstance();
    const bool connected = wifi.IsConnected();
    const std::string ip = connected ? wifi.GetIpAddress() : "";
    const bool moving = controller.IsRoutineRunning();
    const bool mcp_connected = Application::GetInstance().IsDeviceBridgeConnected();
    const auto sd = controller.Storage().GetStatus();
    auto* status = cJSON_CreateObject();
    if (status == nullptr)
        return std::unexpected("Could not allocate robot status response");

    cJSON_AddBoolToObject(status, "wifi_connected", connected);
    cJSON_AddStringToObject(status, "ip", ip.c_str());
    cJSON_AddBoolToObject(status, "routine_running", moving);
    cJSON_AddBoolToObject(status, "mcp_server_connected", mcp_connected);
    cJSON_AddBoolToObject(status, "sd_card_mounted", sd.state == "mounted");
    cJSON_AddStringToObject(status, "sd_card_state", sd.state.c_str());
    cJSON_AddStringToObject(status, "sd_card_message", sd.message.c_str());

    char* json = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);
    if (json == nullptr)
        return std::unexpected("Could not encode robot status response");
    std::string result(json);
    cJSON_free(json);
    return result;
}

static ToolResult GetInfoShuttleStatus(kynangso::RobotController& controller) {
    const auto config = controller.GetInfoShuttleConfig();
    cJSON* status = cJSON_CreateObject();
    if (status == nullptr)
        return std::unexpected("Không cấp phát được phản hồi trạng thái màn hình chính động");

    cJSON_AddBoolToObject(status, "enabled", config.enabled);
    cJSON_AddStringToObject(status, "order", config.random ? "random" : "sequence");
    cJSON_AddNumberToObject(status, "interval_seconds", config.interval_seconds);
    cJSON_AddBoolToObject(status, "show_face", config.show_face);
    cJSON_AddBoolToObject(status, "show_schedule", config.show_schedule);
    cJSON_AddBoolToObject(status, "show_weather", config.show_weather);

    char* json = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);
    if (json == nullptr)
        return std::unexpected("Không mã hóa được trạng thái màn hình chính động");
    std::string result(json);
    cJSON_free(json);
    return result;
}

static ToolResult SetInfoShuttleEnabled(kynangso::RobotController& controller,
                                        const PropertyList& properties) {
    auto config = controller.GetInfoShuttleConfig();
    config.enabled = properties["enabled"].value<bool>();
    if (!controller.ConfigureInfoShuttle(config))
        return std::unexpected("Không lưu được cấu hình màn hình chính động");
    return GetInfoShuttleStatus(controller);
}

static ToolResult ConfigureInfoShuttle(kynangso::RobotController& controller,
                                       const PropertyList& properties) {
    auto config = controller.GetInfoShuttleConfig();
    config.random = properties["random"].value<bool>();
    config.interval_seconds = properties["interval_seconds"].value<int>();
    config.show_face = properties["show_face"].value<bool>();
    config.show_schedule = properties["show_schedule"].value<bool>();
    config.show_weather = properties["show_weather"].value<bool>();
    if (!controller.ConfigureInfoShuttle(config))
        return std::unexpected(
            "Cấu hình màn hình chính động không hợp lệ; cần bật ít nhất một thẻ");
    return GetInfoShuttleStatus(controller);
}

static ToolResult AdvanceInfoShuttle(kynangso::RobotController& controller) {
    if (!controller.AdvanceInfoShuttle())
        return std::unexpected("Màn hình chính động đang tắt; hãy bật trước khi chuyển thẻ");
    return "Đã gửi yêu cầu chuyển thẻ. Nội dung TFT được ưu tiên vẫn giữ nguyên.";
}

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
static ToolResult BluetoothStatusTool(kynangso::RobotController& controller,
                                      const PropertyList&) {
    const auto status = controller.Bluetooth().GetStatus();
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return std::unexpected("Could not allocate Bluetooth status response");
    cJSON_AddBoolToObject(root, "enabled", status.enabled);
    cJSON_AddBoolToObject(root, "online", status.online);
    cJSON_AddBoolToObject(root, "connected", status.connected);
    cJSON_AddBoolToObject(root, "streaming", status.streaming);
    cJSON_AddBoolToObject(root, "scanning", status.scanning);
    cJSON_AddBoolToObject(root, "switching", status.switching);
    cJSON_AddStringToObject(root, "current", status.current.c_str());
    cJSON_AddStringToObject(root, "target", status.target.c_str());
    cJSON_AddStringToObject(root, "message", status.message.c_str());
    cJSON* devices = cJSON_AddArrayToObject(root, "devices");
    for (const auto& device : status.devices) {
        cJSON* item = cJSON_CreateObject();
        if (!item)
            continue;
        cJSON_AddStringToObject(item, "name", device.name.c_str());
        cJSON_AddStringToObject(item, "address", device.address.c_str());
        cJSON_AddNumberToObject(item, "rssi", device.rssi);
        cJSON_AddItemToArray(devices, item);
    }
    char* encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!encoded)
        return std::unexpected("Could not encode Bluetooth status");
    std::string result(encoded);
    cJSON_free(encoded);
    return result;
}

static ToolResult BluetoothScanTool(kynangso::RobotController& controller, const PropertyList&) {
    std::string error;
    if (!controller.Bluetooth().Scan(&error))
        return std::unexpected(error);
    return "Đã bắt đầu quét; danh sách loa sẽ hiện trên TFT. Chạm đơn chọn loa tiếp, chạm đôi kết nối nếu touch đã bật; hoặc dùng VOL/BOOT; gọi self.robot.bluetooth.status để đọc kết quả.";
}

static ToolResult BluetoothConnectTool(kynangso::RobotController& controller,
                                       const PropertyList& properties) {
    const auto name = properties["name"].value<std::string>();
    std::string error;
    if (!controller.Bluetooth().ConnectByName(name, &error))
        return std::unexpected(error);
    return "Đang kết nối loa đã chọn; theo dõi trạng thái trên TFT và OLED nếu có.";
}

static ToolResult BluetoothDisconnectTool(kynangso::RobotController& controller,
                                          const PropertyList&) {
    std::string error;
    if (!controller.Bluetooth().Disconnect(&error))
        return std::unexpected(error);
    return "Đã yêu cầu ngắt loa; trạng thái được hiển thị trên robot.";
}

#endif

// MCP Tool: Toggle chat state
static ToolResult ToggleChat(const PropertyList&) {
    ESP_LOGI(TAG, "Toggling chat state");
    auto& app = Application::GetInstance();
    app.ToggleChatState();
    return std::string("Chat state toggled");
}

// MCP Tool: Start listening
static ToolResult StartListening(const PropertyList&) {
    ESP_LOGI(TAG, "Starting listening");
    auto& app = Application::GetInstance();
    app.StartListening();
    return std::string("Listening started");
}

// MCP Tool: Stop listening
static ToolResult StopListening(const PropertyList&) {
    ESP_LOGI(TAG, "Stopping listening");
    auto& app = Application::GetInstance();
    app.StopListening();
    return std::string("Listening stopped");
}

}  // namespace

void RegisterRobotMcpTools(kynangso::RobotController& controller) {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.time.schedule",
        "Quản lý báo thức và nhắc việc theo giờ Việt Nam UTC+7. Khi người dùng hỏi lịch, gọi "
        "op=status/list để lấy toàn bộ lịch, op=today để lấy lịch đến hạn hôm nay, op=next để lấy "
        "lịch kế tiếp; kết quả sắp theo giờ và due_local là giờ Việt Nam. Chỉ dùng lịch "
        "enabled=true. "
        "id dùng cho thao tác tiếp theo. Với tên lịch trùng/không rõ, hỏi lại thay vì đoán id. "
        "Chỉ báo thành công khi tool trả ok=true. op=add kind=alarm dùng hour/minute; days=0 một "
        "lần, 127 mỗi ngày, 62 thứ hai-thứ sáu (bit0 Chủ nhật..bit6 thứ bảy). kind=reminder dùng "
        "at=YYYY-MM-DDTHH:MM; days=0 một lần, khác 0 lặp theo các ngày đã chọn, bắt đầu vào lần "
        "được chọn đầu tiên từ ngày/giờ at trở đi; ví dụ thứ 2/4/6 là days=42. op=rename dùng "
        "id/title. op=reschedule dùng id "
        "và hour/minute cho báo thức hoặc at cho nhắc việc; giữ nguyên lịch lặp. op=repeat dùng "
        "id/repeat_days (0 là một lần). op=skip bỏ qua lần tới của lịch lặp, không tắt các lần "
        "sau. "
        "op=delete/enable/disable "
        "dùng id; dismiss xác nhận thông báo; snooze nhắc lại sau 5 phút. Khi đổi/xóa/bỏ qua, "
        "trước "
        "tiên gọi status, chọn đúng lịch rồi mới gọi thao tác.",
        PropertyList({Property("op", kPropertyTypeString),
                      Property("kind", kPropertyTypeString, std::string("alarm")),
                      Property("title", kPropertyTypeString, std::string{}),
                      Property("at", kPropertyTypeString, std::string{}),
                      Property("hour", kPropertyTypeInteger, 7, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 0, 59),
                      Property("days", kPropertyTypeInteger, 0, 0, 127),
                      Property("repeat_days", kPropertyTypeInteger, -1, -1, 127),
                      Property("id", kPropertyTypeInteger, 1, 1, 8)}),
        [&controller](const PropertyList& p) -> ToolResult {
            auto* j = cJSON_CreateObject();
            for (const char* key : {"op", "kind", "title", "at"})
                cJSON_AddStringToObject(j, key, p[key].value<std::string>().c_str());
            for (const char* key : {"hour", "minute", "id"})
                cJSON_AddNumberToObject(j, key, p[key].value<int>());
            const auto op = p["op"].value<std::string>();
            cJSON_AddNumberToObject(
                j, "days", op == "repeat" ? p["repeat_days"].value<int>() : p["days"].value<int>());
            auto result = controller.TimeService().Command(j);
            cJSON_Delete(j);
            return result;
        });
    mcp.AddTool(
        "self.time.timer",
        "Đếm ngược/Pomodoro trên TFT. op=countdown dùng seconds 1-86400; op=pomodoro mặc định "
        "25 phút tập trung/5 phút nghỉ, nghỉ cuối 15 phút, 4 vòng. title là tên việc. "
        "op=pause/resume/stop/status; hide/show ẩn/hiện TFT nhưng vẫn đếm; dismiss tắt thông báo. "
        "Một phiên tại một thời điểm, phiên không tiếp tục sau mất nguồn.",
        PropertyList({Property("op", kPropertyTypeString),
                      Property("title", kPropertyTypeString, std::string{}),
                      Property("seconds", kPropertyTypeInteger, 300, 1, 86400),
                      Property("focus_minutes", kPropertyTypeInteger, 25, 1, 180),
                      Property("break_minutes", kPropertyTypeInteger, 5, 1, 60),
                      Property("long_break_minutes", kPropertyTypeInteger, 15, 1, 120),
                      Property("rounds", kPropertyTypeInteger, 4, 1, 12)}),
        [&controller](const PropertyList& p) -> ToolResult {
            auto* j = cJSON_CreateObject();
            for (const char* key : {"op", "title"})
                cJSON_AddStringToObject(j, key, p[key].value<std::string>().c_str());
            for (const char* key :
                 {"seconds", "focus_minutes", "break_minutes", "long_break_minutes", "rounds"})
                cJSON_AddNumberToObject(j, key, p[key].value<int>());
            auto result = controller.TimeService().Command(j);
            cJSON_Delete(j);
            return result;
        });

    // Preserve existing MCP tool names and signed-unit API for callers.
    mcp.AddUserOnlyTool(
        "move_motor",
        "Chạy hai motor DC tiến/lùi theo số đơn vị steps có dấu; mỗi đơn vị chạy delay_ms "
        "mili giây. Dùng khi người dùng yêu cầu robot di chuyển tiến hoặc lùi theo quãng ngắn.",
        PropertyList(std::vector<Property>{Property("steps", kPropertyTypeInteger, 0,
                                                    -RobotDrive::kMaxSteps, RobotDrive::kMaxSteps),
                                           Property("delay_ms", kPropertyTypeInteger, 0, 0, 100)}),
        [&controller](const PropertyList& properties) {
            return MoveMotor(controller, properties);
        });

    mcp.AddUserOnlyTool("stop_motor", "Dừng ngay cả hai motor DC.", PropertyList(),
                        [&controller](const PropertyList& properties) {
                            return StopMotor(controller, properties);
                        });

    mcp.AddUserOnlyTool(
        "set_motor_speed",
        "Đặt thời lượng mỗi đơn vị điều khiển motor bằng mili giây; đây không phải tốc độ PWM.",
        PropertyList(std::vector<Property>{Property("delay_ms", kPropertyTypeInteger, 1, 100)}),
        [&controller](const PropertyList& properties) {
            return SetMotorDurationUnit(controller, properties);
        });

    // Register AI interaction tools
    mcp.AddTool(
        "self.web_chat.consume_pending",
        "Khi văn bản kích hoạt ngắn chính xác là 'web_chat', gọi công cụ này trước khi trả lời. "
        "Kết quả là nội dung thật người dùng gửi từ trang web robot. Hãy trả lời nội dung đó, "
        "không trả lời từ khóa kích hoạt. Chỉ gọi khi có tin nhắn web chat đang chờ.",
        PropertyList(), [&controller](const PropertyList&) -> ToolResult {
            auto result = controller.ConsumeWebPrompt();
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::move(*result);
        });

    mcp.AddUserOnlyTool("toggle_chat", "Bật/tắt trạng thái trò chuyện của robot.", PropertyList(),
                        ToggleChat);

    mcp.AddUserOnlyTool("start_listening",
                        "Bắt đầu nghe lệnh thoại khi người dùng yêu cầu robot lắng nghe.",
                        PropertyList(), StartListening);

    mcp.AddUserOnlyTool("stop_listening",
                        "Dừng nghe lệnh thoại khi người dùng yêu cầu kết thúc chế độ nghe.",
                        PropertyList(), StopListening);

    // AI-visible robot tools. Keep these separate from the legacy user-only
    // motor tools so the tenclass.net MCP session can call them from natural
    // language requests.
    Property direction("direction", kPropertyTypeString);
    direction.SetMaxLength(16);
    mcp.AddTool(
        "self.robot.move",
        "Điều khiển robot hai bánh di chuyển trong thời gian ngắn khi người dùng nói tiến, "
        "lùi, rẽ trái hoặc rẽ phải. direction chỉ nhận forward, backward, left, right; "
        "duration_ms là thời gian chạy tính bằng mili giây. Dùng self.robot.stop để dừng sớm.",
        PropertyList({direction, Property("duration_ms", kPropertyTypeInteger, 800, 100,
                                          RobotDrive::kMaxRunTimeMs)}),
        [&controller](const PropertyList& properties) {
            return RobotMove(controller, properties);
        });
    mcp.AddTool("self.robot.stop",
                "Dừng ngay mọi chuyển động của robot; dùng khi người dùng nói dừng/đứng lại.",
                PropertyList(), [&controller](const PropertyList& properties) {
                    return StopRobot(controller, properties);
                });
    mcp.AddTool("self.robot.be_lively",
                "Cho robot chạy chuyển động khám phá hiếu động lặp liên tục: tiến, lùi và xoay. "
                "Dùng khi người dùng yêu cầu robot đi khám phá/chạy hiếu động; chỉ dừng khi gọi "
                "self.robot.stop hoặc người dùng yêu cầu dừng.",
                PropertyList(), [&controller](const PropertyList& properties) {
                    return StartLivelyRoutine(controller, properties);
                });
    mcp.AddTool("self.robot.dance",
                "Cho robot thực hiện chuỗi chuyển động nhảy nhanh, vui nhộn khi người dùng yêu cầu "
                "nhảy/múa.",
                PropertyList(), [&controller](const PropertyList& properties) {
                    return StartDanceRoutine(controller, properties);
                });

    Property emotion("emotion", kPropertyTypeString);
    emotion.SetMaxLength(32);
    mcp.AddTool(
        "self.robot.set_emotion",
        "Đổi biểu cảm khuôn mặt robot trên TFT theo yêu cầu như vui, cười, ngạc nhiên, buồn, "
        "giận, buồn ngủ, bối rối, yêu thích, chóng mặt, mất tập trung, tự tin, ngáp hoặc tinh "
        "nghịch. "
        "emotion phải là một mã hỗ trợ: neutral, happy, laugh, surprised, sad, angry, sleepy, "
        "confused, love, dizzy, distracted, devil, confident, yawn, playful, listening, speaking. "
        "Không dùng công cụ này để đổi trạng thái hội thoại; listening/speaking chỉ đổi khuôn mặt.",
        PropertyList({emotion}), [&controller](const PropertyList& properties) {
            return SetRobotEmotion(controller, properties);
        });

    Property oled_mode("mode", kPropertyTypeString);
    oled_mode.SetMaxLength(16);
    mcp.AddTool(
        "self.robot.set_oled_mode",
        "Chọn chế độ màn hình OLED phụ: đồng hồ (clock), chữ chạy (marquee), trạng thái robot "
        "(status) hoặc tắt màn (off). mode phải là một trong bốn giá trị này.",
        PropertyList({oled_mode}), [&controller](const PropertyList& properties) {
            return SetRobotOledMode(controller, properties);
        });
    mcp.AddTool(
        "self.robot.set_oled_marquee_speed",
        "Đặt và lưu tốc độ chữ chạy OLED phụ: 1 chậm, 2 vừa, 3 nhanh, 4 rất nhanh, 5 cực nhanh. "
        "Áp dụng ngay nếu OLED đang ở chế độ chữ chạy.",
        PropertyList({Property("speed", kPropertyTypeInteger, 2, 1, 5)}),
        [&controller](const PropertyList& properties) {
            return SetRobotOledMarqueeSpeed(controller, properties);
        });
    mcp.AddTool("self.robot.show_ip",
                "Hiển thị địa chỉ IP Wi-Fi nội bộ của robot và trả IP để thông báo cho người dùng. "
                "Dùng khi người dùng hỏi IP/địa chỉ webserver; cần robot đã kết nối Wi-Fi.",
                PropertyList(), [&controller](const PropertyList& properties) {
                    return ShowRobotIp(controller, properties);
                });
    Property oled_text("text", kPropertyTypeString);
    oled_text.SetMaxLength(240);
    mcp.AddTool(
        "self.robot.show_oled_text",
        "Hiển thị câu chữ do người dùng yêu cầu trên OLED phụ dưới dạng marquee tạm thời; "
        "không thay đổi nội dung đã lưu. duration_ms từ 1.000 đến 30.000 ms; speed từ 1 "
        "(chậm) đến 5 (cực nhanh), mặc định 2.",
        PropertyList(
            {oled_text, Property("duration_ms", kPropertyTypeInteger, 10000, 1000, 30000),
             Property("speed", kPropertyTypeInteger, 2, 1, 5)}),
        [&controller](const PropertyList& properties) {
            return ShowRobotOledText(controller, properties);
        });
    mcp.AddTool(
        "self.robot.show_payment_qr",
        "Hiển thị mã QR thanh toán ngân hàng đã cấu hình sẵn trên TFT để khách quét. "
        "Chỉ dùng khi người dùng yêu cầu hiện QR thanh toán; không tự tạo/sửa thông tin ngân hàng.",
        PropertyList({Property("duration_ms", kPropertyTypeInteger, 30000, 5000, 120000)}),
        [&controller](const PropertyList& properties) {
            return ShowRobotPaymentQr(controller, properties);
        });
    mcp.AddTool(
        "self.robot.get_status",
        "Kiểm tra tình trạng robot khi người dùng hỏi. Trả về Wi-Fi/IP, chuyển động, kết nối "
        "WSS bridge tới MCP server và trạng thái microSD. mcp_server_connected=true nghĩa là "
        "robot đang giữ kết nối bridge tới server. sd_card_mounted=true nghĩa là thẻ đã mount "
        "và dùng được; nếu false hãy đọc sd_card_state/message. Phần cứng không có chân "
        "card-detect nên không thể khẳng định thẻ vật lý đã cắm khi trạng thái chưa mount.",
        PropertyList(), [&controller](const PropertyList& properties) {
            return GetRobotStatus(controller, properties);
        });

    mcp.AddTool(
        "self.robot.dynamic_main_screen.status",
        "Chỉ gọi khi người dùng hỏi về Màn hình chính động (Dynamic Main Screen) trên TFT. "
        "Trả cấu hình bật/tắt, thứ tự ngẫu nhiên hay tuần tự, chu kỳ và các thẻ đang được chọn.",
        PropertyList(),
        [&controller](const PropertyList&) { return GetInfoShuttleStatus(controller); });
    mcp.AddTool("self.robot.dynamic_main_screen.set_enabled",
                "Bật hoặc tắt Màn hình chính động (Dynamic Main Screen) khi người dùng yêu cầu. "
                "enabled=true để bật, false để tắt; giữ nguyên các thẻ và chu kỳ đã cấu hình.",
                PropertyList({Property("enabled", kPropertyTypeBoolean)}),
                [&controller](const PropertyList& properties) {
                    return SetInfoShuttleEnabled(controller, properties);
                });
    mcp.AddTool(
        "self.robot.dynamic_main_screen.configure",
        "Cấu hình các thẻ và chu kỳ của Màn hình chính động (Dynamic Main Screen) khi người dùng "
        "nói rõ muốn chọn thẻ nào, đổi ngẫu nhiên/tuần tự hoặc đổi khoảng thời gian. random=true "
        "là "
        "ngẫu nhiên; false là khuôn mặt -> lịch -> thời tiết. interval_seconds từ 5 đến 120. Bật "
        "ít "
        "nhất một trong show_face/show_schedule/show_weather. Khi chỉ đổi một tùy chọn, hãy đọc "
        "status trước và giữ nguyên các giá trị còn lại. Trạng thái bật/tắt hiện tại được giữ "
        "nguyên.",
        PropertyList({Property("random", kPropertyTypeBoolean),
                      Property("interval_seconds", kPropertyTypeInteger, 5, 120),
                      Property("show_face", kPropertyTypeBoolean),
                      Property("show_schedule", kPropertyTypeBoolean),
                      Property("show_weather", kPropertyTypeBoolean)}),
        [&controller](const PropertyList& properties) {
            return ConfigureInfoShuttle(controller, properties);
        });
    mcp.AddTool(
        "self.robot.dynamic_main_screen.next",
        "Chuyển ngay sang thẻ tiếp theo trong các thẻ đã bật của Màn hình chính động. Chỉ gọi khi "
        "người dùng yêu cầu đổi/bỏ qua thẻ; không dùng để che radio, nhạc, TV, đồng hồ hoặc màn "
        "hình "
        "ưu tiên khác. Nếu tính năng đang tắt, yêu cầu bật trước.",
        PropertyList(),
        [&controller](const PropertyList&) { return AdvanceInfoShuttle(controller); });

    mcp.AddTool(
        "self.network.ip2qrcode",
        "Hiển thị QR tạm thời để mở webserver nội bộ hiện tại của robot. Dùng khi người dùng "
        "yêu cầu QR truy cập robot/webserver; cần Wi-Fi và không làm thay đổi QR ngân hàng đã lưu.",
        PropertyList({Property("duration_ms", kPropertyTypeInteger, 30000, 5000, 120000)}),
        [&controller](const PropertyList& properties) -> ToolResult {
            if (!controller.ShowIpQr(properties["duration_ms"].value<int>()))
                return std::unexpected("Cannot show IP QR; check Wi-Fi");
            return "Webserver IP QR display scheduled";
        });
    auto qr_payload = Property("payload", kPropertyTypeString);
    qr_payload.SetMaxLength(256);
    auto qr_caption = Property("caption", kPropertyTypeString, std::string{});
    qr_caption.SetMaxLength(96);
    mcp.AddTool(
        "self.robot.show_qr",
        "Hiển thị QR tạm thời từ URL hoặc văn bản payload do người dùng cung cấp; không lưu lại. "
        "Chỉ dùng khi người dùng yêu cầu tạo QR cho nội dung đã cho. Với QR thanh toán ngân hàng, "
        "luôn dùng self.robot.show_payment_qr và không tự suy đoán thông tin tài khoản.",
        PropertyList({qr_payload, qr_caption,
                      Property("duration_ms", kPropertyTypeInteger, 30000, 5000, 120000)}),
        [&controller](const PropertyList& properties) -> ToolResult {
            if (!controller.ShowQr(properties["payload"].value<std::string>(),
                                   properties["caption"].value<std::string>(),
                                   properties["duration_ms"].value<int>()))
                return std::unexpected("Invalid QR payload or caption (max 256/96 UTF-8 bytes)");
            return "Temporary QR display scheduled";
        });

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    if (controller.Bluetooth().GetStatus().enabled) {
        mcp.AddTool(
            "self.robot.bluetooth.status",
            "Chỉ dùng khi người dùng hỏi rõ trạng thái Bluetooth; không gọi để phát nhạc, radio, TV hoặc SD. "
            "Lấy trạng thái kết nối loa Bluetooth và danh sách thiết bị vừa quét trên robot. Dùng trước "
            "khi yêu cầu kết nối theo tên để xác nhận loa duy nhất.",
            PropertyList(), [&controller](const PropertyList& properties) {
                return BluetoothStatusTool(controller, properties);
            });
        mcp.AddTool(
            "self.robot.bluetooth.scan",
            "Chỉ quét khi người dùng yêu cầu tìm, chọn hoặc đổi loa Bluetooth; không quét để bắt đầu phát media. "
            "Quét loa Bluetooth xung quanh bằng mạch D1 mở rộng. Quét mới sẽ ngắt loa đang kết nối; "
            "gọi self.robot.bluetooth.status để xem danh sách sau khi quét xong.",
            PropertyList(), [&controller](const PropertyList& properties) {
                return BluetoothScanTool(controller, properties);
            });
        Property bluetooth_name("name", kPropertyTypeString);
        bluetooth_name.SetMaxLength(64);
        mcp.AddTool(
            "self.robot.bluetooth.connect",
            "Chỉ kết nối khi người dùng yêu cầu rõ kết nối/chọn/đổi loa Bluetooth; không hỏi chọn loa khi phát media. "
            "Kết nối loa Bluetooth có tên khớp với name từ kết quả quét gần nhất. Tên phải khớp duy "
            "nhất; nếu có nhiều loa trùng tên, dùng status để phân biệt và không tự chọn ngẫu nhiên. "
            "D1 sẽ dừng luồng, ngắt loa cũ rồi mới kết nối loa mới.",
            PropertyList({bluetooth_name}), [&controller](const PropertyList& properties) {
                return BluetoothConnectTool(controller, properties);
            });
        mcp.AddTool(
            "self.robot.bluetooth.disconnect",
            "Chỉ ngắt khi người dùng yêu cầu rõ. Không ngắt hoặc đổi loa khi phát media. "
            "Ngắt kết nối loa Bluetooth hiện tại qua D1 Mini.",
            PropertyList(), [&controller](const PropertyList& properties) {
                return BluetoothDisconnectTool(controller, properties);
            });
    }
#endif

    ESP_LOGI(TAG, "Robot MCP tools registered");
}
