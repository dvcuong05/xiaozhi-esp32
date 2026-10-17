#include "robot_qr_code.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

int main() {
    using mrkenddo::RobotQrCode;
    RobotQrCode code;
    assert(RobotQrCode::Generate("http://192.0.2.1/", code));
    assert(code.modules >= 21 && code.modules <= 65 && (code.modules - 17) % 4 == 0);
    // All three finder patterns and their white inner borders survived packing.
    for (const auto [row, column] :
         {std::pair{0, 0}, std::pair{0, code.modules - 7}, std::pair{code.modules - 7, 0}}) {
        assert(code.IsBlack(row, column));
        assert(!code.IsBlack(row + 1, column + 1));
        assert(code.IsBlack(row + 3, column + 3));
    }
    assert(!code.IsBlack(-1, 0) && !code.IsBlack(65, 0) && !code.IsBlack(0, 65));
    // Synthetic public-test payload only; never write a real bank payload to logs/artifacts.
    std::ofstream pbm("/tmp/mrkenddo-qr-test.pbm");
    constexpr int scale = 6, quiet = 4;
    const int side = (code.modules + 2 * quiet) * scale;
    pbm << "P1\n" << side << " " << side << "\n";
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x)
            pbm << (code.IsBlack(y / scale - quiet, x / scale - quiet) ? "1 " : "0 ");
        pbm << '\n';
    }
    pbm.close();
    assert(RobotQrCode::Generate(std::string(256, 'a'), code));
    assert(code.modules <= 65);
    assert((240 - 28) / (code.modules + 8) >= 2);
    assert(!RobotQrCode::Generate(std::string(257, 'a'), code));
    assert(!RobotQrCode::Generate("", code));
    assert(!RobotQrCode::Generate(std::string("a\0b", 3), code));
    assert(!RobotQrCode::Generate("line\ncontrol", code));
    assert(!RobotQrCode::ValidText("\xc0\xaf", 256));
    assert(!RobotQrCode::ValidText("\xed\xa0\x80", 256));
    assert(!RobotQrCode::ValidText("\xf4\x90\x80\x80", 256));
    assert(!RobotQrCode::ValidText("\xe1\x80", 256));
    assert(RobotQrCode::Generate("Xin chào Việt Nam 🌟", code));
    mrkenddo::QrConfig config;
    assert(RobotQrCode::ValidateConfig(config));
    config.valid = false;
    assert(!RobotQrCode::ValidateConfig(config));
    config.valid = true;
    config.payload[256] = 'x';
    assert(!RobotQrCode::ValidateConfig(config));
    puts(
        "PASS actual Nayuki QR: URL/Unicode/max payload, finder patterns, packed modules, pixel "
        "scale, malformed UTF-8 and config health");
}
