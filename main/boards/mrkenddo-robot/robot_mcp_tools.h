#ifndef ROBOT_MCP_TOOLS_H
#define ROBOT_MCP_TOOLS_H

#include "mcp_server.h"
#include "robot_drive.h"
#include "application.h"

#include <string>

bool QueueRobotWebPrompt(std::string prompt);
const char* GetRobotWebPromptStatus();

void RegisterRobotMcpTools(RobotDrive* drive);

#endif // ROBOT_MCP_TOOLS_H
