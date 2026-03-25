#include "strategy/strategy_factory.h"
#include "strategy/moving_average_strategy.h"
#include "strategy/rsi_strategy.h"
#include "strategy/macd_strategy.h"
#include "strategy/bollinger_bands_strategy.h"
#include "strategy/advanced_strategies.h"
#include "strategy/multi_leg_strategy.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace backtesting {

// StrategyConfig implementation
nlohmann::json StrategyConfig::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["description"] = description;
    j["parameters"] = parameters;
    j["string_parameters"] = string_parameters;
    j["is_active"] = is_active;
    j["confidence_threshold"] = confidence_threshold;
    j["max_position_size"] = max_position_size;
    j["stop_loss_pct"] = stop_loss_pct;
    j["take_profit_pct"] = take_profit_pct;
    j["use_volume_filter"] = use_volume_filter;
    j["min_volume"] = min_volume;
    j["use_volatility_filter"] = use_volatility_filter;
    j["max_volatility"] = max_volatility;
    return j;
}

StrategyConfig StrategyConfig::from_json(const nlohmann::json& j) {
    StrategyConfig config;
    config.name = j.value("name", "");
    config.description = j.value("description", "");
    config.parameters = j.value("parameters", std::unordered_map<std::string, double>{});
    config.string_parameters = j.value("string_parameters", std::unordered_map<std::string, std::string>{});
    config.is_active = j.value("is_active", true);
    config.confidence_threshold = j.value("confidence_threshold", 0.5);
    config.max_position_size = j.value("max_position_size", 1.0);
    config.stop_loss_pct = j.value("stop_loss_pct", 0.05);
    config.take_profit_pct = j.value("take_profit_pct", 0.10);
    config.use_volume_filter = j.value("use_volume_filter", false);
    config.min_volume = j.value("min_volume", 1000L);
    config.use_volatility_filter = j.value("use_volatility_filter", false);
    config.max_volatility = j.value("max_volatility", 0.05);
    return config;
}

// StrategyMetrics implementation
nlohmann::json StrategyMetrics::to_json() const {
    nlohmann::json j;
    j["total_return"] = total_return;
    j["sharpe_ratio"] = sharpe_ratio;
    j["max_drawdown"] = max_drawdown;
    j["win_rate"] = win_rate;
    j["total_trades"] = total_trades;
    j["profit_factor"] = profit_factor;
    j["avg_trade_duration"] = avg_trade_duration;
    return j;
}

// Enhanced strategy creation
std::unique_ptr<Strategy> StrategyFactory::create(
    const std::string& strategy_name,
    const std::unordered_map<std::string, double>& params) {
    
    // Validate parameters
    if (!validate_parameters(strategy_name, params)) {
        throw std::invalid_argument("Invalid parameters for strategy: " + strategy_name);
    }
    
    // Merge with default parameters
    auto merged_params = merge_default_parameters(strategy_name, params);
    
    // Create strategy based on name
    if (strategy_name == "moving_average" || strategy_name == "sma" || strategy_name == "ema") {
        return create_moving_average_strategy(merged_params);
    } else if (strategy_name == "rsi") {
        return create_rsi_strategy(merged_params);
    } else if (strategy_name == "macd") {
        return create_macd_strategy(merged_params);
    } else if (strategy_name == "bollinger_bands" || strategy_name == "bollinger") {
        return create_bollinger_bands_strategy(merged_params);
    } else if (strategy_name == "momentum") {
        return create_momentum_strategy(merged_params);
    } else if (strategy_name == "mean_reversion") {
        return create_mean_reversion_strategy(merged_params);
    } else if (strategy_name == "breakout") {
        return create_breakout_strategy(merged_params);
    } else if (strategy_name == "multi_leg") {
        // Start with no configured legs; legs/indicators are supplied via request config/string_parameters.
        return create_multi_leg_strategy({});
    } else {
        throw std::invalid_argument("Unknown strategy: " + strategy_name);
    }
}

std::unique_ptr<Strategy> StrategyFactory::create_from_config(const StrategyConfig& config) {
    if (config.name == "multi_leg") {
        return create_multi_leg_strategy(config.string_parameters);
    }
    return create(config.name, config.parameters);
}

std::unique_ptr<Strategy> StrategyFactory::create_from_json(const nlohmann::json& strategy_json) {
    auto config = StrategyConfig::from_json(strategy_json);
    return create_from_config(config);
}

std::vector<std::string> StrategyFactory::get_available_strategies() {
    return {
        "moving_average",
        "rsi", 
        "macd",
        "bollinger_bands",
        "momentum",
        "mean_reversion",
        "breakout",
        "multi_leg"
    };
}

std::vector<StrategyConfig> StrategyFactory::get_default_configs() {
    std::vector<StrategyConfig> configs;
    
    for (const auto& strategy_name : get_available_strategies()) {
        configs.push_back(get_default_config(strategy_name));
    }
    
    return configs;
}

StrategyConfig StrategyFactory::get_default_config(const std::string& strategy_name) {
    StrategyConfig config;
    config.name = strategy_name;
    config.description = get_strategy_description(strategy_name);
    
    if (strategy_name == "moving_average") {
        config.parameters = {{"short_window", 10}, {"long_window", 20}};
    } else if (strategy_name == "rsi") {
        config.parameters = {{"period", 14}, {"oversold_threshold", 30}, {"overbought_threshold", 70}};
    } else if (strategy_name == "macd") {
        config.parameters = {{"fast_period", 12}, {"slow_period", 26}, {"signal_period", 9}};
    } else if (strategy_name == "bollinger_bands") {
        config.parameters = {{"period", 20}, {"std_dev", 2.0}};
    } else if (strategy_name == "momentum") {
        config.parameters = {
            {"lookback_period", 14}, {"rsi_period", 14}, {"volume_sma_period", 20},
            {"rsi_oversold", 30}, {"rsi_overbought", 70}, {"momentum_threshold", 0.02},
            {"volume_factor", 1.5}, {"use_volume_confirmation", 1}
        };
    } else if (strategy_name == "mean_reversion") {
        config.parameters = {
            {"bollinger_period", 20}, {"bollinger_std_dev", 2.0}, {"rsi_period", 14},
            {"rsi_oversold", 30}, {"rsi_overbought", 70}, {"mean_reversion_threshold", 0.02},
            {"max_hold_period", 10}
        };
    } else if (strategy_name == "breakout") {
        config.parameters = {
            {"lookback_period", 20}, {"breakout_threshold", 0.01}, {"volume_confirmation_period", 10},
            {"volume_multiplier", 1.5}, {"atr_period", 14}, {"atr_multiplier", 2.0},
            {"use_dynamic_stops", 1}
        };
    }
    
    return config;
}

std::string StrategyFactory::get_strategy_description(const std::string& strategy_name) {
    if (strategy_name == "moving_average") {
        return "Crossover strategy using short and long period moving averages";
    } else if (strategy_name == "rsi") {
        return "Relative Strength Index based momentum strategy";
    } else if (strategy_name == "macd") {
        return "Moving Average Convergence Divergence trend following strategy";
    } else if (strategy_name == "bollinger_bands") {
        return "Bollinger Bands based mean reversion strategy";
    } else if (strategy_name == "momentum") {
        return "Multi-indicator momentum strategy with volume confirmation";
    } else if (strategy_name == "mean_reversion") {
        return "Advanced mean reversion strategy using Bollinger Bands and RSI";
    } else if (strategy_name == "breakout") {
        return "Breakout strategy with volume confirmation and dynamic stops";
    }
    return "Unknown strategy";
}

bool StrategyFactory::validate_parameters(
    const std::string& strategy_name,
    const std::unordered_map<std::string, double>& params) {
    
    try {
        validate_common_parameters(params);
        
        // Strategy-specific validation
        if (strategy_name == "moving_average") {
            auto short_it = params.find("short_window");
            auto long_it = params.find("long_window");
            if (short_it != params.end() && long_it != params.end()) {
                if (short_it->second >= long_it->second) {
                    return false; // Short window must be less than long window
                }
            }
        } else if (strategy_name == "rsi") {
            auto oversold_it = params.find("oversold_threshold");
            auto overbought_it = params.find("overbought_threshold");
            if (oversold_it != params.end() && overbought_it != params.end()) {
                if (oversold_it->second >= overbought_it->second) {
                    return false; // Oversold must be less than overbought
                }
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void StrategyFactory::validate_common_parameters(
    const std::unordered_map<std::string, double>& params) {
    
    for (const auto& [key, value] : params) {
        // Check for reasonable parameter ranges
        if (key.find("period") != std::string::npos || key.find("window") != std::string::npos) {
            if (value < 1 || value > 1000) {
                throw std::invalid_argument("Period/window parameters must be between 1 and 1000");
            }
        }
        
        if (key.find("threshold") != std::string::npos) {
            if (value < 0 || value > 1) {
                throw std::invalid_argument("Threshold parameters must be between 0 and 1");
            }
        }
        
        // Check for NaN or infinite values
        if (std::isnan(value) || std::isinf(value)) {
            throw std::invalid_argument("Parameter values cannot be NaN or infinite");
        }
    }
}

std::unordered_map<std::string, double> StrategyFactory::merge_default_parameters(
    const std::string& strategy_name,
    const std::unordered_map<std::string, double>& user_params) {
    
    auto default_config = get_default_config(strategy_name);
    auto merged_params = default_config.parameters;
    
    // Override with user parameters
    for (const auto& [key, value] : user_params) {
        merged_params[key] = value;
    }
    
    return merged_params;
}

std::unique_ptr<Strategy> StrategyFactory::create_moving_average_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int short_window = 10;
    int long_window = 20;
    
    auto it = params.find("short_window");
    if (it != params.end()) {
        short_window = static_cast<int>(it->second);
    }
    
    it = params.find("long_window");
    if (it != params.end()) {
        long_window = static_cast<int>(it->second);
    }
    
    return std::make_unique<MovingAverageStrategy>(short_window, long_window);
}

std::unique_ptr<Strategy> StrategyFactory::create_rsi_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int period = 14;
    double oversold_threshold = 30.0;
    double overbought_threshold = 70.0;
    
    auto it = params.find("period");
    if (it != params.end()) {
        period = static_cast<int>(it->second);
    }
    
    it = params.find("oversold_threshold");
    if (it != params.end()) {
        oversold_threshold = it->second;
    }
    
    it = params.find("overbought_threshold");
    if (it != params.end()) {
        overbought_threshold = it->second;
    }
    
    return std::make_unique<RSIStrategy>(period, oversold_threshold, overbought_threshold);
}

std::unique_ptr<Strategy> StrategyFactory::create_macd_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int fast_period = 12;
    int slow_period = 26;
    int signal_period = 9;
    
    auto it = params.find("fast_period");
    if (it != params.end()) {
        fast_period = static_cast<int>(it->second);
    }
    
    it = params.find("slow_period");
    if (it != params.end()) {
        slow_period = static_cast<int>(it->second);
    }
    
    it = params.find("signal_period");
    if (it != params.end()) {
        signal_period = static_cast<int>(it->second);
    }
    
    return std::make_unique<MACDStrategy>(fast_period, slow_period, signal_period);
}

std::unique_ptr<Strategy> StrategyFactory::create_bollinger_bands_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int period = 20;
    double std_dev = 2.0;
    
    auto it = params.find("period");
    if (it != params.end()) {
        period = static_cast<int>(it->second);
    }
    
    it = params.find("std_dev");
    if (it != params.end()) {
        std_dev = it->second;
    }
    
    return std::make_unique<BollingerBandsStrategy>(period, std_dev);
}

// Advanced strategy creators
std::unique_ptr<Strategy> StrategyFactory::create_momentum_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int lookback_period = static_cast<int>(params.at("lookback_period"));
    int rsi_period = static_cast<int>(params.at("rsi_period"));
    int volume_sma_period = static_cast<int>(params.at("volume_sma_period"));
    double rsi_oversold = params.at("rsi_oversold");
    double rsi_overbought = params.at("rsi_overbought");
    double momentum_threshold = params.at("momentum_threshold");
    double volume_factor = params.at("volume_factor");
    bool use_volume_confirmation = params.at("use_volume_confirmation") > 0.5;
    
    return std::make_unique<MomentumStrategy>(
        lookback_period, rsi_period, volume_sma_period,
        rsi_oversold, rsi_overbought, momentum_threshold,
        volume_factor, use_volume_confirmation
    );
}

std::unique_ptr<Strategy> StrategyFactory::create_mean_reversion_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int bollinger_period = static_cast<int>(params.at("bollinger_period"));
    double bollinger_std_dev = params.at("bollinger_std_dev");
    int rsi_period = static_cast<int>(params.at("rsi_period"));
    double rsi_oversold = params.at("rsi_oversold");
    double rsi_overbought = params.at("rsi_overbought");
    double mean_reversion_threshold = params.at("mean_reversion_threshold");
    int max_hold_period = static_cast<int>(params.at("max_hold_period"));
    
    return std::make_unique<MeanReversionStrategy>(
        bollinger_period, bollinger_std_dev, rsi_period,
        rsi_oversold, rsi_overbought, mean_reversion_threshold,
        max_hold_period
    );
}

std::unique_ptr<Strategy> StrategyFactory::create_breakout_strategy(
    const std::unordered_map<std::string, double>& params) {
    
    int lookback_period = static_cast<int>(params.at("lookback_period"));
    double breakout_threshold = params.at("breakout_threshold");
    int volume_confirmation_period = static_cast<int>(params.at("volume_confirmation_period"));
    double volume_multiplier = params.at("volume_multiplier");
    int atr_period = static_cast<int>(params.at("atr_period"));
    double atr_multiplier = params.at("atr_multiplier");
    bool use_dynamic_stops = params.at("use_dynamic_stops") > 0.5;
    
    return std::make_unique<BreakoutStrategy>(
        lookback_period, breakout_threshold, volume_confirmation_period,
        volume_multiplier, atr_period, atr_multiplier, use_dynamic_stops
    );
}

std::unique_ptr<Strategy> StrategyFactory::create_multi_leg_strategy(
    const std::unordered_map<std::string, std::string>& string_params) {
    
    std::vector<StrategyLeg> legs;
    
    // Parse legs from JSON string if available
    auto it = string_params.find("legs");
    if (it != string_params.end()) {
        try {
            auto j_legs = nlohmann::json::parse(it->second);
            for (const auto& j_leg : j_legs) {
                StrategyLeg leg;
                leg.symbol = j_leg.value("symbol", "");
                leg.direction = j_leg.value("direction", "BUY");
                leg.quantity = j_leg.value("quantity", 100.0);
                leg.stop_loss_pct = j_leg.value("stop_loss_pct", 0.05);
                leg.take_profit_pct = j_leg.value("take_profit_pct", 0.10);
                leg.strike = j_leg.value("strike", "");
                leg.segment = j_leg.value("segment", "EQUITY");
                legs.push_back(leg);
            }
        } catch (...) {
            // Log error or handle gracefully
        }
    }
    
    auto strategy = std::make_unique<MultiLegStrategy>(legs);
    
    // Parse indicators from JSON if available
    auto indicators_it = string_params.find("indicators");
    if (indicators_it != string_params.end()) {
        try {
            auto j_indicators = nlohmann::json::parse(indicators_it->second);
            std::vector<MultiLegStrategy::IndicatorConfig> indicator_configs;
            
            for (const auto& j_ind : j_indicators) {
                MultiLegStrategy::IndicatorConfig config;
                config.type = j_ind.value("type", "");
                config.weight = j_ind.value("weight", 1.0);
                
                // Parse parameters based on indicator type
                if (config.type == "SMA" || config.type == "EMA") {
                    config.length = j_ind.value("length", j_ind.value("period", 20));
                    config.param1 = 0.0;
                    config.param2 = 0.0;
                    config.param3 = 0.0;
                } else if (config.type == "RSI") {
                    config.length = j_ind.value("period", j_ind.value("length", 14));
                    config.param1 = j_ind.value("overbought", 70.0);
                    config.param2 = j_ind.value("oversold", 30.0);
                    config.param3 = 0.0;
                } else if (config.type == "MACD") {
                    config.length = 0; // Not used for MACD
                    config.param1 = j_ind.value("fastPeriod", 12.0);
                    config.param2 = j_ind.value("slowPeriod", 26.0);
                    config.param3 = j_ind.value("signalPeriod", 9.0);
                } else if (config.type == "BOLLINGER") {
                    config.length = j_ind.value("period", j_ind.value("length", 20));
                    config.param1 = j_ind.value("stdDev", 2.0);
                    config.param2 = 0.0;
                    config.param3 = 0.0;
                } else if (config.type == "ATR" || config.type == "MFI" || config.type == "WILLIAMS_R") {
                    config.length = j_ind.value("period", j_ind.value("length", 14));
                    config.param1 = 0.0;
                    config.param2 = 0.0;
                    config.param3 = 0.0;
                } else if (config.type == "CCI") {
                    config.length = j_ind.value("period", j_ind.value("length", 20));
                    config.param1 = 0.0;
                    config.param2 = 0.0;
                    config.param3 = 0.0;
                } else if (config.type == "STOCHASTIC") {
                    config.length = j_ind.value("kPeriod", j_ind.value("period", 14));
                    config.param1 = j_ind.value("dPeriod", 3.0);
                    config.param2 = 0.0;
                    config.param3 = 0.0;
                } else if (config.type == "OBV") {
                    config.length = 0; // OBV doesn't use period
                    config.param1 = 0.0;
                    config.param2 = 0.0;
                    config.param3 = 0.0;
                } else {
                    // Generic fallback - try to extract common parameters
                    config.length = j_ind.value("length", j_ind.value("period", 14));
                    if (j_ind.contains("params") && j_ind["params"].is_object()) {
                        auto params = j_ind["params"];
                        config.param1 = params.value("param1", params.value("stdDev", params.value("fastPeriod", 0.0)));
                        config.param2 = params.value("param2", params.value("slowPeriod", params.value("dPeriod", 0.0)));
                        config.param3 = params.value("param3", params.value("signalPeriod", 0.0));
                    } else {
                        config.param1 = j_ind.value("param1", j_ind.value("stdDev", j_ind.value("fastPeriod", 0.0)));
                        config.param2 = j_ind.value("param2", j_ind.value("slowPeriod", j_ind.value("dPeriod", 0.0)));
                        config.param3 = j_ind.value("param3", j_ind.value("signalPeriod", 0.0));
                    }
                }
                
                if (!config.type.empty()) {
                    indicator_configs.push_back(config);
                }
            }
            
            if (!indicator_configs.empty()) {
                strategy->set_indicators_extended(indicator_configs);
            }
        } catch (const std::exception& e) {
            // Log error - indicators are optional, so continue without them
            std::cerr << "Warning: Failed to parse indicators: " << e.what() << std::endl;
        }
    }
    
    return strategy;
}

// Unsupported / not-yet-open-sourced strategies.
// These are intentionally not implemented in the open-source build.
std::unique_ptr<Strategy> StrategyFactory::create_pairs_trading_strategy(
    const std::unordered_map<std::string, double>& params) {
    (void)params;  // Unused parameter
    throw std::runtime_error("Pairs trading strategy is not supported in the open-source build");
}

std::unique_ptr<Strategy> StrategyFactory::create_machine_learning_strategy(
    const std::unordered_map<std::string, double>& params) {
    (void)params;  // Unused parameter
    throw std::runtime_error("Machine learning strategy is not supported in the open-source build");
}

std::unique_ptr<Strategy> StrategyFactory::create_volatility_strategy(
    const std::unordered_map<std::string, double>& params) {
    (void)params;  // Unused parameter
    throw std::runtime_error("Volatility strategy is not supported in the open-source build");
}

std::unique_ptr<Strategy> StrategyFactory::create_arbitrage_strategy(
    const std::unordered_map<std::string, double>& params) {
    (void)params;  // Unused parameter
    throw std::runtime_error("Arbitrage strategy is not supported in the open-source build");
}

} // namespace backtesting 