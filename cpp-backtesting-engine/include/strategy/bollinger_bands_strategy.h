#pragma once

#include "strategy.h"
#include <deque>

namespace backtesting {

class BollingerBandsStrategy : public Strategy {
private:
    int period_;
    double std_dev_;
    std::deque<double> price_history_;
    std::deque<double> upper_band_history_;
    std::deque<double> lower_band_history_;
    std::deque<double> middle_band_history_;
    bool is_ready_;
    Signal last_signal_;
    
public:
    BollingerBandsStrategy(int period = 20, double std_dev = 2.0);
    
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "BollingerBandsStrategy"; }
    
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
private:
    void update_bands();
    double calculate_sma() const;
    double calculate_std_deviation() const;
    double get_current_upper_band() const;
    double get_current_lower_band() const;
    double get_current_middle_band() const;
};

} // namespace backtesting 