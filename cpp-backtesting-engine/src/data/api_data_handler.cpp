// Minimal stub to satisfy linker; APIDataHandler is deprecated and not used.
#include "data/data_handler.h"

namespace backtesting {

APIDataHandler::APIDataHandler() : current_index_(0) {}

bool APIDataHandler::load_symbol_data(
    const std::string& /*symbol*/,
    const std::string& /*start_date*/,
    const std::string& /*end_date*/
) {
    return false;
}

bool APIDataHandler::has_next() const {
    return false;
}

OHLC APIDataHandler::get_next() {
    return OHLC{};
}

void APIDataHandler::reset() {}

std::vector<std::string> APIDataHandler::get_symbols() const {
    return {};
}

std::vector<OHLC> APIDataHandler::get_historical_data(const std::string& /*symbol*/) const {
    return {};
}

std::vector<OHLC> APIDataHandler::generate_sample_data(
    const std::string& /*symbol*/,
    const std::string& /*start_date*/,
    const std::string& /*end_date*/
) const {
    return {};
}

Timestamp APIDataHandler::parse_date(const std::string& /*date_str*/) const {
    return {};
}

} // namespace backtesting