#pragma once

#include "common/types.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace backtesting {

// Convert a std::tm broken-down UTC time to time_t without depending on the
// host's TZ environment. POSIX timegm()/Win32 _mkgmtime() are preferred;
// we fall back to a manual computation so we never silently use local time.
inline std::time_t tm_to_time_t_utc(std::tm& tm) {
#if defined(_WIN32)
    return _mkgmtime(&tm);
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(__FreeBSD__)
    return ::timegm(&tm);
#else
    // Portable fallback (gmtime epoch). Handles dates from 1970-01-01 onward.
    constexpr int kSecondsPerMinute = 60;
    constexpr int kSecondsPerHour   = 3600;
    constexpr int kSecondsPerDay    = 86400;
    int y = tm.tm_year + 1900;
    int m = tm.tm_mon + 1;
    int d = tm.tm_mday;
    // Days since 1970-01-01 using the Howard Hinnant civil-from-days algorithm.
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
    return static_cast<std::time_t>(
        days * kSecondsPerDay +
        tm.tm_hour * kSecondsPerHour +
        tm.tm_min * kSecondsPerMinute +
        tm.tm_sec);
#endif
}

// Parse a date or datetime string into a UTC time_point.
// Recognized formats (tried in order):
//   YYYY-MM-DDTHH:MM:SS, YYYY-MM-DD HH:MM:SS, YYYY-MM-DD, YYYY/MM/DD.
// Returns std::nullopt on failure so callers can decide a policy (skip vs default).
inline std::optional<Timestamp> parse_utc_timestamp(const std::string& s) {
    if (s.empty()) {
        return std::nullopt;
    }

    static constexpr const char* kFormats[] = {
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d",
        "%Y/%m/%d",
    };

    // Some inputs append a 'Z' UTC marker or fractional seconds; strip them before parsing.
    std::string normalized = s;
    if (!normalized.empty() && (normalized.back() == 'Z' || normalized.back() == 'z')) {
        normalized.pop_back();
    }
    auto dot = normalized.find('.');
    if (dot != std::string::npos) {
        // Drop fractional seconds and any trailing timezone offset for simplicity.
        normalized.erase(dot);
    }

    for (const char* fmt : kFormats) {
        std::tm tm{};
        // Use a portable manual parser: copy of the format string + sscanf-style read.
        // We rely on std::get_time via a stringstream for cross-platform consistency.
        std::istringstream ss(normalized);
        ss >> std::get_time(&tm, fmt);
        if (!ss.fail()) {
            // get_time leaves tm_isdst at 0 which is fine for UTC conversion.
            tm.tm_isdst = 0;
            const std::time_t t = tm_to_time_t_utc(tm);
            return std::chrono::system_clock::from_time_t(t);
        }
    }

    return std::nullopt;
}

// Convenience: parse with a fallback (e.g., system_clock::now()) when the input is invalid.
inline Timestamp parse_utc_timestamp_or(const std::string& s, Timestamp fallback) {
    if (auto v = parse_utc_timestamp(s)) return *v;
    return fallback;
}

} // namespace backtesting
