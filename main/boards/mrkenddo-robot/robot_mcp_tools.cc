#include "robot_mcp_tools.h"
#include "application.h"
#include "mcp_server.h"
#include "robot_controller.h"
#include "robot_drive.h"

#include <esp_log.h>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include <wifi_manager.h>

static const char* TAG = "RobotMCP";

namespace {

// Legacy signed steps select forward/backward; each unit lasts delay_ms milliseconds.
static ToolResult MoveMotor(mrkenddo::RobotController& controller, const PropertyList& properties) {
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

static ToolResult StopMotor(mrkenddo::RobotController& controller, const PropertyList&) {
    ESP_LOGI(TAG, "Stopping DC motors");
    controller.Stop();

    return std::string("Motor stopped");
}

static ToolResult SetMotorDurationUnit(mrkenddo::RobotController& controller,
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

static ToolResult RobotMove(mrkenddo::RobotController& controller, const PropertyList& properties) {
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

static ToolResult StartLivelyRoutine(mrkenddo::RobotController& controller, const PropertyList&) {
    if (!controller.StartRoutine(RobotDrive::Routine::Lively)) {
        return std::unexpected("Robot is busy or the movement task is unavailable");
    }
    return "Lively routine started";
}

static ToolResult StartDanceRoutine(mrkenddo::RobotController& controller, const PropertyList&) {
    if (!controller.StartRoutine(RobotDrive::Routine::Dance)) {
        return std::unexpected("Robot is busy or the movement task is unavailable");
    }
    return "Dance routine started";
}

static ToolResult StopRobot(mrkenddo::RobotController& controller, const PropertyList&) {
    controller.Stop();
    return "Robot stopped";
}

static ToolResult SetRobotEmotion(mrkenddo::RobotController& controller,
                                  const PropertyList& properties) {
    const std::string emotion = properties["emotion"].value<std::string>();
    controller.SetEmotion(emotion);
    return "Robot emotion set to " + emotion;
}

static ToolResult SetRobotOledMode(mrkenddo::RobotController& controller,
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

static ToolResult ShowRobotIp(mrkenddo::RobotController& controller, const PropertyList&) {
    const std::string ip = controller.ShowIp();
    if (ip.empty()) {
        return std::unexpected("Wi-Fi is not connected");
    }
    return "Robot local IP address is " + ip;
}

static ToolResult ShowRobotOledText(mrkenddo::RobotController& controller,
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

static ToolResult ShowRobotPaymentQr(mrkenddo::RobotController& controller,
                                     const PropertyList& properties) {
    const int duration_ms = properties["duration_ms"].value<int>();
    if (!controller.ShowPaymentQr(duration_ms)) {
        return std::unexpected("Could not display the payment QR on the TFT");
    }
    return "Payment QR displayed on the TFT";
}

static ToolResult GetRobotStatus(mrkenddo::RobotController& controller, const PropertyList&) {
    const auto& wifi = WifiManager::GetInstance();
    const bool connected = wifi.IsConnected();
    const std::string ip = connected ? wifi.GetIpAddress() : "";
    const bool moving = controller.IsRoutineRunning();
    return "{\"wifi_connected\":" + std::string(connected ? "true" : "false") + ",\"ip\":\"" + ip +
           "\",\"routine_running\":" + std::string(moving ? "true" : "false") + "}";
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

void RegisterRobotMcpTools(mrkenddo::RobotController& controller) {
    auto& mcp = McpServer::GetInstance();

    // Preserve existing MCP tool names and signed-unit API for callers.
    mcp.AddUserOnlyTool(
        "move_motor", "Run both DC motors forward or backward for units * delay_ms milliseconds",
        PropertyList(std::vector<Property>{Property("steps", kPropertyTypeInteger, 0,
                                                    -RobotDrive::kMaxSteps, RobotDrive::kMaxSteps),
                                           Property("delay_ms", kPropertyTypeInteger, 0, 0, 100)}),
        [&controller](const PropertyList& properties) {
            return MoveMotor(controller, properties);
        });

    mcp.AddUserOnlyTool("stop_motor", "Stop both DC motors", PropertyList(),
                        [&controller](const PropertyList& properties) {
                            return StopMotor(controller, properties);
                        });

    mcp.AddUserOnlyTool(
        "set_motor_speed", "Set milliseconds per motor movement unit (not PWM speed)",
        PropertyList(std::vector<Property>{Property("delay_ms", kPropertyTypeInteger, 1, 100)}),
        [&controller](const PropertyList& properties) {
            return SetMotorDurationUnit(controller, properties);
        });

    // Register AI interaction tools
    mcp.AddTool(
        "self.web_chat.consume_pending",
        "When the short wake/detect text is exactly 'web_chat', call this tool before replying. "
        "The tool result is the user's real message from the robot web page. Answer that "
        "message, not the trigger word. Only call this for a pending web chat message.",
        PropertyList(), [&controller](const PropertyList&) -> ToolResult {
            auto result = controller.ConsumeWebPrompt();
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::move(*result);
        });

    mcp.AddUserOnlyTool("toggle_chat", "Toggle the chat state (start/stop conversation)",
                        PropertyList(), ToggleChat);

    mcp.AddUserOnlyTool("start_listening", "Start voice listening", PropertyList(), StartListening);

    mcp.AddUserOnlyTool("stop_listening", "Stop voice listening", PropertyList(), StopListening);

    // AI-visible robot tools. Keep these separate from the legacy user-only
    // motor tools so the tenclass.net MCP session can call them from natural
    // language requests.
    Property direction("direction", kPropertyTypeString);
    direction.SetMaxLength(16);
    mcp.AddTool("self.robot.move",
                "Move the two-wheel robot for a short duration. Direction: forward, backward, "
                "left, or right.",
                PropertyList({direction, Property("duration_ms", kPropertyTypeInteger, 800, 100,
                                                  RobotDrive::kMaxRunTimeMs)}),
                [&controller](const PropertyList& properties) {
                    return RobotMove(controller, properties);
                });
    mcp.AddTool("self.robot.stop", "Stop all robot motor movement immediately", PropertyList(),
                [&controller](const PropertyList& properties) {
                    return StopRobot(controller, properties);
                });
    mcp.AddTool("self.robot.be_lively",
                "Run a playful robot routine continuously with forward, backward, and turning "
                "movements until stopped",
                PropertyList(), [&controller](const PropertyList& properties) {
                    return StartLivelyRoutine(controller, properties);
                });
    mcp.AddTool("self.robot.dance", "Run a faster, energetic dancing routine", PropertyList(),
                [&controller](const PropertyList& properties) {
                    return StartDanceRoutine(controller, properties);
                });

    Property emotion("emotion", kPropertyTypeString);
    emotion.SetMaxLength(32);
    mcp.AddTool("self.robot.set_emotion",
                "Set the custom TFT robot eyes emotion. Examples: neutral, happy, surprised, "
                "sad, angry, sleepy, confused, listening, speaking.",
                PropertyList({emotion}), [&controller](const PropertyList& properties) {
                    return SetRobotEmotion(controller, properties);
                });

    Property oled_mode("mode", kPropertyTypeString);
    oled_mode.SetMaxLength(16);
    mcp.AddTool("self.robot.set_oled_mode",
                "Set the secondary OLED mode: clock, marquee, status, or off",
                PropertyList({oled_mode}), [&controller](const PropertyList& properties) {
                    return SetRobotOledMode(controller, properties);
                });
    mcp.AddTool("self.robot.show_ip",
                "Show the local Wi-Fi IP on the robot display and return it so the assistant can "
                "tell the user",
                PropertyList(), [&controller](const PropertyList& properties) {
                    return ShowRobotIp(controller, properties);
                });
    Property oled_text("text", kPropertyTypeString);
    oled_text.SetMaxLength(240);
    mcp.AddTool("self.robot.show_oled_text",
                "Show a custom sentence on the secondary OLED as scrolling text without changing "
                "the saved OLED content",
                PropertyList(
                    {oled_text, Property("duration_ms", kPropertyTypeInteger, 10000, 1000, 30000)}),
                [&controller](const PropertyList& properties) {
                    return ShowRobotOledText(controller, properties);
                });
    mcp.AddTool("self.robot.show_payment_qr",
                "Show the configured bank payment QR code on the TFT for scanning",
                PropertyList({Property("duration_ms", kPropertyTypeInteger, 30000, 5000, 120000)}),
                [&controller](const PropertyList& properties) {
                    return ShowRobotPaymentQr(controller, properties);
                });
    mcp.AddTool("self.robot.get_status", "Get robot Wi-Fi, IP, and movement status", PropertyList(),
                [&controller](const PropertyList& properties) {
                    return GetRobotStatus(controller, properties);
                });

    mcp.AddTool("self.network.ip2qrcode",
                "Show a QR code opening this robot's current local webserver. Requires Wi-Fi; does "
                "not change the saved bank QR.",
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
        "Show a temporary QR from supplied URL/text. Does not save it. For bank payment always use "
        "self.robot.show_payment_qr; never invent bank details.",
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
