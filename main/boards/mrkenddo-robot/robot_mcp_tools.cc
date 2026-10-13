#include "robot_mcp_tools.h"
#include "board.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cstdint>
#include <mutex>
#include <utility>

#include <wifi_manager.h>

static RobotDrive* g_drive = nullptr;
static std::function<bool(int)> g_set_oled_mode;
static std::function<std::string()> g_show_ip;
static std::function<bool(std::string, int)> g_show_oled_text;
static std::function<bool(int)> g_show_payment_qr;

static const char* TAG = "RobotMCP";

namespace {

enum class WebPromptStatus { Idle, Queued, TriggerSent, Consumed, Failed, Timeout };

std::mutex web_prompt_mutex;
std::string pending_web_prompt;
WebPromptStatus web_prompt_status = WebPromptStatus::Idle;
int64_t web_prompt_started_us = 0;
uint32_t web_prompt_id = 0;

void ExpireWebPrompt() {
    if ((web_prompt_status == WebPromptStatus::Queued ||
         web_prompt_status == WebPromptStatus::TriggerSent) &&
        esp_timer_get_time() - web_prompt_started_us > 30000000) {
        pending_web_prompt.clear();
        web_prompt_status = WebPromptStatus::Timeout;
    }
}

ToolResult ConsumeWebPrompt(const PropertyList&) {
    std::lock_guard<std::mutex> lock(web_prompt_mutex);
    ExpireWebPrompt();
    if (pending_web_prompt.empty()) {
        return std::unexpected("No pending Web UI prompt");
    }
    std::string prompt = std::move(pending_web_prompt);
    pending_web_prompt.clear();
    web_prompt_status = WebPromptStatus::Consumed;
    ESP_LOGI(TAG, "Web prompt consumed by assistant tool (%u bytes)",
             static_cast<unsigned>(prompt.size()));
    return prompt;
}

}  // namespace

bool QueueRobotWebPrompt(std::string prompt) {
    uint32_t id;
    {
        std::lock_guard<std::mutex> lock(web_prompt_mutex);
        ExpireWebPrompt();
        if (!pending_web_prompt.empty()) {
            return false;
        }
        pending_web_prompt = std::move(prompt);
        web_prompt_status = WebPromptStatus::Queued;
        web_prompt_started_us = esp_timer_get_time();
        id = ++web_prompt_id;
    }
    if (!Application::GetInstance().StartTextBridgeConversation([id](bool ready) {
            std::lock_guard<std::mutex> lock(web_prompt_mutex);
            if (id != web_prompt_id || web_prompt_status != WebPromptStatus::Queued) {
                return;
            }
            web_prompt_status = ready ? WebPromptStatus::TriggerSent : WebPromptStatus::Failed;
            if (!ready) {
                pending_web_prompt.clear();
            }
        })) {
        std::lock_guard<std::mutex> lock(web_prompt_mutex);
        if (id == web_prompt_id) {
            pending_web_prompt.clear();
            web_prompt_status = WebPromptStatus::Failed;
        }
        return false;
    }
    return true;
}

const char* GetRobotWebPromptStatus() {
    std::lock_guard<std::mutex> lock(web_prompt_mutex);
    ExpireWebPrompt();
    switch (web_prompt_status) {
        case WebPromptStatus::Idle:
            return "idle";
        case WebPromptStatus::Queued:
            return "queued";
        case WebPromptStatus::TriggerSent:
            return "trigger_sent";
        case WebPromptStatus::Consumed:
            return "consumed";
        case WebPromptStatus::Failed:
            return "failed";
        case WebPromptStatus::Timeout:
            return "timeout";
    }
    return "failed";
}

// Legacy signed steps select forward/backward; each unit lasts delay_ms milliseconds.
static ToolResult MoveMotor(const PropertyList& properties) {
    if (g_drive == nullptr) {
        return std::unexpected("Motor drive not initialized");
    }

    int steps = properties["steps"].value<int>();
    int delay_ms = 0;

    if (properties["delay_ms"].has_default_value()) {
        delay_ms = properties["delay_ms"].value<int>();
    }

    ESP_LOGI(TAG, "Moving DC motors: units=%d, delay=%dms", steps, delay_ms);
    if (!g_drive->Step(steps, delay_ms)) {
        return std::unexpected("Motor command failed");
    }
    return std::string("Motor command started");
}

static ToolResult StopMotor(const PropertyList& properties) {
    if (g_drive == nullptr) {
        return std::unexpected("Motor drive not initialized");
    }

    ESP_LOGI(TAG, "Stopping DC motors");
    g_drive->Stop();

    return std::string("Motor stopped");
}

static ToolResult SetMotorDurationUnit(const PropertyList& properties) {
    if (g_drive == nullptr) {
        return std::unexpected("Motor drive not initialized");
    }

    int delay_ms = properties["delay_ms"].value<int>();

    ESP_LOGI(TAG, "Setting motor duration unit: %dms", delay_ms);
    g_drive->SetSpeed(delay_ms);

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

static ToolResult RobotMove(const PropertyList& properties) {
    if (g_drive == nullptr) {
        return std::unexpected("Motor drive not initialized");
    }
    auto direction = ParseDirection(properties["direction"].value<std::string>());
    if (!direction) {
        return std::unexpected(direction.error());
    }
    const int duration_ms = properties["duration_ms"].value<int>();
    if (!g_drive->Move(*direction, duration_ms, 1)) {
        return std::unexpected("Robot movement could not be started");
    }
    return "Robot moving for " + std::to_string(duration_ms) + " ms";
}

static ToolResult StartLivelyRoutine(const PropertyList&) {
    if (g_drive == nullptr || !g_drive->StartRoutine(RobotDrive::Routine::Lively)) {
        return std::unexpected("Robot is busy or the movement task is unavailable");
    }
    return "Lively routine started";
}

static ToolResult StartDanceRoutine(const PropertyList&) {
    if (g_drive == nullptr || !g_drive->StartRoutine(RobotDrive::Routine::Dance)) {
        return std::unexpected("Robot is busy or the movement task is unavailable");
    }
    return "Dance routine started";
}

static ToolResult StopRobot(const PropertyList&) {
    if (g_drive == nullptr) {
        return std::unexpected("Motor drive not initialized");
    }
    g_drive->Stop();
    return "Robot stopped";
}

static ToolResult SetRobotEmotion(const PropertyList& properties) {
    auto* display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return std::unexpected("Robot display is unavailable");
    }
    const std::string emotion = properties["emotion"].value<std::string>();
    display->SetEmotion(emotion.c_str());
    return "Robot emotion set to " + emotion;
}

static ToolResult SetRobotOledMode(const PropertyList& properties) {
    if (!g_set_oled_mode) {
        return std::unexpected("Secondary OLED control is unavailable");
    }
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
    if (!g_set_oled_mode(mode_value)) {
        return std::unexpected("Could not apply the secondary OLED mode");
    }
    return "Secondary OLED mode set to " + mode;
}

static ToolResult ShowRobotIp(const PropertyList&) {
    if (!g_show_ip) {
        return std::unexpected("IP display is unavailable");
    }
    const std::string ip = g_show_ip();
    if (ip.empty()) {
        return std::unexpected("Wi-Fi is not connected");
    }
    return "Robot local IP address is " + ip;
}

static ToolResult ShowRobotOledText(const PropertyList& properties) {
    if (!g_show_oled_text) {
        return std::unexpected("Secondary OLED text control is unavailable");
    }
    const std::string text = properties["text"].value<std::string>();
    const int duration_ms = properties["duration_ms"].value<int>();
    if (text.empty()) {
        return std::unexpected("OLED text must not be empty");
    }
    if (!g_show_oled_text(text, duration_ms)) {
        return std::unexpected("Could not display text on the secondary OLED");
    }
    return "Custom OLED text display started";
}

static ToolResult ShowRobotPaymentQr(const PropertyList& properties) {
    if (!g_show_payment_qr) {
        return std::unexpected("Payment QR display is unavailable");
    }
    const int duration_ms = properties["duration_ms"].value<int>();
    if (!g_show_payment_qr(duration_ms)) {
        return std::unexpected("Could not display the payment QR on the TFT");
    }
    return "Payment QR displayed on the TFT";
}

static ToolResult GetRobotStatus(const PropertyList&) {
    const auto& wifi = WifiManager::GetInstance();
    const bool connected = wifi.IsConnected();
    const std::string ip = connected ? wifi.GetIpAddress() : "";
    const bool moving = g_drive != nullptr && g_drive->IsRoutineRunning();
    return "{\"wifi_connected\":" + std::string(connected ? "true" : "false") + ",\"ip\":\"" + ip +
           "\",\"routine_running\":" + std::string(moving ? "true" : "false") + "}";
}

// MCP Tool: Toggle chat state
static ToolResult ToggleChat(const PropertyList& properties) {
    ESP_LOGI(TAG, "Toggling chat state");
    auto& app = Application::GetInstance();
    app.ToggleChatState();
    return std::string("Chat state toggled");
}

// MCP Tool: Start listening
static ToolResult StartListening(const PropertyList& properties) {
    ESP_LOGI(TAG, "Starting listening");
    auto& app = Application::GetInstance();
    app.StartListening();
    return std::string("Listening started");
}

// MCP Tool: Stop listening
static ToolResult StopListening(const PropertyList& properties) {
    ESP_LOGI(TAG, "Stopping listening");
    auto& app = Application::GetInstance();
    app.StopListening();
    return std::string("Listening stopped");
}

void RegisterRobotMcpTools(RobotDrive* drive, std::function<bool(int)> set_oled_mode,
                           std::function<std::string()> show_ip,
                           std::function<bool(std::string, int)> show_oled_text,
                           std::function<bool(int)> show_payment_qr) {
    g_drive = drive;
    g_set_oled_mode = std::move(set_oled_mode);
    g_show_ip = std::move(show_ip);
    g_show_oled_text = std::move(show_oled_text);
    g_show_payment_qr = std::move(show_payment_qr);

    auto& mcp = McpServer::GetInstance();

    // Preserve existing MCP tool names and signed-unit API for callers.
    mcp.AddUserOnlyTool(
        "move_motor", "Run both DC motors forward or backward for units * delay_ms milliseconds",
        PropertyList(std::vector<Property>{Property("steps", kPropertyTypeInteger, 0,
                                                    -RobotDrive::kMaxSteps, RobotDrive::kMaxSteps),
                                           Property("delay_ms", kPropertyTypeInteger, 0, 0, 100)}),
        MoveMotor);

    mcp.AddUserOnlyTool("stop_motor", "Stop both DC motors", PropertyList(), StopMotor);

    mcp.AddUserOnlyTool(
        "set_motor_speed", "Set milliseconds per motor movement unit (not PWM speed)",
        PropertyList(std::vector<Property>{Property("delay_ms", kPropertyTypeInteger, 1, 100)}),
        SetMotorDurationUnit);

    // Register AI interaction tools
    mcp.AddTool(
        "self.web_chat.consume_pending",
        "When the short wake/detect text is exactly 'web_chat', call this tool before replying. "
        "The tool result is the user's real message from the robot web page. Answer that "
        "message, not the trigger word. Only call this for a pending web chat message.",
        PropertyList(), ConsumeWebPrompt);

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
                RobotMove);
    mcp.AddTool("self.robot.stop", "Stop all robot motor movement immediately", PropertyList(),
                StopRobot);
    mcp.AddTool("self.robot.be_lively",
                "Run a playful robot routine continuously with forward, backward, and turning "
                "movements until stopped",
                PropertyList(), StartLivelyRoutine);
    mcp.AddTool("self.robot.dance", "Run a faster, energetic dancing routine", PropertyList(),
                StartDanceRoutine);

    Property emotion("emotion", kPropertyTypeString);
    emotion.SetMaxLength(32);
    mcp.AddTool("self.robot.set_emotion",
                "Set the custom TFT robot eyes emotion. Examples: neutral, happy, surprised, "
                "sad, angry, sleepy, confused, listening, speaking.",
                PropertyList({emotion}), SetRobotEmotion);

    Property oled_mode("mode", kPropertyTypeString);
    oled_mode.SetMaxLength(16);
    mcp.AddTool("self.robot.set_oled_mode",
                "Set the secondary OLED mode: clock, marquee, status, or off",
                PropertyList({oled_mode}), SetRobotOledMode);
    mcp.AddTool("self.robot.show_ip",
                "Show the local Wi-Fi IP on the robot display and return it so the assistant can "
                "tell the user",
                PropertyList(), ShowRobotIp);
    Property oled_text("text", kPropertyTypeString);
    oled_text.SetMaxLength(240);
    mcp.AddTool("self.robot.show_oled_text",
                "Show a custom sentence on the secondary OLED as scrolling text without changing "
                "the saved OLED content",
                PropertyList(
                    {oled_text, Property("duration_ms", kPropertyTypeInteger, 10000, 1000, 30000)}),
                ShowRobotOledText);
    mcp.AddTool("self.robot.show_payment_qr",
                "Show the configured bank payment QR code on the TFT for scanning",
                PropertyList({Property("duration_ms", kPropertyTypeInteger, 30000, 5000, 120000)}),
                ShowRobotPaymentQr);
    mcp.AddTool("self.robot.get_status", "Get robot Wi-Fi, IP, and movement status", PropertyList(),
                GetRobotStatus);

    ESP_LOGI(TAG, "Robot MCP tools registered");
}
