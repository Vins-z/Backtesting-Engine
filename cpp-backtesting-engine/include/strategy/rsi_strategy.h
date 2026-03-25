#pragma once

#include "strategy.h"
#include <deque>

namespace backtesting {

class RSIStrategy : public Strategy {
private:
    int period_;
    double oversold_threshold_;
    double overbought_threshold_;
    std::deque<double> price_history_;
    std::deque<double> rsi_history_;
    bool is_ready_;
    Signal last_signal_;
    
public:
    RSIStrategy(int period = 14, double oversold = 30.0, double overbought = 70.0);
    
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "RSIStrategy"; }
    
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
private:
    void update_rsi();
    double calculate_current_rsi() const;
};

} // namespace backtesting 