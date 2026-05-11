#include "common/time_utils.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

// Verify parse_utc_timestamp() yields the same time_point regardless of TZ.
// Many handlers previously used std::mktime, which interprets the broken-down
// time as local time; that caused the same CSV to produce different bars on
// different machines.

namespace {

#if defined(_WIN32)
static void set_tz(const char* tz) { _putenv_s("TZ", tz); _tzset(); }
#else
static void set_tz(const char* tz) { setenv("TZ", tz, 1); tzset(); }
#endif

}

int main() {
    using namespace backtesting;
    const std::string input = "2024-01-15";

    set_tz("UTC");
    auto utc = parse_utc_timestamp(input);

    set_tz("America/Los_Angeles");
    auto la  = parse_utc_timestamp(input);

    set_tz("Asia/Kolkata");
    auto ist = parse_utc_timestamp(input);

    set_tz("Pacific/Kiritimati"); // UTC+14, lots of older code breaks here
    auto kir = parse_utc_timestamp(input);

    if (!utc || !la || !ist || !kir) {
        std::cerr << "parse_utc_timestamp failed for one of the TZ runs\n";
        return 1;
    }
    if (*utc != *la || *utc != *ist || *utc != *kir) {
        std::cerr << "TZ leakage detected: parse_utc_timestamp depended on local TZ\n";
        return 1;
    }

    // 2024-01-15 00:00:00 UTC == 1705276800 seconds since epoch.
    const auto epoch_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        utc->time_since_epoch()).count();
    if (epoch_seconds != 1705276800LL) {
        std::cerr << "Unexpected epoch seconds: " << epoch_seconds << " (want 1705276800)\n";
        return 1;
    }

    // ISO datetime with timezone-Z suffix should also parse.
    auto iso = parse_utc_timestamp("2024-01-15T00:00:00Z");
    if (!iso || *iso != *utc) {
        std::cerr << "ISO Z-suffixed datetime did not match plain date\n";
        return 1;
    }

    // Garbage input returns nullopt.
    if (parse_utc_timestamp("not-a-date").has_value()) {
        std::cerr << "Expected parse failure for garbage input\n";
        return 1;
    }

    return 0;
}
