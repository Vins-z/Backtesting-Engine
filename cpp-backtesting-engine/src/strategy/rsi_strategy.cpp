#include "strategy/rsi_strategy.h"
#include "portfolio/portfolio_manager.h"
#include <numeric>
#include <algorithm>
#include <cmath>

namespace backtesting {

RSIStrategy::RSIStrategy(int period, double oversold, double overbought)
    : period_(period), oversold_threshold_(oversold), overbought_threshold_(overbought), 
      is_ready_(false), last_signal_(Signal::NONE) {}

void RSIStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    for (const auto& ohlc : historical_data) {
        update(ohlc);
    }
}

void RSIStrategy::update(const OHLC& new_data) {
    price_history_.push_back(new_data.close);
    
    // Keep only required history
    if (price_history_.size() > static_cast<size_t>(period_ * 2)) {
        price_history_.pop_front();
    }
    
    update_rsi();
    
    // Strategy is ready when we have enough data for RSI
    if (price_history_.size() >= static_cast<size_t>(period_ + 1)) {
        is_ready_ = true;
    }
}

Signal RSIStrategy::generate_signal(
    const std::string& symbol,
    const OHLC& current_data,
    const PortfolioManager& portfolio
) {
    (void)symbol;  // Unused parameter
    (void)current_data;  // Unused parameter
    (void)portfolio;  // Unused parameter
    
    if (!is_ready_ || rsi_history_.empty()) {
        return Signal::NONE;
    }
    
    double current_rsi = calculate_current_rsi();
    
    // RSI oversold condition -> BUY signal
    if (current_rsi < oversold_threshold_ && last_signal_ != Signal::BUY) {
        last_signal_ = Signal::BUY;
        return Signal::BUY;
    }
    
    // RSI overbought condition -> SELL signal  
    if (current_rsi > overbought_threshold_ && last_signal_ != Signal::SELL) {
        last_signal_ = Signal::SELL;
        return Signal::SELL;
    }
    
    return Signal::NONE;
}

void RSIStrategy::update_rsi() {
    if (price_history_.size() < static_cast<size_t>(period_ + 1)) {
        return;
    }
    
    // Calculate gains and losses
    std::vector<double> gains, losses;
    
    for (size_t i = 1; i < price_history_.size(); ++i) {
        double change = price_history_[i] - price_history_[i-1];
        gains.push_back(std::max(change, 0.0));
        losses.push_back(std::max(-change, 0.0));
    }
    
    if (gains.size() < static_cast<size_t>(period_)) {
        return;
    }
    
    // Calculate average gains and losses for the period
    double avg_gain = 0.0;
    double avg_loss = 0.0;
    
    for (size_t i = gains.size() - period_; i < gains.size(); ++i) {
        avg_gain += gains[i];
        avg_loss += losses[i];
    }
    
    avg_gain /= period_;
    avg_loss /= period_;
    
    // Calculate RSI
    double rsi = 0.0;
    if (avg_loss != 0.0) {
        double rs = avg_gain / avg_loss;
        rsi = 100.0 - (100.0 / (1.0 + rs));
    } else {
        rsi = 100.0;
    }
    
    rsi_history_.push_back(rsi);
    
    // Keep only recent history
    if (rsi_history_.size() > 10) {
        rsi_history_.pop_front();
    }
}

std::unordered_map<std::string, double> RSIStrategy::get_parameters() const {
    return {
        {"period", static_cast<double>(period_)},
        {"oversold_threshold", oversold_threshold_},
        {"overbought_threshold", overbought_threshold_}
    };
}

void RSIStrategy::reset() {
    price_history_.clear();
    rsi_history_.clear();
    is_ready_ = false;
    last_signal_ = Signal::NONE;
}

bool RSIStrategy::is_ready() const {
    return is_ready_;
}

double RSIStrategy::calculate_current_rsi() const {
    return rsi_history_.empty() ? 50.0 : rsi_history_.back();
}

} // namespace backtesting 