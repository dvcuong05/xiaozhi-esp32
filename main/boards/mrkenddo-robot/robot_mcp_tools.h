#ifndef ROBOT_MCP_TOOLS_H
#define ROBOT_MCP_TOOLS_H

#include "application.h"
#include "mcp_server.h"
#include "robot_drive.h"

#include <functional>
#include <string>

bool QueueRobotWebPrompt(std::string prompt);
const char* GetRobotWebPromptStatus();

void RegisterRobotMcpTools(RobotDrive* drive, std::function<bool(int)> set_oled_mode = {},
                           std::function<std::string()> show_ip = {},
                           std::function<bool(std::string, int)> show_oled_text = {});

#endif  // ROBOT_MCP_TOOLS_H
