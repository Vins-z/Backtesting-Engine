#include "strategy/moving_average_strategy.h"
#include "portfolio/portfolio_manager.h"
#include <numeric>
#include <algorithm>

namespace backtesting {

MovingAverageStrategy::MovingAverageStrategy(int short_window, int long_window)
    : short_window_(short_window), long_window_(long_window), is_ready_(false), last_signal_(Signal::NONE) {
    if (short_window_ >= long_window_) {
        throw std::invalid_argument("Short window must be smaller than long window");
    }
}

void MovingAverageStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    for (const auto& ohlc : historical_data) {
        update(ohlc);
    }
}

void MovingAverageStrategy::update(const OHLC& new_data) {
    price_history_.push_back(new_data.close);
    
    // Keep only the required history
    if (price_history_.size() > static_cast<size_t>(long_window_ * 2)) {
        price_history_.pop_front();
    }
    
    update_moving_averages();
    
    // Strategy is ready when we have enough data for long MA
    if (price_history_.size() >= static_cast<size_t>(long_window_)) {
        is_ready_ = true;
    }
}

Signal MovingAverageStrategy::generate_signal(
    const std::string& symbol,
    const OHLC& current_data,
    const PortfolioManager& portfolio
) {
    (void)symbol;  // Unused parameter
    (void)current_data;  // Unused parameter
    (void)portfolio;  // Unused parameter
    
    if (!is_ready_ || short_ma_history_.size() < 2 || long_ma_history_.size() < 2) {
        return Signal::NONE;
    }
    
    double current_short_ma = get_current_short_ma();
    double current_long_ma = get_current_long_ma();
    double previous_short_ma = get_previous_short_ma();
    double previous_long_ma = get_previous_long_ma();
    
    // Golden Cross: Short MA crosses above Long MA -> BUY signal
    if (previous_short_ma <= previous_long_ma && current_short_ma > current_long_ma) {
        last_signal_ = Signal::BUY;
        return Signal::BUY;
    }
    
    // Death Cross: Short MA crosses below Long MA -> SELL signal
    if (previous_short_ma >= previous_long_ma && current_short_ma < current_long_ma) {
        last_signal_ = Signal::SELL;
        return Signal::SELL;
    }
    
    return Signal::NONE;
}

void MovingAverageStrategy::update_moving_averages() {
    if (price_history_.size() < static_cast<size_t>(short_window_)) {
        return;
    }
    
    // Calculate short MA
    auto short_begin = price_history_.end() - short_window_;
    double short_sum = std::accumulate(short_begin, price_history_.end(), 0.0);
    double short_ma = short_sum / short_window_;
    short_ma_history_.push_back(short_ma);
    
    // Keep only recent history
    if (short_ma_history_.size() > 10) {
        short_ma_history_.pop_front();
    }
    
    // Calculate long MA if we have enough data
    if (price_history_.size() >= static_cast<size_t>(long_window_)) {
        auto long_begin = price_history_.end() - long_window_;
        double long_sum = std::accumulate(long_begin, price_history_.end(), 0.0);
        double long_ma = long_sum / long_window_;
        long_ma_history_.push_back(long_ma);
        
        if (long_ma_history_.size() > 10) {
            long_ma_history_.pop_front();
        }
    }
}

std::unordered_map<std::string, double> MovingAverageStrategy::get_parameters() const {
    return {
        {"short_window", static_cast<double>(short_window_)},
        {"long_window", static_cast<double>(long_window_)}
    };
}

void MovingAverageStrategy::reset() {
    price_history_.clear();
    short_ma_history_.clear();
    long_ma_history_.clear();
    is_ready_ = false;
    last_signal_ = Signal::NONE;
}

bool MovingAverageStrategy::is_ready() const {
    return is_ready_;
}

double MovingAverageStrategy::get_current_short_ma() const {
    return short_ma_history_.empty() ? 0.0 : short_ma_history_.back();
}

double MovingAverageStrategy::get_current_long_ma() const {
    return long_ma_history_.empty() ? 0.0 : long_ma_history_.back();
}

double MovingAverageStrategy::get_previous_short_ma() const {
    return short_ma_history_.size() < 2 ? 0.0 : short_ma_history_[short_ma_history_.size() - 2];
}

double MovingAverageStrategy::get_previous_long_ma() const {
    return long_ma_history_.size() < 2 ? 0.0 : long_ma_history_[long_ma_history_.size() - 2];
}

} // namespace backtesting 