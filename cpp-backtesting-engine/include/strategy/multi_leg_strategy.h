#pragma once

#include "strategy/strategy.h"
#include <vector>
#include <string>

namespace backtesting {

struct StrategyLeg {
    std::string symbol;
    std::string direction; // "BUY" or "SELL"
    double quantity;
    double stop_loss_pct;
    double take_profit_pct;
    std::string strike;
    std::string segment; // "EQUITY", "OPTIONS", "FUTURES"
};

class MultiLegStrategy : public Strategy {
public:
    // Indicator storage with comprehensive parameters
    struct IndicatorConfig {
        std::string type;  // "SMA", "EMA", "RSI", "MACD", "BOLLINGER", "ATR", "STOCHASTIC", "WILLIAMS_R", "CCI", "OBV", "MFI", "ADX", "SAR"
        int length;        // Period/length (for most indicators)
        double param1;     // Additional parameter (e.g., stdDev for Bollinger, fastPeriod for MACD)
        double param2;     // Additional parameter (e.g., slowPeriod for MACD, dPeriod for Stochastic)
        double param3;     // Additional parameter (e.g., signalPeriod for MACD)
        double weight;     // Weight for signal generation (default 1.0)
    };

    MultiLegStrategy(const std::vector<StrategyLeg>& legs);
    
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    void set_indicators(const std::vector<std::pair<std::string, int>>& indicators); // type, length pairs (legacy)
    void set_indicators_extended(const std::vector<IndicatorConfig>& indicators); // Full indicator configs
    std::string get_name() const override { return "MultiLegStrategy"; }
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;

private:
    std::vector<StrategyLeg> legs_;
    std::vector<OHLC> history_;
    bool is_ready_ = false;
    
    // Indicator storage
    std::vector<IndicatorConfig> indicators_;
    
    // Indicator result caching
    mutable std::unordered_map<std::string, double> indicator_cache_;
    mutable std::unordered_map<std::string, std::pair<double, double>> indicator_cache_pair_; // For indicators with two values
    
    // Calculate indicators and generate signal based on them
    Signal evaluate_indicators(const std::string& symbol, const OHLC& current_data) const;
    double get_indicator_value(const IndicatorConfig& indicator, const std::vector<OHLC>& data) const;
    std::pair<double, double> get_indicator_pair_value(const IndicatorConfig& indicator, const std::vector<OHLC>& data) const;
    
    // Signal strength calculation
    double calculate_signal_strength(int bullish_count, int bearish_count, double total_weight) const;
};

} // namespace backtesting
