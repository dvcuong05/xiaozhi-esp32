#ifndef KYNANGSO_MARKET_BULLETIN_H_
#define KYNANGSO_MARKET_BULLETIN_H_

#include <array>
#include <cstddef>
#include <string>

namespace kynangso {

inline constexpr size_t kMarketBulletinMaxTiles = 4;
inline constexpr size_t kMarketBulletinMaxRows = 8;

struct MarketBulletinRow {
    std::string label;
    std::string value;
};

struct MarketBulletinTile {
    std::string category;
    std::string updated_at;
    bool available = false;
    bool stale = false;
    std::array<MarketBulletinRow, kMarketBulletinMaxRows> rows{};
    size_t row_count = 0;
};

struct MarketBulletin {
    std::array<MarketBulletinTile, kMarketBulletinMaxTiles> tiles{};
    size_t tile_count = 0;
};

}  // namespace kynangso

#endif  // KYNANGSO_MARKET_BULLETIN_H_
