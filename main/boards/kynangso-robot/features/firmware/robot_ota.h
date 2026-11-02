#pragma once
#include <functional>
namespace kynangso {
// One persistent internal-stack worker: flash writes must not run on a PSRAM stack.
void StartRobotOta(std::function<void()> before_update,
                   std::function<void(const char*, int)> progress);
}  // namespace kynangso
