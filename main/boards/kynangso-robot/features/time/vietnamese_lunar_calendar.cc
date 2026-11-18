#include "features/time/vietnamese_lunar_calendar.h"

#include <cmath>

// Vietnamese conversion follows Ho Ngoc Duc's rules and Meeus-based
// astronomical approximations, with the civil day fixed to UTC+7.
namespace kynangso {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTimeZone = 7.0;
constexpr double kSynodicMonth = 29.530588853;

int FloorToInt(double value) { return static_cast<int>(std::floor(value)); }

int JulianDayNumber(int day, int month, int year) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

int NewMoonDay(int k) {
    const double t = k / 1236.85;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double radians = kPi / 180.0;
    const double jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * t2 - 0.000000155 * t3 +
                       0.00033 * std::sin((166.56 + 132.87 * t - 0.009173 * t2) * radians);
    const double m = 359.2242 + 29.10535608 * k - 0.0000333 * t2 - 0.00000347 * t3;
    const double m_prime = 306.0253 + 385.81691806 * k + 0.0107306 * t2 + 0.00001236 * t3;
    const double f = 21.2964 + 390.67050646 * k - 0.0016528 * t2 - 0.00000239 * t3;
    const double c1 =
        (0.1734 - 0.000393 * t) * std::sin(m * radians) + 0.0021 * std::sin(2 * radians * m) -
        0.4068 * std::sin(m_prime * radians) + 0.0161 * std::sin(2 * radians * m_prime) -
        0.0004 * std::sin(3 * radians * m_prime) + 0.0104 * std::sin(2 * radians * f) -
        0.0051 * std::sin((m + m_prime) * radians) - 0.0074 * std::sin((m - m_prime) * radians) +
        0.0004 * std::sin((2 * f + m) * radians) - 0.0004 * std::sin((2 * f - m) * radians) -
        0.0006 * std::sin((2 * f + m_prime) * radians) +
        0.0010 * std::sin((2 * f - m_prime) * radians) +
        0.0005 * std::sin((2 * m_prime + m) * radians);
    const double delta_t =
        t < -11 ? 0.001 + 0.000839 * t + 0.0002261 * t2 - 0.00000845 * t3 - 0.000000081 * t * t3
                : -0.000278 + 0.000265 * t + 0.000262 * t2;
    return FloorToInt(jd1 + c1 - delta_t + 0.5 + kTimeZone / 24.0);
}

int SunLongitudeSector(int day_number) {
    const double t = (day_number - 2451545.5 - kTimeZone / 24.0) / 36525.0;
    const double t2 = t * t;
    const double radians = kPi / 180.0;
    const double mean_anomaly =
        (357.52910 + 35999.05030 * t - 0.0001559 * t2 - 0.00000048 * t2 * t) * radians;
    const double mean_longitude = (280.46645 + 36000.76983 * t + 0.0003032 * t2) * radians;
    const double correction = (1.914600 - 0.004817 * t - 0.000014 * t2) * std::sin(mean_anomaly) +
                              (0.019993 - 0.000101 * t) * std::sin(2 * mean_anomaly) +
                              0.000290 * std::sin(3 * mean_anomaly);
    double longitude = mean_longitude + correction * radians;
    longitude -= 2 * kPi * std::floor(longitude / (2 * kPi));
    return FloorToInt(longitude / (2 * kPi) * 12.0);
}

int LunarMonth11(int year) {
    const int offset = JulianDayNumber(31, 12, year) - 2415021;
    const int k = FloorToInt(offset / kSynodicMonth);
    int month_start = NewMoonDay(k);
    if (SunLongitudeSector(month_start) >= 9)
        month_start = NewMoonDay(k - 1);
    return month_start;
}

int LeapMonthOffset(int month_11) {
    const int k = FloorToInt((month_11 - 2415021.076998695) / kSynodicMonth + 0.5);
    int previous_sector = 0;
    int i = 1;
    int sector = SunLongitudeSector(NewMoonDay(k + i));
    do {
        previous_sector = sector;
        ++i;
        sector = SunLongitudeSector(NewMoonDay(k + i));
    } while (sector != previous_sector && i < 14);
    return i - 1;
}

bool IsLeapYear(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

int DaysInMonth(int month, int year) {
    constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && IsLeapYear(year))
        return 29;
    return kDays[month - 1];
}
}  // namespace

bool SolarToVietnameseLunarDate(int day, int month, int year, VietnameseLunarDate* result) {
    if (!result || year < 1900 || year > 2100 || month < 1 || month > 12 || day < 1 ||
        day > DaysInMonth(month, year))
        return false;

    const int day_number = JulianDayNumber(day, month, year);
    const int k = FloorToInt((day_number - 2415021.076998695) / kSynodicMonth);
    int month_start = NewMoonDay(k + 1);
    if (month_start > day_number)
        month_start = NewMoonDay(k);

    int month_11 = LunarMonth11(year);
    int next_month_11 = month_11;
    int lunar_year = year;
    if (month_11 >= month_start) {
        month_11 = LunarMonth11(year - 1);
    } else {
        next_month_11 = LunarMonth11(year + 1);
        ++lunar_year;
    }

    const int lunar_day = day_number - month_start + 1;
    const int month_difference = (month_start - month_11) / 29;
    int lunar_month = month_difference + 11;
    bool leap_month = false;
    if (next_month_11 - month_11 > 365) {
        const int leap_offset = LeapMonthOffset(month_11);
        if (month_difference >= leap_offset) {
            lunar_month = month_difference + 10;
            leap_month = month_difference == leap_offset;
        }
    }
    if (lunar_month > 12)
        lunar_month -= 12;
    if (lunar_month >= 11 && month_difference < 4)
        --lunar_year;

    *result = {lunar_day, lunar_month, lunar_year, leap_month};
    return true;
}

}  // namespace kynangso
