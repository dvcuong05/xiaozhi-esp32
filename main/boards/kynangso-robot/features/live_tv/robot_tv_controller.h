#pragma once

#include <functional>
#include <string>

namespace kynangso {
class RobotLiveTv;

class RobotTvController {
public:
    explicit RobotTvController(RobotLiveTv& player) : player_(player) {}
    bool HandleCommand(const std::string& payload, std::string* result, std::string* error);
    bool RequestService(const std::string& payload, std::string* result, std::string* error);
    std::string StatusJson();
    void StopLocally();
    void SetBeforePlay(std::function<void()> callback) { before_play_ = std::move(callback); }

private:
    RobotLiveTv& player_;
    std::function<void()> before_play_;
};
}  // namespace kynangso
