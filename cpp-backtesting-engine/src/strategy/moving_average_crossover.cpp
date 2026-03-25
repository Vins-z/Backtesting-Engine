#include "strategy/strategy.h"
#include <ta_libc.h>

namespace backtesting {

MovingAverageCrossover::MovingAverageCrossover(int short_window, int long_window)
    : short_window_(short_window), long_window_(long_window) {
    // Initialize TA-Lib
    TA_Initialize();
}

std::vector<Signal> MovingAverageCrossover::generate_signals(const std::vector<OHLC>& data) {
    if (data.size() < static_cast<size_t>(long_window_)) {
        return std::vector<Signal>(data.size(), Signal::NONE);
    }
    
    // Extract close prices
    std::vector<double> closes(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        closes[i] = data[i].close;
    }
    
    // Calculate moving averages using TA-Lib
    std::vector<double> short_ma(data.size());
    std::vector<double> long_ma(data.size());
    
    int short_out_begin, short_out_nb_element;
    int long_out_begin, long_out_nb_element;
    
    // Calculate short-term moving average
    TA_RetCode short_ret = TA_SMA(
        0, data.size() - 1,
        closes.data(),
        short_window_,
        &short_out_begin, &short_out_nb_element,
        short_ma.data()
    );
    
    // Calculate long-term moving average
    TA_RetCode long_ret = TA_SMA(
        0, data.size() - 1,
        closes.data(),
        long_window_,
        &long_out_begin, &long_out_nb_element,
        long_ma.data()
    );
    
    if (short_ret != TA_SUCCESS || long_ret != TA_SUCCESS) {
        return std::vector<Signal>(data.size(), Signal::NONE);
    }
    
    // Generate signals based on crossover
    std::vector<Signal> signals(data.size(), Signal::NONE);
    
    // Start from the point where both MAs are available
    int start_idx = std::max(short_out_begin, long_out_begin) + 1;
    
    for (int i = start_idx; i < static_cast<int>(data.size()); ++i) {
        int short_idx = i - short_out_begin;
        int long_idx = i - long_out_begin;
        int prev_short_idx = (i - 1) - short_out_begin;
        int prev_long_idx = (i - 1) - long_out_begin;
        
        // Check bounds
        if (short_idx >= 0 && short_idx < short_out_nb_element &&
            long_idx >= 0 && long_idx < long_out_nb_element &&
            prev_short_idx >= 0 && prev_short_idx < short_out_nb_element &&
            prev_long_idx >= 0 && prev_long_idx < long_out_nb_element) {
            
            // Current values
            double short_current = short_ma[short_idx];
            double long_current = long_ma[long_idx];
            
            // Previous values
            double short_previous = short_ma[prev_short_idx];
            double long_previous = long_ma[prev_long_idx];
            
            // Bullish crossover: short MA crosses above long MA
            if (short_current > long_current && short_previous <= long_previous) {
                signals[i] = Signal::BUY;
            }
            // Bearish crossover: short MA crosses below long MA
            else if (short_current < long_current && short_previous >= long_previous) {
                signals[i] = Signal::SELL;
            }
        }
    }
    
    return signals;
}

std::unordered_map<std::string, double> MovingAverageCrossover::get_parameters() const {
    return {
        {"short_window", static_cast<double>(short_window_)},
        {"long_window", static_cast<double>(long_window_)}
    };
}

void MovingAverageCrossover::set_parameters(const std::unordered_map<std::string, double>& params) {
    auto short_it = params.find("short_window");
    if (short_it != params.end()) {
        short_window_ = static_cast<int>(short_it->second);
    }
    
    auto long_it = params.find("long_window");
    if (long_it != params.end()) {
        long_window_ = static_cast<int>(long_it->second);
    }
}

} // namespace backtesting 