#pragma once

#include "common/types.h"
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace backtesting {

// Technical indicator result structure
struct IndicatorResult {
    double current_value;
    std::vector<double> history;  // Last 30 days
    std::string signal;          // "bullish", "bearish", "neutral"
    std::string interpretation;  // Human-readable interpretation
};

// Technical indicators calculator
class TechnicalIndicatorsCalculator {
public:
    // Calculate all indicators for a symbol
    static nlohmann::json calculate_all_indicators(
        const std::vector<OHLC>& data,
        const std::string& symbol
    );
    
    // Individual indicator calculations
    static IndicatorResult calculate_sma(const std::vector<OHLC>& data, int period);
    static IndicatorResult calculate_ema(const std::vector<OHLC>& data, int period);
    static IndicatorResult calculate_rsi(const std::vector<OHLC>& data, int period = 14);
    static IndicatorResult calculate_macd(const std::vector<OHLC>& data);
    static IndicatorResult calculate_bollinger_bands(const std::vector<OHLC>& data, int period = 20, double std_dev = 2.0);
    static IndicatorResult calculate_atr(const std::vector<OHLC>& data, int period = 14);
    static IndicatorResult calculate_volume_indicators(const std::vector<OHLC>& data);
    
    // Signal interpretation helpers
    static std::string interpret_rsi_signal(double rsi);
    static std::string interpret_macd_signal(double macd, double signal, double histogram);
    static std::string interpret_bollinger_signal(double price, double upper, double lower, double middle);
    static std::string interpret_sma_signal(double price, double sma_short, double sma_long);
    
private:
    // Helper functions
    static std::vector<double> extract_closes(const std::vector<OHLC>& data);
    static std::vector<double> extract_highs(const std::vector<OHLC>& data);
    static std::vector<double> extract_lows(const std::vector<OHLC>& data);
    static std::vector<double> extract_volumes(const std::vector<OHLC>& data);
    static std::vector<double> get_last_n_values(const std::vector<double>& values, int n);
};

} // namespace backtesting
