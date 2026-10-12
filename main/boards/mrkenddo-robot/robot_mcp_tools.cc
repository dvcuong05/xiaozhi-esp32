#include "robot_mcp_tools.h"
#include <esp_log.h>

static RobotDrive* g_stepper_motor = nullptr;

static const char* TAG = "RobotMCP";

// MCP Tool: Move stepper motor
// Parameters: steps (int, required), delay_ms (int, optional, 0 = configured speed)
static ToolResult MoveStepperMotor(const PropertyList& properties) {
    if (g_stepper_motor == nullptr) {
        return std::unexpected("Stepper motor not initialized");
    }

    int steps = properties["steps"].value<int>();
    int delay_ms = 0;

    if (properties["delay_ms"].has_default_value()) {
        delay_ms = properties["delay_ms"].value<int>();
    }

    ESP_LOGI(TAG, "Moving stepper motor: steps=%d, delay=%dms", steps, delay_ms);
    if (!g_stepper_motor->Step(steps, delay_ms)) {
        return std::unexpected("Motor command failed");
    }
    return std::string("Motor command started");
}

// MCP Tool: Stop stepper motor
static ToolResult StopStepperMotor(const PropertyList& properties) {
    if (g_stepper_motor == nullptr) {
        return std::unexpected("Stepper motor not initialized");
    }

    ESP_LOGI(TAG, "Stopping stepper motor");
    g_stepper_motor->Stop();

    return std::string("Motor stopped");
}

// MCP Tool: Set stepper motor speed
// Parameters: delay_ms (int, required)
static ToolResult SetStepperSpeed(const PropertyList& properties) {
    if (g_stepper_motor == nullptr) {
        return std::unexpected("Stepper motor not initialized");
    }

    int delay_ms = properties["delay_ms"].value<int>();

    ESP_LOGI(TAG, "Setting stepper motor speed: delay=%dms", delay_ms);
    g_stepper_motor->SetSpeed(delay_ms);

    return std::string("Speed set successfully");
}

// MCP Tool: Send prompt to AI
// Parameters: prompt (string, required)
static ToolResult SendPrompt(const PropertyList& properties) {
    std::string prompt = properties["prompt"].value<std::string>();

    ESP_LOGI(TAG, "Sending prompt to AI: %s", prompt.c_str());

    // Send the prompt through the MCP protocol
    auto& app = Application::GetInstance();

    // Create a JSON message for the prompt
    CJsonUniquePtr json(cJSON_CreateObject());
    if (json == nullptr) {
        return std::unexpected("Failed to create JSON");
    }

    cJSON_AddStringToObject(json.get(), "type", "text");
    cJSON_AddStringToObject(json.get(), "text", prompt.c_str());

    CJsonStringUniquePtr json_str(cJSON_PrintUnformatted(json.get()));
    if (json_str == nullptr) {
        return std::unexpected("Failed to serialize JSON");
    }

    // Send through MCP
    app.SendMcpMessage(std::string(json_str.get()));

    return std::string("Prompt sent successfully");
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

void RegisterRobotMcpTools(RobotDrive* stepper_motor) {
    g_stepper_motor = stepper_motor;

    auto& mcp = McpServer::GetInstance();

    // Register stepper motor control tools
    mcp.AddUserOnlyTool("move_motor", "Move both DC motors forward or backward",
                        PropertyList(std::vector<Property>{
                            Property("steps", kPropertyTypeInteger, 0, -10000, 30000),
                            Property("delay_ms", kPropertyTypeInteger, 0, 0, 100)}),
                        MoveStepperMotor);

    mcp.AddUserOnlyTool("stop_motor", "Stop both DC motors", PropertyList(), StopStepperMotor);

    mcp.AddUserOnlyTool(
        "set_motor_speed", "Set the stepper motor speed (delay between steps)",
        PropertyList(std::vector<Property>{Property("delay_ms", kPropertyTypeInteger, 1, 100)}),
        SetStepperSpeed);

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
