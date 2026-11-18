#pragma once

namespace kynangso {

struct VietnameseLunarDate {
    int day = 0;
    int month = 0;
    int year = 0;
    bool leap_month = false;
};

// Converts a Gregorian civil date to the Vietnamese lunar calendar (UTC+7).
// Returns false for invalid dates or years outside 1900..2100.
bool SolarToVietnameseLunarDate(int day, int month, int year, VietnameseLunarDate* result);

}  // namespace kynangso
