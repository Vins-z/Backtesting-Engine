#include "data/data_handler.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <filesystem>

namespace backtesting {

CSVDataHandler::CSVDataHandler(const std::string& data_path) 
    : data_path_(data_path), current_index_(0) {}

bool CSVDataHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) {
    // Create filename or search recursively
    std::string filename = find_symbol_csv_path(symbol);
    if (filename.empty()) {
        // fallback to flat path
        filename = data_path_ + "/" + symbol + ".csv";
    }
    if (!load_csv_file(filename)) {
        return false;
    }

    Timestamp start_ts{};
    Timestamp end_ts{};
    const bool has_start = !start_date.empty();
    const bool has_end = !end_date.empty();
    if (has_start) {
        start_ts = parse_timestamp(start_date);
    }
    if (has_end) {
        end_ts = parse_timestamp(end_date);
    }
    
    symbols_.push_back(symbol);
    
    // Combine all symbol data into current_data_ for iteration
    if (symbol_data_.find(symbol) != symbol_data_.end()) {
        auto& series = symbol_data_[symbol];
        std::vector<OHLC> filtered;
        filtered.reserve(series.size());
        for (const auto& bar : series) {
            if (has_start && bar.timestamp < start_ts) continue;
            if (has_end && bar.timestamp > end_ts) continue;
            filtered.push_back(bar);
        }
        series = std::move(filtered);
        current_data_.insert(current_data_.end(), series.begin(), series.end());
    }
    
    // Sort by timestamp
    std::sort(current_data_.begin(), current_data_.end(), 
              [](const OHLC& a, const OHLC& b) {
                  return a.timestamp < b.timestamp;
              });
    
    return true;
}

bool CSVDataHandler::has_next() const {
    return current_index_ < current_data_.size();
}

OHLC CSVDataHandler::get_next() {
    if (!has_next()) {
        return OHLC{};
    }
    
    OHLC data = current_data_[current_index_];
    current_index_++;
    return data;
}

void CSVDataHandler::reset() {
    current_index_ = 0;
}

std::vector<std::string> CSVDataHandler::get_symbols() const {
    return symbols_;
}

std::vector<OHLC> CSVDataHandler::get_historical_data(const std::string& symbol) const {
    auto it = symbol_data_.find(symbol);
    if (it != symbol_data_.end()) {
        return it->second;
    }
    return {};
}

bool CSVDataHandler::load_csv_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    std::vector<OHLC> data;

    // Parse header to discover column indices.
    // Some bundled datasets include a first metadata/comment line like:
    //   # Metadata: {...}
    // Skip those lines before reading the actual CSV header row.
    if (!std::getline(file, line)) {
        return false;
    }
    while (!line.empty() && line[0] == '#') {
        if (!std::getline(file, line)) {
            return false;
        }
    }
    // Also tolerate leading empty lines before the header.
    while (line.empty()) {
        if (!std::getline(file, line)) {
            return false;
        }
        while (!line.empty() && line[0] == '#') {
            if (!std::getline(file, line)) {
                return false;
            }
        }
    }

    std::vector<std::string> header_cells;
    {
        std::stringstream hs(line);
        std::string cell;
        while (std::getline(hs, cell, ',')) header_cells.push_back(cell);
    }
    // Map columns (case-sensitive matches typical yfinance)
    auto find_idx = [&](const std::string& name) -> int {
        for (size_t i = 0; i < header_cells.size(); ++i) {
            if (header_cells[i] == name) return static_cast<int>(i);
        }
        return -1;
    };
    int date_idx = find_idx("Date");
    int open_idx = find_idx("Open");
    int high_idx = find_idx("High");
    int low_idx = find_idx("Low");
    int close_idx = find_idx("Close");
    int volume_idx = find_idx("Volume");
    if (date_idx < 0 || open_idx < 0 || high_idx < 0 || low_idx < 0 || close_idx < 0 || volume_idx < 0) {
        return false;
    }
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        try {
            std::stringstream ss(line);
            std::string cell;
            std::vector<std::string> cells;
            while (std::getline(ss, cell, ',')) cells.push_back(cell);
            if (static_cast<int>(cells.size()) <= std::max({date_idx, open_idx, high_idx, low_idx, close_idx, volume_idx})) continue;
            OHLC ohlc = parse_csv_line(cells, date_idx, open_idx, high_idx, low_idx, close_idx, volume_idx);
            data.push_back(ohlc);
        } catch (const std::exception& e) {
            // Skip invalid lines
            continue;
        }
    }
    // If the CSV had a valid header but no OHLC rows, treat it as unusable.
    // This prevents silently running backtests with 0 datapoints.
    if (data.empty()) {
        return false;
    }
    
    // Extract symbol from filename
    std::string symbol = filename.substr(filename.find_last_of("/") + 1);
    symbol = symbol.substr(0, symbol.find_last_of("."));
    
    symbol_data_[symbol] = data;
    return true;
}

OHLC CSVDataHandler::parse_csv_line(const std::vector<std::string>& cells, int date_idx, int open_idx, int high_idx, int low_idx, int close_idx, int volume_idx) const {
    OHLC ohlc;
    ohlc.timestamp = parse_timestamp(cells[date_idx]);
    ohlc.open = std::stod(cells[open_idx]);
    ohlc.high = std::stod(cells[high_idx]);
    ohlc.low = std::stod(cells[low_idx]);
    ohlc.close = std::stod(cells[close_idx]);
    // Some CSVs may have empty volume; default to 0
    try {
        ohlc.volume = cells[volume_idx].empty() ? 0 : std::stoll(cells[volume_idx]);
    } catch (...) {
        ohlc.volume = 0;
    }
    return ohlc;
}

Timestamp CSVDataHandler::parse_timestamp(const std::string& date_str) const {
    // Simple date parsing for YYYY-MM-DD format
    std::tm tm = {};
    std::istringstream ss(date_str);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string CSVDataHandler::find_symbol_csv_path(const std::string& symbol) const {
    namespace fs = std::filesystem;
    std::string target_name = symbol + ".csv";
    try {
        for (const auto& entry : fs::recursive_directory_iterator(data_path_)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().filename() == target_name) {
                return entry.path().string();
            }
        }
    } catch (...) {
        // ignore traversal errors
    }
    return std::string();
}

} // namespace backtesting 