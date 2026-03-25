#include "strategy/macd_strategy.h"
#include "portfolio/portfolio_manager.h"
#include <algorithm>
#include <cmath>

namespace backtesting {

MACDStrategy::MACDStrategy(int fast_period, int slow_period, int signal_period)
    : fast_period_(fast_period), slow_period_(slow_period), signal_period_(signal_period),
      is_ready_(false), last_signal_(Signal::NONE) {
    if (fast_period_ >= slow_period_) {
        throw std::invalid_argument("Fast period must be smaller than slow period");
    }
}

void MACDStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    for (const auto& ohlc : historical_data) {
        update(ohlc);
    }
}

void MACDStrategy::update(const OHLC& new_data) {
    price_history_.push_back(new_data.close);
    
    // Keep only required history
    if (price_history_.size() > static_cast<size_t>(slow_period_ * 3)) {
        price_history_.pop_front();
    }
    
    update_macd();
    
    // Strategy is ready when we have enough data for signal line
    if (price_history_.size() >= static_cast<size_t>(slow_period_ + signal_period_)) {
        is_ready_ = true;
    }
}

Signal MACDStrategy::generate_signal(
    const std::string& symbol,
    const OHLC& current_data,
    const PortfolioManager& portfolio
) {
    (void)symbol;  // Unused parameter
    (void)current_data;  // Unused parameter
    (void)portfolio;  // Unused parameter
    
    if (!is_ready_ || macd_history_.size() < 2 || signal_history_.size() < 2) {
        return Signal::NONE;
    }
    
    double current_macd = get_current_macd();
    double current_signal = get_current_signal();
    double previous_macd = get_previous_macd();
    double previous_signal = get_previous_signal();
    
    // MACD bullish crossover: MACD crosses above signal line -> BUY
    if (previous_macd <= previous_signal && current_macd > current_signal) {
        last_signal_ = Signal::BUY;
        return Signal::BUY;
    }
    
    // MACD bearish crossover: MACD crosses below signal line -> SELL
    if (previous_macd >= previous_signal && current_macd < current_signal) {
        last_signal_ = Signal::SELL;
        return Signal::SELL;
    }
    
    return Signal::NONE;
}

void MACDStrategy::update_macd() {
    if (price_history_.size() < static_cast<size_t>(slow_period_)) {
        return;
    }
    
    // Convert deque to vector for EMA calculation
    std::vector<double> prices(price_history_.begin(), price_history_.end());
    
    // Calculate fast and slow EMAs
    double fast_ema = calculate_ema(prices, fast_period_);
    double slow_ema = calculate_ema(prices, slow_period_);
    
    // Calculate MACD line
    double macd = fast_ema - slow_ema;
    macd_history_.push_back(macd);
    
    // Keep only recent history
    if (macd_history_.size() > 50) {
        macd_history_.pop_front();
    }
    
    // Calculate signal line (EMA of MACD)
    if (macd_history_.size() >= static_cast<size_t>(signal_period_)) {
        std::vector<double> macd_values(macd_history_.end() - signal_period_, macd_history_.end());
        double signal_ema = calculate_ema(macd_values, signal_period_);
        signal_history_.push_back(signal_ema);
        
        if (signal_history_.size() > 50) {
            signal_history_.pop_front();
        }
    }
}

double MACDStrategy::calculate_ema(const std::vector<double>& values, int period) const {
    if (values.empty() || period <= 0) return 0.0;
    
    double multiplier = 2.0 / (period + 1);
    double ema = values[0];
    
    for (size_t i = 1; i < values.size(); ++i) {
        ema = (values[i] * multiplier) + (ema * (1 - multiplier));
    }
    
    return ema;
}

std::unordered_map<std::string, double> MACDStrategy::get_parameters() const {
    return {
        {"fast_period", static_cast<double>(fast_period_)},
        {"slow_period", static_cast<double>(slow_period_)},
        {"signal_period", static_cast<double>(signal_period_)}
    };
}

void MACDStrategy::reset() {
    price_history_.clear();
    macd_history_.clear();
    signal_history_.clear();
    is_ready_ = false;
    last_signal_ = Signal::NONE;
}

bool MACDStrategy::is_ready() const {
    return is_ready_;
}

double MACDStrategy::get_current_macd() const {
    return macd_history_.empty() ? 0.0 : macd_history_.back();
}

double MACDStrategy::get_current_signal() const {
    return signal_history_.empty() ? 0.0 : signal_history_.back();
}

double MACDStrategy::get_previous_macd() const {
    return macd_history_.size() < 2 ? 0.0 : macd_history_[macd_history_.size() - 2];
}

double MACDStrategy::get_previous_signal() const {
    return signal_history_.size() < 2 ? 0.0 : signal_history_[signal_history_.size() - 2];
}

} // namespace backtesting 