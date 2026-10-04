#include "strategy/advanced_strategies.h"
#include "strategy/advanced_strategies.h"
#include "portfolio/portfolio_manager.h"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace backtesting {

// MomentumStrategy implementation
MomentumStrategy::MomentumStrategy(
    int lookback_period,
    int rsi_period,
    int volume_sma_period,
    double rsi_oversold,
    double rsi_overbought,
    double momentum_threshold,
    double volume_factor,
    bool use_volume_confirmation
) : lookback_period_(lookback_period),
    rsi_period_(rsi_period),
    volume_sma_period_(volume_sma_period),
    rsi_oversold_threshold_(rsi_oversold),
    rsi_overbought_threshold_(rsi_overbought),
    momentum_threshold_(momentum_threshold),
    volume_factor_threshold_(volume_factor),
    use_volume_confirmation_(use_volume_confirmation),
    is_initialized_(false),
    last_signal_(Signal::NONE),
    bars_since_signal_(0),
    entry_price_(0.0) {
}

Signal MomentumStrategy::generate_signal(
    [[maybe_unused]] const std::string& symbol,
    const OHLC& current_data,
    [[maybe_unused]] const PortfolioManager& portfolio
) {
    // Update internal data
    update(current_data);
    
    if (!is_ready()) {
        return Signal::NONE;
    }
    
    // Generate signal based on momentum analysis
    Signal signal = determine_signal(current_data);
    
    // Update state
    if (signal != Signal::NONE) {
        last_signal_ = signal;
        bars_since_signal_ = 0;
        entry_price_ = current_data.close;
    } else {
        bars_since_signal_++;
    }
    
    return signal;
}

void MomentumStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    // Initialize with historical data
    for (const auto& data : historical_data) {
        update(data);
    }
    
    is_initialized_ = true;
}

void MomentumStrategy::update(const OHLC& new_data) {
    // Add new data point
    price_history_.push_back(new_data);
    
    // Maintain maximum history size
    int max_history = std::max({lookback_period_, rsi_period_, volume_sma_period_}) + 50;
    if (static_cast<int>(price_history_.size()) > max_history) {
        price_history_.pop_front();
    }
    
    // Calculate technical indicators
    if (static_cast<int>(price_history_.size()) >= rsi_period_) {
        double current_rsi = calculate_rsi();
        rsi_values_.push_back(current_rsi);
        
        if (rsi_values_.size() > 50) {
            rsi_values_.pop_front();
        }
    }
    
    if (static_cast<int>(price_history_.size()) >= lookback_period_) {
        double current_momentum = calculate_price_momentum(lookback_period_);
        momentum_values_.push_back(current_momentum);
        
        if (momentum_values_.size() > 50) {
            momentum_values_.pop_front();
        }
    }
    
    if (static_cast<int>(price_history_.size()) >= volume_sma_period_) {
        double current_volume_sma = calculate_volume_sma();
        volume_sma_values_.push_back(current_volume_sma);
        
        if (volume_sma_values_.size() > 50) {
            volume_sma_values_.pop_front();
        }
    }
}

std::unordered_map<std::string, double> MomentumStrategy::get_parameters() const {
    return {
        {"lookback_period", static_cast<double>(lookback_period_)},
        {"rsi_period", static_cast<double>(rsi_period_)},
        {"volume_sma_period", static_cast<double>(volume_sma_period_)},
        {"rsi_oversold_threshold", rsi_oversold_threshold_},
        {"rsi_overbought_threshold", rsi_overbought_threshold_},
        {"momentum_threshold", momentum_threshold_},
        {"volume_factor_threshold", volume_factor_threshold_},
        {"use_volume_confirmation", use_volume_confirmation_ ? 1.0 : 0.0}
    };
}

void MomentumStrategy::reset() {
    price_history_.clear();
    rsi_values_.clear();
    momentum_values_.clear();
    volume_sma_values_.clear();
    is_initialized_ = false;
    last_signal_ = Signal::NONE;
    bars_since_signal_ = 0;
    entry_price_ = 0.0;
}

bool MomentumStrategy::is_ready() const {
    return is_initialized_ && 
           static_cast<int>(price_history_.size()) >= std::max({lookback_period_, rsi_period_, volume_sma_period_}) &&
           !momentum_values_.empty() && 
           !rsi_values_.empty();
}

double MomentumStrategy::calculate_price_momentum(int period) const {
    if (static_cast<int>(price_history_.size()) < period + 1) {
        return 0.0;
    }
    
    double current_price = price_history_.back().close;
    double past_price = price_history_[price_history_.size() - period - 1].close;
    
    return (current_price - past_price) / past_price;
}

double MomentumStrategy::calculate_volume_momentum(int period) const {
    if (static_cast<int>(price_history_.size()) < period + 1) {
        return 0.0;
    }
    
    double current_volume = static_cast<double>(price_history_.back().volume);
    double past_volume = static_cast<double>(price_history_[price_history_.size() - period - 1].volume);
    
    if (past_volume == 0.0) {
        return 0.0;
    }
    
    return (current_volume - past_volume) / past_volume;
}

double MomentumStrategy::calculate_rsi() const {
    if (static_cast<int>(price_history_.size()) < rsi_period_ + 1) {
        return 50.0; // Neutral RSI
    }
    
    std::vector<double> gains, losses;
    
    for (size_t i = price_history_.size() - rsi_period_; i < price_history_.size(); ++i) {
        double price_change = price_history_[i].close - price_history_[i-1].close;
        if (price_change > 0) {
            gains.push_back(price_change);
            losses.push_back(0.0);
        } else {
            gains.push_back(0.0);
            losses.push_back(-price_change);
        }
    }
    
    double avg_gain = std::accumulate(gains.begin(), gains.end(), 0.0) / gains.size();
    double avg_loss = std::accumulate(losses.begin(), losses.end(), 0.0) / losses.size();
    
    if (avg_loss == 0.0) {
        return 100.0;
    }
    
    double rs = avg_gain / avg_loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

double MomentumStrategy::calculate_volume_sma() const {
    if (static_cast<int>(price_history_.size()) < volume_sma_period_) {
        return 0.0;
    }
    
    double volume_sum = 0.0;
    for (size_t i = price_history_.size() - volume_sma_period_; i < price_history_.size(); ++i) {
        volume_sum += static_cast<double>(price_history_[i].volume);
    }
    
    return volume_sum / volume_sma_period_;
}

bool MomentumStrategy::is_volume_confirming() const {
    if (!use_volume_confirmation_ || volume_sma_values_.empty()) {
        return true; // Skip volume confirmation if not enabled or no data
    }
    
    double current_volume = static_cast<double>(price_history_.back().volume);
    double avg_volume = volume_sma_values_.back();
    
    return current_volume >= (avg_volume * volume_factor_threshold_);
}

bool MomentumStrategy::is_trend_strong() const {
    if (momentum_values_.size() < 3) {
        return false;
    }
    
    // Check if momentum is consistent over last few bars
    int consistent_count = 0;
    double threshold = momentum_threshold_ / 2.0;
    
    for (int i = std::max(0, static_cast<int>(momentum_values_.size()) - 3); 
         i < static_cast<int>(momentum_values_.size()); ++i) {
        if (std::abs(momentum_values_[i]) > threshold) {
            consistent_count++;
        }
    }
    
    return consistent_count >= 2;
}

Signal MomentumStrategy::determine_signal([[maybe_unused]] const OHLC& current_data) {
    if (momentum_values_.empty() || rsi_values_.empty()) {
        return Signal::NONE;
    }
    
    double current_momentum = momentum_values_.back();
    double current_rsi = rsi_values_.back();
    
    // Check for exit conditions first
    if (last_signal_ == Signal::BUY) {
        // Exit long position if momentum turns negative or RSI becomes overbought
        if (current_momentum < -momentum_threshold_ / 2.0 || current_rsi > rsi_overbought_threshold_) {
            return Signal::SELL;
        }
    } else if (last_signal_ == Signal::SELL) {
        // Exit short position if momentum turns positive or RSI becomes oversold
        if (current_momentum > momentum_threshold_ / 2.0 || current_rsi < rsi_oversold_threshold_) {
            return Signal::BUY;
        }
    }
    
    // Check for new entry conditions
    bool volume_confirmed = is_volume_confirming();
    bool trend_strong = is_trend_strong();
    
    // Long signal: Strong positive momentum, RSI not overbought, volume confirmed
    if (current_momentum > momentum_threshold_ && 
        current_rsi < rsi_overbought_threshold_ && 
        current_rsi > 50.0 && // Prefer bullish RSI
        volume_confirmed && 
        trend_strong &&
        last_signal_ != Signal::BUY) {
        return Signal::BUY;
    }
    
    // Short signal: Strong negative momentum, RSI not oversold, volume confirmed
    if (current_momentum < -momentum_threshold_ && 
        current_rsi > rsi_oversold_threshold_ && 
        current_rsi < 50.0 && // Prefer bearish RSI
        volume_confirmed && 
        trend_strong &&
        last_signal_ != Signal::SELL) {
        return Signal::SELL;
    }
    
    return Signal::NONE;
}

double MomentumStrategy::get_current_momentum() const {
    return momentum_values_.empty() ? 0.0 : momentum_values_.back();
}

double MomentumStrategy::get_current_rsi() const {
    return rsi_values_.empty() ? 50.0 : rsi_values_.back();
}

// MeanReversionStrategy implementation
MeanReversionStrategy::MeanReversionStrategy(
    int bollinger_period,
    double bollinger_std_dev,
    int rsi_period,
    double rsi_oversold,
    double rsi_overbought,
    double mean_reversion_threshold,
    int max_hold_period
) : bollinger_period_(bollinger_period),
    bollinger_std_dev_(bollinger_std_dev),
    rsi_period_(rsi_period),
    rsi_oversold_(rsi_oversold),
    rsi_overbought_(rsi_overbought),
    mean_reversion_threshold_(mean_reversion_threshold),
    max_hold_period_(max_hold_period),
    is_initialized_(false),
    current_position_(Signal::NONE),
    bars_in_position_(0),
    entry_price_(0.0) {
}

Signal MeanReversionStrategy::generate_signal(
    const std::string& symbol,
    const OHLC& current_data,
    const PortfolioManager& portfolio
) {
    update(current_data);
    
    if (!is_ready()) {
        return Signal::NONE;
    }
    
    Signal signal = determine_mean_reversion_signal(current_data);

    // Use portfolio state to avoid emitting signals that are not actionable.
    // This makes strategy outputs consistent for consumers integrating the engine as a library.
    const auto pos = portfolio.get_position(symbol);
    if (signal == Signal::BUY && pos.is_open()) {
        signal = Signal::NONE;
    } else if (signal == Signal::SELL && !pos.is_open()) {
        signal = Signal::NONE;
    }
    
    // Update position tracking
    if (signal != Signal::NONE) {
        if (signal != current_position_) {
            current_position_ = signal;
            bars_in_position_ = 0;
            entry_price_ = current_data.close;
        }
    } else if (current_position_ != Signal::NONE) {
        bars_in_position_++;
    }
    
    return signal;
}

void MeanReversionStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    for (const auto& data : historical_data) {
        update(data);
    }
    
    is_initialized_ = true;
}

void MeanReversionStrategy::update(const OHLC& new_data) {
    // Add new data point
    price_history_.push_back(new_data);
    
    // Maintain maximum history size
    int max_history = std::max({bollinger_period_, rsi_period_}) + 50;
    if (static_cast<int>(price_history_.size()) > max_history) {
        price_history_.pop_front();
    }
    
    if (static_cast<int>(price_history_.size()) >= bollinger_period_) {
        calculate_bollinger_bands();
    }
    
    if (static_cast<int>(price_history_.size()) >= rsi_period_) {
        double current_rsi = calculate_current_rsi();
        rsi_values_.push_back(current_rsi);
        
        if (rsi_values_.size() > 50) {
            rsi_values_.pop_front();
        }
    }
}

std::unordered_map<std::string, double> MeanReversionStrategy::get_parameters() const {
    return {
        {"bollinger_period", static_cast<double>(bollinger_period_)},
        {"bollinger_std_dev", bollinger_std_dev_},
        {"rsi_period", static_cast<double>(rsi_period_)},
        {"rsi_oversold", rsi_oversold_},
        {"rsi_overbought", rsi_overbought_},
        {"mean_reversion_threshold", mean_reversion_threshold_},
        {"max_hold_period", static_cast<double>(max_hold_period_)}
    };
}

void MeanReversionStrategy::reset() {
    price_history_.clear();
    bollinger_upper_.clear();
    bollinger_lower_.clear();
    bollinger_middle_.clear();
    rsi_values_.clear();
    is_initialized_ = false;
    current_position_ = Signal::NONE;
    bars_in_position_ = 0;
    entry_price_ = 0.0;
}

bool MeanReversionStrategy::is_ready() const {
    return is_initialized_ && 
           static_cast<int>(price_history_.size()) >= std::max(bollinger_period_, rsi_period_) &&
           !rsi_values_.empty() && 
           !bollinger_upper_.empty() && !bollinger_lower_.empty();
}

void MeanReversionStrategy::calculate_bollinger_bands() {
    if (price_history_.size() < static_cast<size_t>(bollinger_period_)) {
        return;
    }
    
    // Calculate SMA and standard deviation
    double sum = 0.0;
    for (size_t i = price_history_.size() - bollinger_period_; i < price_history_.size(); ++i) {
        sum += price_history_[i].close;
    }
    double sma = sum / bollinger_period_;
    
    double variance = 0.0;
    for (size_t i = price_history_.size() - bollinger_period_; i < price_history_.size(); ++i) {
        double diff = price_history_[i].close - sma;
        variance += diff * diff;
    }
    double std_dev = std::sqrt(variance / bollinger_period_);
    
    // Calculate bands
    double upper = sma + (bollinger_std_dev_ * std_dev);
    double lower = sma - (bollinger_std_dev_ * std_dev);
    
    bollinger_upper_.push_back(upper);
    bollinger_lower_.push_back(lower);
    bollinger_middle_.push_back(sma);
    
    // Maintain size
    if (bollinger_upper_.size() > 50) {
        bollinger_upper_.pop_front();
        bollinger_lower_.pop_front();
        bollinger_middle_.pop_front();
    }
}

double MeanReversionStrategy::calculate_current_rsi() const {
    if (price_history_.size() < static_cast<size_t>(rsi_period_ + 1)) {
        return 50.0;
    }
    
    std::vector<double> gains, losses;
    
    for (size_t i = price_history_.size() - rsi_period_; i < price_history_.size(); ++i) {
        double price_change = price_history_[i].close - price_history_[i-1].close;
        if (price_change > 0) {
            gains.push_back(price_change);
            losses.push_back(0.0);
        } else {
            gains.push_back(0.0);
            losses.push_back(-price_change);
        }
    }
    
    double avg_gain = std::accumulate(gains.begin(), gains.end(), 0.0) / gains.size();
    double avg_loss = std::accumulate(losses.begin(), losses.end(), 0.0) / losses.size();
    
    if (avg_loss == 0.0) {
        return 100.0;
    }
    
    double rs = avg_gain / avg_loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

bool MeanReversionStrategy::is_oversold() const {
    return !rsi_values_.empty() && rsi_values_.back() < rsi_oversold_;
}

bool MeanReversionStrategy::is_overbought() const {
    return !rsi_values_.empty() && rsi_values_.back() > rsi_overbought_;
}

bool MeanReversionStrategy::is_near_bollinger_bands() const {
    if (bollinger_upper_.empty() || bollinger_lower_.empty() || price_history_.empty()) {
        return false;
    }
    
    double current_price = price_history_.back().close;
    double upper_band = bollinger_upper_.back();
    double lower_band = bollinger_lower_.back();
    double middle_band = bollinger_middle_.back();
    
    // Check if price is near the bands (within threshold)
    double upper_distance = std::abs(current_price - upper_band) / middle_band;
    double lower_distance = std::abs(current_price - lower_band) / middle_band;
    
    return (upper_distance < mean_reversion_threshold_) || (lower_distance < mean_reversion_threshold_);
}

Signal MeanReversionStrategy::determine_mean_reversion_signal(const OHLC& current_data) {
    if (bollinger_upper_.empty() || rsi_values_.empty()) {
        return Signal::NONE;
    }
    
    double current_price = current_data.close;
    double upper_band = bollinger_upper_.back();
    double lower_band = bollinger_lower_.back();
    double middle_band = bollinger_middle_.back();
    
    // Exit conditions - check for mean reversion
    if (current_position_ == Signal::BUY) {
        // Exit long if price approaches middle band from below or max hold period reached
        if (current_price >= middle_band * (1.0 - mean_reversion_threshold_) || 
            bars_in_position_ >= max_hold_period_) {
            return Signal::SELL;
        }
    } else if (current_position_ == Signal::SELL) {
        // Exit short if price approaches middle band from above or max hold period reached
        if (current_price <= middle_band * (1.0 + mean_reversion_threshold_) || 
            bars_in_position_ >= max_hold_period_) {
            return Signal::BUY;
        }
    }
    
    // Entry conditions - look for oversold/overbought with bollinger band confirmation
    if (current_position_ == Signal::NONE) {
        // Long signal: Price near lower band and RSI oversold
        if (current_price <= lower_band * (1.0 + mean_reversion_threshold_) && is_oversold()) {
            return Signal::BUY;
        }
        
        // Short signal: Price near upper band and RSI overbought
        if (current_price >= upper_band * (1.0 - mean_reversion_threshold_) && is_overbought()) {
            return Signal::SELL;
        }
    }
    
    return Signal::NONE;
}

double MeanReversionStrategy::get_current_bollinger_position() const {
    if (bollinger_upper_.empty() || bollinger_lower_.empty() || price_history_.empty()) {
        return 0.5; // Middle position
    }
    
    double current_price = price_history_.back().close;
    double upper_band = bollinger_upper_.back();
    double lower_band = bollinger_lower_.back();
    
    // Return position as percentage (0 = lower band, 1 = upper band)
    return (current_price - lower_band) / (upper_band - lower_band);
}

bool MeanReversionStrategy::is_in_oversold_territory() const {
    return is_oversold() && get_current_bollinger_position() < 0.2;
}

bool MeanReversionStrategy::is_in_overbought_territory() const {
    return is_overbought() && get_current_bollinger_position() > 0.8;
}

// BreakoutStrategy implementation
BreakoutStrategy::BreakoutStrategy(
    int lookback_period,
    double breakout_threshold,
    int volume_confirmation_period,
    double volume_multiplier,
    int atr_period,
    double atr_multiplier,
    bool use_dynamic_stops
) : lookback_period_(lookback_period),
    breakout_threshold_(breakout_threshold),
    volume_confirmation_period_(volume_confirmation_period),
    volume_multiplier_(volume_multiplier),
    atr_period_(atr_period),
    atr_multiplier_(atr_multiplier),
    use_dynamic_stops_(use_dynamic_stops),
    resistance_level_(0.0),
    support_level_(0.0),
    breakout_confirmed_(false),
    is_initialized_(false),
    current_direction_(Signal::NONE),
    entry_price_(0.0),
    stop_loss_level_(0.0),
    take_profit_level_(0.0) {
}

Signal BreakoutStrategy::generate_signal(
    [[maybe_unused]] const std::string& symbol,
    const OHLC& current_data,
    [[maybe_unused]] const PortfolioManager& portfolio
) {
    update(current_data);
    
    if (!is_ready()) {
        return Signal::NONE;
    }
    
    return determine_breakout_signal(current_data);
}

void BreakoutStrategy::initialize(const std::vector<OHLC>& historical_data) {
    reset();
    
    for (const auto& data : historical_data) {
        update(data);
    }
    
    is_initialized_ = true;
}

void BreakoutStrategy::update(const OHLC& new_data) {
    // Add new data point
    price_history_.push_back(new_data);
    
    // Maintain maximum history size  
    int max_history = std::max({lookback_period_, volume_confirmation_period_, atr_period_}) + 50;
    if (static_cast<int>(price_history_.size()) > max_history) {
        price_history_.pop_front();
    }
    
    if (static_cast<int>(price_history_.size()) >= lookback_period_) {
        calculate_breakout_levels();
    }
    
    if (static_cast<int>(price_history_.size()) >= volume_confirmation_period_) {
        double current_avg_volume = calculate_volume_average();
        volume_sma_.push_back(current_avg_volume);
        
        if (volume_sma_.size() > 50) {
            volume_sma_.pop_front();
        }
    }
    
    if (static_cast<int>(price_history_.size()) >= atr_period_) {
        double current_atr = calculate_atr();
        atr_values_.push_back(current_atr);
        
        if (atr_values_.size() > 50) {
            atr_values_.pop_front();
        }
    }
}

std::unordered_map<std::string, double> BreakoutStrategy::get_parameters() const {
    return {
        {"lookback_period", static_cast<double>(lookback_period_)},
        {"breakout_threshold", breakout_threshold_},
        {"volume_confirmation_period", static_cast<double>(volume_confirmation_period_)},
        {"volume_multiplier", volume_multiplier_},
        {"atr_period", static_cast<double>(atr_period_)},
        {"atr_multiplier", atr_multiplier_},
        {"use_dynamic_stops", use_dynamic_stops_ ? 1.0 : 0.0}
    };
}

void BreakoutStrategy::reset() {
    price_history_.clear();
    volume_sma_.clear();
    atr_values_.clear();
    resistance_level_ = 0.0;
    support_level_ = 0.0;
    breakout_confirmed_ = false;
    is_initialized_ = false;
    current_direction_ = Signal::NONE;
    entry_price_ = 0.0;
    stop_loss_level_ = 0.0;
    take_profit_level_ = 0.0;
}

bool BreakoutStrategy::is_ready() const {
    return is_initialized_ && 
           static_cast<int>(price_history_.size()) >= std::max({lookback_period_, volume_confirmation_period_, atr_period_}) &&
           !volume_sma_.empty() && !atr_values_.empty();
}

void BreakoutStrategy::calculate_breakout_levels() {
    if (static_cast<int>(price_history_.size()) < lookback_period_) {
        return;
    }
    
    double max_high = price_history_[price_history_.size() - lookback_period_].high;
    double min_low = price_history_[price_history_.size() - lookback_period_].low;
    
    for (size_t i = price_history_.size() - lookback_period_ + 1; i < price_history_.size(); ++i) {
        max_high = std::max(max_high, price_history_[i].high);
        min_low = std::min(min_low, price_history_[i].low);
    }
    
    resistance_level_ = max_high;
    support_level_ = min_low;
}

double BreakoutStrategy::calculate_atr() const {
    if (static_cast<int>(price_history_.size()) < atr_period_ + 1) {
        return 0.0;
    }
    
    std::vector<double> true_ranges;
    
    for (size_t i = price_history_.size() - atr_period_; i < price_history_.size(); ++i) {
        double high_low = price_history_[i].high - price_history_[i].low;
        double high_close = std::abs(price_history_[i].high - price_history_[i-1].close);
        double low_close = std::abs(price_history_[i].low - price_history_[i-1].close);
        
        double true_range = std::max({high_low, high_close, low_close});
        true_ranges.push_back(true_range);
    }
    
    return std::accumulate(true_ranges.begin(), true_ranges.end(), 0.0) / true_ranges.size();
}

double BreakoutStrategy::calculate_volume_average() const {
    if (static_cast<int>(price_history_.size()) < volume_confirmation_period_) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (size_t i = price_history_.size() - volume_confirmation_period_; i < price_history_.size(); ++i) {
        sum += static_cast<double>(price_history_[i].volume);
    }
    
    return sum / volume_confirmation_period_;
}

bool BreakoutStrategy::is_volume_confirming(const OHLC& current_data) const {
    if (volume_sma_.empty()) {
        return true; // No volume confirmation if no data
    }
    
    double current_volume = static_cast<double>(current_data.volume);
    double avg_volume = volume_sma_.back();
    
    return current_volume >= (avg_volume * volume_multiplier_);
}

bool BreakoutStrategy::is_price_breaking_out(const OHLC& current_data) const {
    if (resistance_level_ == 0.0 || support_level_ == 0.0) {
        return false;
    }
    
    double range = resistance_level_ - support_level_;
    double breakout_buffer = range * breakout_threshold_;
    
    // Check for upward breakout
    if (current_data.close > resistance_level_ + breakout_buffer) {
        return true;
    }
    
    // Check for downward breakout
    if (current_data.close < support_level_ - breakout_buffer) {
        return true;
    }
    
    return false;
}

void BreakoutStrategy::update_stop_levels(const OHLC& current_data) {
    if (!use_dynamic_stops_ || atr_values_.empty()) {
        return;
    }
    
    double atr = atr_values_.back();
    
    if (current_direction_ == Signal::BUY) {
        double new_stop = current_data.close - (atr * atr_multiplier_);
        stop_loss_level_ = std::max(stop_loss_level_, new_stop); // Trailing stop
        take_profit_level_ = entry_price_ + (atr * atr_multiplier_ * 2.0);
    } else if (current_direction_ == Signal::SELL) {
        double new_stop = current_data.close + (atr * atr_multiplier_);
        stop_loss_level_ = std::min(stop_loss_level_, new_stop); // Trailing stop
        take_profit_level_ = entry_price_ - (atr * atr_multiplier_ * 2.0);
    }
}

Signal BreakoutStrategy::determine_breakout_signal(const OHLC& current_data) {
    if (resistance_level_ == 0.0 || support_level_ == 0.0) {
        return Signal::NONE;
    }
    
    // Exit conditions
    if (current_direction_ != Signal::NONE) {
        update_stop_levels(current_data);
        
        if (current_direction_ == Signal::BUY) {
            if (current_data.close <= stop_loss_level_ || current_data.close >= take_profit_level_) {
                current_direction_ = Signal::NONE;
                return Signal::SELL;
            }
        } else if (current_direction_ == Signal::SELL) {
            if (current_data.close >= stop_loss_level_ || current_data.close <= take_profit_level_) {
                current_direction_ = Signal::NONE;
                return Signal::BUY;
            }
        }
    }
    
    // Entry conditions
    if (current_direction_ == Signal::NONE && is_price_breaking_out(current_data)) {
        bool volume_confirmed = is_volume_confirming(current_data);
        
        if (!volume_confirmed) {
            return Signal::NONE;
        }
        
        double range = resistance_level_ - support_level_;
        double breakout_buffer = range * breakout_threshold_;
        
        // Upward breakout
        if (current_data.close > resistance_level_ + breakout_buffer) {
            current_direction_ = Signal::BUY;
            entry_price_ = current_data.close;
            
            if (use_dynamic_stops_ && !atr_values_.empty()) {
                double atr = atr_values_.back();
                stop_loss_level_ = current_data.close - (atr * atr_multiplier_);
                take_profit_level_ = current_data.close + (atr * atr_multiplier_ * 2.0);
            }
            
            breakout_confirmed_ = true;
            return Signal::BUY;
        }
        
        // Downward breakout
        if (current_data.close < support_level_ - breakout_buffer) {
            current_direction_ = Signal::SELL;
            entry_price_ = current_data.close;
            
            if (use_dynamic_stops_ && !atr_values_.empty()) {
                double atr = atr_values_.back();
                stop_loss_level_ = current_data.close + (atr * atr_multiplier_);
                take_profit_level_ = current_data.close - (atr * atr_multiplier_ * 2.0);
            }
            
            breakout_confirmed_ = true;
            return Signal::SELL;
        }
    }
    
    return Signal::NONE;
}

double BreakoutStrategy::get_current_atr() const {
    return atr_values_.empty() ? 0.0 : atr_values_.back();
}

} // namespace backtesting 