#include "strategy/multi_leg_strategy.h"
#include "portfolio/portfolio_manager.h"
#include <numeric>

namespace backtesting {

MultiLegStrategy::MultiLegStrategy(const std::vector<StrategyLeg>& legs)
    : legs_(legs) {}

void MultiLegStrategy::initialize(const std::vector<OHLC>& historical_data) {
    history_ = historical_data;
    is_ready_ = !history_.empty();
}

void MultiLegStrategy::set_indicators(const std::vector<std::pair<std::string, int>>& indicators) {
    indicators_.clear();
    for (const auto& [type, length] : indicators) {
        IndicatorConfig config;
        config.type = type;
        config.length = length;
        config.param1 = 0.0;
        config.param2 = 0.0;
        config.param3 = 0.0;
        config.weight = 1.0;
        indicators_.push_back(config);
    }
}

void MultiLegStrategy::set_indicators_extended(const std::vector<IndicatorConfig>& indicators) {
    indicators_ = indicators;
    indicator_cache_.clear();
    indicator_cache_pair_.clear();
}

void MultiLegStrategy::update(const OHLC& new_data) {
    history_.push_back(new_data);
    if (history_.size() > 1000) { // Keep history manageable
        history_.erase(history_.begin());
    }
}

Signal MultiLegStrategy::generate_signal(
    const std::string& symbol,
    const OHLC& current_data,
    const PortfolioManager& portfolio
) {
    // Enhanced multi-leg strategy with indicator-based signal generation
    // Find the leg for this symbol
    const StrategyLeg* target_leg = nullptr;
    for (const auto& leg : legs_) {
        if (leg.symbol == symbol) {
            target_leg = &leg;
            break;
        }
    }
    
    if (!target_leg) {
        // This symbol is not part of the multi-leg strategy
        return Signal::NONE;
    }
    
    // Check current position for this symbol
    const auto& position = portfolio.get_position(symbol);
    bool has_position = (position.quantity != 0);
    
    // Evaluate indicators to get signal direction
    Signal indicator_signal = evaluate_indicators(symbol, current_data);
    
    // For entry: Check if ALL legs can be entered (none have positions) AND indicators agree
    // For exit: Check if ANY leg should exit (position exists and exit conditions met)
    
    if (!has_position) {
        // Entry logic: Verify all other legs also don't have positions
        // This ensures coordinated entry across all legs
        bool all_legs_ready = true;
        for (const auto& leg : legs_) {
            const auto& leg_position = portfolio.get_position(leg.symbol);
            if (leg_position.quantity != 0) {
                // Another leg already has a position - don't enter this leg yet
                // This prevents partial entries
                all_legs_ready = false;
                break;
            }
        }
        
        if (all_legs_ready && indicator_signal != Signal::NONE) {
            // All legs are ready for entry AND indicators generate a signal
            // Use indicator signal if it matches leg direction, otherwise use leg direction
            if (indicator_signal == Signal::BUY && target_leg->direction == "BUY") {
                return Signal::BUY;
            } else if (indicator_signal == Signal::SELL && target_leg->direction == "SELL") {
                return Signal::SELL;
            }
            // If indicators don't match leg direction, don't enter (wait for alignment)
        }
    } else {
        // Exit logic: Check if we should exit this position
        // Exit if indicators reverse or if stop loss/take profit conditions are met
        
        // Check if position direction matches leg direction
        bool position_matches_leg = (target_leg->direction == "BUY" && position.quantity > 0) ||
                                    (target_leg->direction == "SELL" && position.quantity < 0);
        
        if (!position_matches_leg) {
            // Position direction doesn't match leg - exit current position
            return (position.quantity > 0) ? Signal::SELL : Signal::BUY;
        }
        
        // Exit if indicators reverse (opposite signal)
        if (indicator_signal != Signal::NONE) {
            if (position.quantity > 0 && indicator_signal == Signal::SELL) {
                return Signal::SELL; // Exit long position
            } else if (position.quantity < 0 && indicator_signal == Signal::BUY) {
                return Signal::BUY; // Exit short position
            }
        }
        
        // Check if any other leg has exited - if so, exit all legs (coordinated exit)
        bool any_leg_exited = false;
        for (const auto& leg : legs_) {
            const auto& leg_position = portfolio.get_position(leg.symbol);
            if (leg_position.quantity == 0 && leg.symbol != symbol) {
                // Another leg has exited - check if it previously had a position
                // This is a simplified check - in practice, you'd track entry state
                any_leg_exited = true;
                break;
            }
        }
        
        if (any_leg_exited) {
            // Coordinate exit: exit this leg when another leg exits
            return (position.quantity > 0) ? Signal::SELL : Signal::BUY;
        }
    }
    
    return Signal::NONE;
}

Signal MultiLegStrategy::evaluate_indicators(const std::string& symbol, const OHLC& current_data) const {
    (void)symbol; // Currently unused: evaluation is based on internal indicator configs + history
    // Require at least one indicator and enough history for the longest period (e.g. SMA 26 ~ 30 bars)
    const size_t min_bars = 30;
    if (indicators_.empty() || history_.size() < min_bars) {
        // No indicators configured or insufficient data - return neutral
        return Signal::NONE;
    }
    
    // Create extended history with current data for calculations
    std::vector<OHLC> extended_history = history_;
    extended_history.push_back(current_data);
    
    // Evaluate each indicator with weighted signals
    double bullish_weight = 0.0;
    double bearish_weight = 0.0;
    int trend_bullish = 0;
    int trend_bearish = 0;
    
    double current_price = current_data.close;
    
    for (const auto& indicator : indicators_) {
        double weight = indicator.weight > 0.0 ? indicator.weight : 1.0;
        
        // Trend indicators (SMA, EMA, MACD)
        if (indicator.type == "SMA" || indicator.type == "EMA") {
            double value = get_indicator_value(indicator, extended_history);
            if (value > 0.0) {
                if (current_price > value) {
                    bullish_weight += weight;
                    trend_bullish++;
                } else {
                    bearish_weight += weight;
                    trend_bearish++;
                }
            }
        } else if (indicator.type == "MACD") {
            double signal_line, histogram;
            double macd_line = Strategy::calculate_macd(
                Strategy::extract_close_prices(extended_history),
                indicator.param1 > 0 ? static_cast<int>(indicator.param1) : 12,
                indicator.param2 > 0 ? static_cast<int>(indicator.param2) : 26,
                indicator.param3 > 0 ? static_cast<int>(indicator.param3) : 9,
                signal_line, histogram
            );
            if (macd_line > signal_line && histogram > 0) {
                bullish_weight += weight * 1.5; // MACD crossovers are strong signals
                trend_bullish++;
            } else if (macd_line < signal_line && histogram < 0) {
                bearish_weight += weight * 1.5;
                trend_bearish++;
            }
        }
        // Momentum indicators (RSI, Stochastic, Williams %R, CCI)
        else if (indicator.type == "RSI") {
            double value = get_indicator_value(indicator, extended_history);
            if (value > 0.0) {
                if (value < 30.0) {
                    bullish_weight += weight * 1.2; // Oversold - strong buy signal
                } else if (value > 70.0) {
                    bearish_weight += weight * 1.2; // Overbought - strong sell signal
                } else if (value < 50.0) {
                    bullish_weight += weight * 0.5; // Below neutral
                } else {
                    bearish_weight += weight * 0.5; // Above neutral
                }
            }
        } else if (indicator.type == "STOCHASTIC") {
            double d_value;
            double k_value = Strategy::calculate_stochastic(
                extended_history,
                indicator.length > 0 ? indicator.length : 14,
                indicator.param1 > 0 ? static_cast<int>(indicator.param1) : 3,
                d_value
            );
            if (k_value < 20.0 && d_value < 20.0) {
                bullish_weight += weight * 1.2; // Oversold
            } else if (k_value > 80.0 && d_value > 80.0) {
                bearish_weight += weight * 1.2; // Overbought
            }
        } else if (indicator.type == "WILLIAMS_R") {
            double value = Strategy::calculate_williams_r(
                extended_history,
                indicator.length > 0 ? indicator.length : 14
            );
            if (value < -80.0) {
                bullish_weight += weight * 1.2; // Oversold
            } else if (value > -20.0) {
                bearish_weight += weight * 1.2; // Overbought
            }
        } else if (indicator.type == "CCI") {
            double value = Strategy::calculate_cci(
                extended_history,
                indicator.length > 0 ? indicator.length : 20
            );
            if (value < -100.0) {
                bullish_weight += weight * 1.2; // Oversold
            } else if (value > 100.0) {
                bearish_weight += weight * 1.2; // Overbought
            }
        }
        // Volatility indicators (Bollinger Bands, ATR)
        else if (indicator.type == "BOLLINGER") {
            double upper_band, lower_band;
            double middle = Strategy::calculate_bollinger_bands(
                Strategy::extract_close_prices(extended_history),
                indicator.length > 0 ? indicator.length : 20,
                indicator.param1 > 0.0 ? indicator.param1 : 2.0,
                upper_band, lower_band
            );
            if (current_price < lower_band) {
                bullish_weight += weight * 1.3; // Price at lower band - potential bounce
            } else if (current_price > upper_band) {
                bearish_weight += weight * 1.3; // Price at upper band - potential reversal
            } else if (current_price > middle) {
                bullish_weight += weight * 0.3; // Above middle band
            } else {
                bearish_weight += weight * 0.3; // Below middle band
            }
        } else if (indicator.type == "ATR") {
            double atr = Strategy::calculate_atr(
                extended_history,
                indicator.length > 0 ? indicator.length : 14
            );
            // ATR is used for position sizing, not direct signals
            // But high ATR can indicate volatility - use as filter
            if (atr > 0.0) {
                // High volatility might suggest caution
                // This is more of a filter than a signal generator
            }
        }
        // Volume indicators (OBV, MFI)
        else if (indicator.type == "OBV") {
            double obv = Strategy::calculate_obv(extended_history);
            // Compare current OBV with previous OBV trend
            if (extended_history.size() >= 2) {
                std::vector<OHLC> prev_history(extended_history.begin(), extended_history.end() - 1);
                double prev_obv = Strategy::calculate_obv(prev_history);
                if (obv > prev_obv) {
                    bullish_weight += weight * 0.8; // Increasing volume on up moves
                } else {
                    bearish_weight += weight * 0.8; // Decreasing volume
                }
            }
        } else if (indicator.type == "MFI") {
            double mfi = Strategy::calculate_mfi(
                extended_history,
                indicator.length > 0 ? indicator.length : 14
            );
            if (mfi < 20.0) {
                bullish_weight += weight * 1.1; // Oversold
            } else if (mfi > 80.0) {
                bearish_weight += weight * 1.1; // Overbought
            }
        }
    }
    
    // Sophisticated signal generation with category confirmation
    // Require at least one trend indicator to agree
    bool trend_confirmed = (trend_bullish > 0 || trend_bearish > 0);
    
    // Calculate signal strength
    double total_weight = bullish_weight + bearish_weight;
    double signal_strength = calculate_signal_strength(
        static_cast<int>(bullish_weight),
        static_cast<int>(bearish_weight),
        total_weight
    );
    
    // Generate signal based on weighted consensus
    // Require minimum signal strength and trend confirmation
    if (trend_confirmed && signal_strength > 0.3) {
        if (bullish_weight > bearish_weight * 1.2) { // 20% stronger bullish signal
            return Signal::BUY;
        } else if (bearish_weight > bullish_weight * 1.2) { // 20% stronger bearish signal
            return Signal::SELL;
        }
    }
    
    return Signal::NONE;
}

double MultiLegStrategy::calculate_signal_strength(int bullish_count, int bearish_count, double total_weight) const {
    if (total_weight == 0.0) return 0.0;
    
    double max_weight = std::max(bullish_count, bearish_count);
    return max_weight / total_weight;
}

double MultiLegStrategy::get_indicator_value(const IndicatorConfig& indicator, const std::vector<OHLC>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    // Check cache first
    std::string cache_key = indicator.type + "_" + std::to_string(indicator.length) + "_" + 
                           std::to_string(indicator.param1) + "_" + std::to_string(indicator.param2);
    auto cache_it = indicator_cache_.find(cache_key);
    if (cache_it != indicator_cache_.end()) {
        return cache_it->second;
    }
    
    double result = 0.0;
    auto closes = Strategy::extract_close_prices(data);
    
    if (indicator.type == "SMA") {
        if (data.size() >= static_cast<size_t>(indicator.length)) {
            result = Strategy::calculate_sma(closes, indicator.length);
        }
    } else if (indicator.type == "EMA") {
        if (data.size() >= static_cast<size_t>(indicator.length)) {
            result = Strategy::calculate_ema(closes, indicator.length);
        }
    } else if (indicator.type == "RSI") {
        if (data.size() >= static_cast<size_t>(indicator.length + 1)) {
            result = Strategy::calculate_rsi(closes, indicator.length);
        }
    } else if (indicator.type == "WILLIAMS_R") {
        if (data.size() >= static_cast<size_t>(indicator.length)) {
            result = Strategy::calculate_williams_r(data, indicator.length);
        }
    } else if (indicator.type == "CCI") {
        if (data.size() >= static_cast<size_t>(indicator.length)) {
            result = Strategy::calculate_cci(data, indicator.length);
        }
    } else if (indicator.type == "OBV") {
        if (data.size() >= 2) {
            result = Strategy::calculate_obv(data);
        }
    } else if (indicator.type == "MFI") {
        if (data.size() >= static_cast<size_t>(indicator.length + 1)) {
            result = Strategy::calculate_mfi(data, indicator.length);
        }
    } else if (indicator.type == "ATR") {
        if (data.size() >= static_cast<size_t>(indicator.length + 1)) {
            result = Strategy::calculate_atr(data, indicator.length);
        }
    }
    
    // Cache result
    if (result != 0.0) {
        indicator_cache_[cache_key] = result;
    }
    
    return result;
}

std::pair<double, double> MultiLegStrategy::get_indicator_pair_value(const IndicatorConfig& indicator, const std::vector<OHLC>& data) const {
    std::pair<double, double> result(0.0, 0.0);
    
    if (data.empty()) {
        return result;
    }
    
    std::string cache_key = indicator.type + "_pair_" + std::to_string(indicator.length);
    auto cache_it = indicator_cache_pair_.find(cache_key);
    if (cache_it != indicator_cache_pair_.end()) {
        return cache_it->second;
    }
    
    if (indicator.type == "STOCHASTIC") {
        if (data.size() >= static_cast<size_t>(indicator.length + indicator.param1)) {
            double d_value;
            double k_value = Strategy::calculate_stochastic(
                data,
                indicator.length > 0 ? indicator.length : 14,
                indicator.param1 > 0 ? static_cast<int>(indicator.param1) : 3,
                d_value
            );
            result = std::make_pair(k_value, d_value);
        }
    } else if (indicator.type == "BOLLINGER") {
        if (data.size() >= static_cast<size_t>(indicator.length)) {
            double upper, lower;
            Strategy::calculate_bollinger_bands(
                Strategy::extract_close_prices(data),
                indicator.length > 0 ? indicator.length : 20,
                indicator.param1 > 0.0 ? indicator.param1 : 2.0,
                upper, lower
            );
            result = std::make_pair(upper, lower);
        }
    } else if (indicator.type == "MACD") {
        if (data.size() >= static_cast<size_t>(indicator.param2 + indicator.param3)) {
            double signal_line, histogram;
            double macd_line = Strategy::calculate_macd(
                Strategy::extract_close_prices(data),
                indicator.param1 > 0 ? static_cast<int>(indicator.param1) : 12,
                indicator.param2 > 0 ? static_cast<int>(indicator.param2) : 26,
                indicator.param3 > 0 ? static_cast<int>(indicator.param3) : 9,
                signal_line, histogram
            );
            result = std::make_pair(macd_line, signal_line);
        }
    }
    
    if (result.first != 0.0 || result.second != 0.0) {
        indicator_cache_pair_[cache_key] = result;
    }
    
    return result;
}

std::unordered_map<std::string, double> MultiLegStrategy::get_parameters() const {
    std::unordered_map<std::string, double> params;
    params["num_legs"] = static_cast<double>(legs_.size());
    return params;
}

void MultiLegStrategy::reset() {
    history_.clear();
    is_ready_ = false;
}

bool MultiLegStrategy::is_ready() const {
    return is_ready_;
}

} // namespace backtesting
