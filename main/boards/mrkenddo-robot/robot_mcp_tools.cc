#include "robot_mcp_tools.h"
#include <esp_log.h>

static RobotDrive* g_drive = nullptr;

static const char* TAG = "RobotMCP";

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

// MCP Tool: Send prompt to AI
// Parameters: prompt (string, required)
static ToolResult SendPrompt(const PropertyList& properties) {
    std::string prompt = properties["prompt"].value<std::string>();

    if (!Application::GetInstance().QueueTextPrompt(std::move(prompt))) {
        return std::unexpected("Device is busy or unavailable");
    }
    return std::string("Prompt queued");
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
    mcp.AddUserOnlyTool("send_prompt", "Send a text prompt to the AI assistant",
                        PropertyList(std::vector<Property>{
                            Property("prompt", kPropertyTypeString).SetMaxLength(1000)}),
                        SendPrompt);

    mcp.AddUserOnlyTool("toggle_chat", "Toggle the chat state (start/stop conversation)",
                        PropertyList(), ToggleChat);

    mcp.AddUserOnlyTool("start_listening", "Start voice listening", PropertyList(), StartListening);

    mcp.AddUserOnlyTool("stop_listening", "Stop voice listening", PropertyList(), StopListening);

    ESP_LOGI(TAG, "Robot MCP tools registered");
}
