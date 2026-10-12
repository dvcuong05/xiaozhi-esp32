#include "robot_mcp_tools.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstdint>
#include <mutex>
#include <utility>

static RobotDrive* g_drive = nullptr;

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

void RegisterRobotMcpTools(RobotDrive* drive) {
    g_drive = drive;

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

    ESP_LOGI(TAG, "Robot MCP tools registered");
}
