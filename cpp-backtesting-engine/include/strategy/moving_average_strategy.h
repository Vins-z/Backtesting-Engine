#pragma once

#include "strategy.h"
#include <deque>

namespace backtesting {

class MovingAverageStrategy : public Strategy {
private:
    int short_window_;
    int long_window_;
    std::deque<double> price_history_;
    std::deque<double> short_ma_history_;
    std::deque<double> long_ma_history_;
    bool is_ready_;
    Signal last_signal_;
    
public:
    MovingAverageStrategy(int short_window = 10, int long_window = 20);
    
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "MovingAverageStrategy"; }
    
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
private:
    void update_moving_averages();
    double get_current_short_ma() const;
    double get_current_long_ma() const;
    double get_previous_short_ma() const;
    double get_previous_long_ma() const;
};

} // namespace backtesting 