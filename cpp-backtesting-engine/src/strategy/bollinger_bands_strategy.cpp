#include "strategy/bollinger_bands_strategy.h"
#include "portfolio/portfolio_manager.h"
#include <numeric>
#include <algorithm>
#include <cmath>

namespace backtesting {

BollingerBandsStrategy::BollingerBandsStrategy(int period, double std_dev)
    : period_(period), std_dev_(std_dev), is_ready_(false), last_signal_(Signal::NONE) {}

void BollingerBandsStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    for (const auto& ohlc : historical_data) {
        update(ohlc);
    }
}

void BollingerBandsStrategy::update(const OHLC& new_data) {
    price_history_.push_back(new_data.close);
    
    // Keep only required history
    if (price_history_.size() > static_cast<size_t>(period_ * 2)) {
        price_history_.pop_front();
    }
    
    update_bands();
    
    // Strategy is ready when we have enough data for bands
    if (price_history_.size() >= static_cast<size_t>(period_)) {
        is_ready_ = true;
    }
}

Signal BollingerBandsStrategy::generate_signal(
    const std::string& symbol,
    const OHLC& current_data,
    const PortfolioManager& portfolio
) {
    (void)symbol;  // Unused parameter
    (void)portfolio;  // Unused parameter
    
    if (!is_ready_ || upper_band_history_.empty() || lower_band_history_.empty()) {
        return Signal::NONE;
    }
    
    double current_price = current_data.close;
    double upper_band = get_current_upper_band();
    double lower_band = get_current_lower_band();
    double middle_band = get_current_middle_band();
    
    // Price touches lower band -> oversold -> BUY signal
    if (current_price <= lower_band && last_signal_ != Signal::BUY) {
        last_signal_ = Signal::BUY;
        return Signal::BUY;
    }
    
    // Price touches upper band -> overbought -> SELL signal
    if (current_price >= upper_band && last_signal_ != Signal::SELL) {
        last_signal_ = Signal::SELL;
        return Signal::SELL;
    }
    
    // Price crosses back to middle band -> potential exit signal
    if (last_signal_ == Signal::BUY && current_price >= middle_band) {
        last_signal_ = Signal::SELL;
        return Signal::SELL;
    }
    
    if (last_signal_ == Signal::SELL && current_price <= middle_band) {
        last_signal_ = Signal::BUY;
        return Signal::BUY;
    }
    
    return Signal::NONE;
}

void BollingerBandsStrategy::update_bands() {
    if (price_history_.size() < static_cast<size_t>(period_)) {
        return;
    }
    
    // Calculate middle band (SMA)
    double middle_band = calculate_sma();
    middle_band_history_.push_back(middle_band);
    
    // Calculate standard deviation
    double std_deviation = calculate_std_deviation();
    
    // Calculate upper and lower bands
    double upper_band = middle_band + (std_dev_ * std_deviation);
    double lower_band = middle_band - (std_dev_ * std_deviation);
    
    upper_band_history_.push_back(upper_band);
    lower_band_history_.push_back(lower_band);
    
    // Keep only recent history
    if (upper_band_history_.size() > 10) {
        upper_band_history_.pop_front();
        lower_band_history_.pop_front();
        middle_band_history_.pop_front();
    }
}

double BollingerBandsStrategy::calculate_sma() const {
    if (price_history_.size() < static_cast<size_t>(period_)) {
        return 0.0;
    }
    
    auto begin = price_history_.end() - period_;
    double sum = std::accumulate(begin, price_history_.end(), 0.0);
    return sum / period_;
}

double BollingerBandsStrategy::calculate_std_deviation() const {
    if (price_history_.size() < static_cast<size_t>(period_)) {
        return 0.0;
    }
    
    double sma = calculate_sma();
    double variance = 0.0;
    
    auto begin = price_history_.end() - period_;
    for (auto it = begin; it != price_history_.end(); ++it) {
        double diff = *it - sma;
        variance += diff * diff;
    }
    
    variance /= period_;
    return std::sqrt(variance);
}

std::unordered_map<std::string, double> BollingerBandsStrategy::get_parameters() const {
    return {
        {"period", static_cast<double>(period_)},
        {"std_dev", std_dev_}
    };
}

void BollingerBandsStrategy::reset() {
    price_history_.clear();
    upper_band_history_.clear();
    lower_band_history_.clear();
    middle_band_history_.clear();
    is_ready_ = false;
    last_signal_ = Signal::NONE;
}

bool BollingerBandsStrategy::is_ready() const {
    return is_ready_;
}

double BollingerBandsStrategy::get_current_upper_band() const {
    return upper_band_history_.empty() ? 0.0 : upper_band_history_.back();
}

double BollingerBandsStrategy::get_current_lower_band() const {
    return lower_band_history_.empty() ? 0.0 : lower_band_history_.back();
}

double BollingerBandsStrategy::get_current_middle_band() const {
    return middle_band_history_.empty() ? 0.0 : middle_band_history_.back();
}

} // namespace backtesting 