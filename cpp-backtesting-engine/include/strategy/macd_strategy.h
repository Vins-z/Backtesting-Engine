#pragma once

#include "strategy.h"
#include <deque>

namespace backtesting {

class MACDStrategy : public Strategy {
private:
    int fast_period_;
    int slow_period_;
    int signal_period_;
    std::deque<double> price_history_;
    std::deque<double> macd_history_;
    std::deque<double> signal_history_;
    bool is_ready_;
    Signal last_signal_;
    
public:
    MACDStrategy(int fast_period = 12, int slow_period = 26, int signal_period = 9);
    
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "MACDStrategy"; }
    
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
private:
    void update_macd();
    double calculate_ema(const std::vector<double>& values, int period) const;
    double get_current_macd() const;
    double get_current_signal() const;
    double get_previous_macd() const;
    double get_previous_signal() const;
};

} // namespace backtesting 