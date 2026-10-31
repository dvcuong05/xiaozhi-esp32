#include "robot_mcp_tools.h"
#include "application.h"
#include "mcp_server.h"
#include "robot_controller.h"
#include "robot_drive.h"
#include "sd_storage.h"

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
    if (text.empty()) {
        return std::unexpected("OLED text must not be empty");
    }
    if (!controller.ShowOledText(text, duration_ms)) {
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
        "Hiển thị câu chữ do người dùng yêu cầu trên OLED phụ dưới dạng chữ chạy tạm thời; "
        "không thay đổi nội dung OLED đã lưu. Truyền nguyên văn nội dung vào text và thời lượng "
        "hiển thị vào duration_ms (1.000–30.000 ms).",
        PropertyList(
            {oled_text, Property("duration_ms", kPropertyTypeInteger, 10000, 1000, 30000)}),
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

    ESP_LOGI(TAG, "Robot MCP tools registered");
}
