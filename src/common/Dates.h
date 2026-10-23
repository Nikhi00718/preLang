#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

// Dates and times as PARSE_DATE reads them and DERIVE takes them apart. A date
// is held as seconds since 1970-01-01 00:00, without a time zone, as pandas
// holds a datetime64 value. Only the ISO layouts pandas' to_datetime reads
// without being told a format are accepted, and a column must use one of them
// throughout (to_datetime takes the layout from the first value).

enum class DateLayout { None, Date, Minutes, Seconds, SecondsT };

inline const char* dateLayoutText(DateLayout layout) {
    switch (layout) {
        case DateLayout::Date:     return "YYYY-MM-DD";
        case DateLayout::Minutes:  return "YYYY-MM-DD HH:MM";
        case DateLayout::Seconds:  return "YYYY-MM-DD HH:MM:SS";
        case DateLayout::SecondsT: return "YYYY-MM-DDTHH:MM:SS";
        case DateLayout::None:     break;
    }
    return "";
}

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar (H. Hinnant's algorithm).
inline std::int64_t daysFromCivil(std::int64_t year, int month, int day) {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t yearOfEra = year - era * 400;
    const std::int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const std::int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

struct CivilTime {
    std::int64_t year = 1970;
    int month = 1, day = 1, hour = 0, weekday = 3;  // weekday: Monday 0 ... Sunday 6, as pandas' dt.weekday
    std::int64_t days = 0;                          // days since 1970-01-01
};

inline CivilTime civilFromSeconds(double seconds) {
    CivilTime time;
    const auto total = static_cast<std::int64_t>(std::floor(seconds));
    std::int64_t days = total / 86400;
    std::int64_t rest = total % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    time.days = days;
    time.hour = static_cast<int>(rest / 3600);
    time.weekday = static_cast<int>(((days + 3) % 7 + 7) % 7);  // 1970-01-01 was a Thursday
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t dayOfEra = z - era * 146097;
    const std::int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const std::int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const std::int64_t mp = (5 * dayOfYear + 2) / 153;
    time.day = static_cast<int>(dayOfYear - (153 * mp + 2) / 5 + 1);
    time.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    time.year = yearOfEra + era * 400 + (time.month <= 2);
    return time;
}

// The layout of one cell and its value, or DateLayout::None if it is not a valid ISO date.
inline DateLayout parseIsoDate(std::string_view text, double& seconds) {
    const auto digits = [&](size_t at, size_t count, int& value) {
        value = 0;
        for (size_t i = at; i < at + count; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return false;
            }
            value = value * 10 + (text[i] - '0');
        }
        return true;
    };
    DateLayout layout;
    switch (text.size()) {
        case 10: layout = DateLayout::Date; break;
        case 16: layout = DateLayout::Minutes; break;
        case 19: layout = text[10] == 'T' ? DateLayout::SecondsT : DateLayout::Seconds; break;
        default: return DateLayout::None;
    }
    int year, month, day, hour = 0, minute = 0, second = 0;
    if (!digits(0, 4, year) || text[4] != '-' || !digits(5, 2, month) || text[7] != '-' || !digits(8, 2, day)) {
        return DateLayout::None;
    }
    if (layout != DateLayout::Date) {
        if ((text[10] != ' ' && text[10] != 'T') || (layout == DateLayout::Minutes && text[10] != ' ') ||
            !digits(11, 2, hour) || text[13] != ':' || !digits(14, 2, minute)) {
            return DateLayout::None;
        }
        if (text.size() == 19 && (text[16] != ':' || !digits(17, 2, second))) {
            return DateLayout::None;
        }
    }
    static const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month < 1 || month > 12 || day < 1 || day > monthDays[month - 1] + (month == 2 && leap) || hour > 23 ||
        minute > 59 || second > 59) {
        return DateLayout::None;
    }
    seconds = static_cast<double>(daysFromCivil(year, month, day) * 86400 + std::int64_t{hour} * 3600 +
                                  std::int64_t{minute} * 60 + second);
    return layout;
}

// The first second of the year, month or day of a date, and the last one.
inline double startOf(const std::string& unit, double seconds) {
    const CivilTime time = civilFromSeconds(seconds);
    const std::int64_t day = unit == "YEAR"    ? daysFromCivil(time.year, 1, 1)
                             : unit == "MONTH" ? daysFromCivil(time.year, time.month, 1)
                                               : time.days;
    return static_cast<double>(day * 86400);
}

inline double endOf(const std::string& unit, double seconds) {
    const CivilTime time = civilFromSeconds(seconds);
    const std::int64_t next = unit == "YEAR"    ? daysFromCivil(time.year + 1, 1, 1)
                              : unit == "MONTH" ? (time.month == 12 ? daysFromCivil(time.year + 1, 1, 1)
                                                                    : daysFromCivil(time.year, time.month + 1, 1))
                                                : time.days + 1;
    return static_cast<double>(next * 86400 - 1);
}

// "2012-10-02 09:00:00" for a value in seconds; "-inf" or "inf" for an open bound.
inline std::string dateText(double seconds) {
    if (std::isinf(seconds)) {
        return seconds < 0 ? "-inf" : "inf";
    }
    const CivilTime time = civilFromSeconds(seconds);
    const auto rest = static_cast<std::int64_t>(std::floor(seconds)) - time.days * 86400;
    char text[128];  // room for any year, so GCC's -Wformat-truncation has nothing to report
    std::snprintf(text, sizeof text, "%04lld-%02d-%02d %02lld:%02lld:%02lld", static_cast<long long>(time.year),
                  time.month, time.day, static_cast<long long>(rest / 3600), static_cast<long long>(rest % 3600 / 60),
                  static_cast<long long>(rest % 60));
    return text;
}
