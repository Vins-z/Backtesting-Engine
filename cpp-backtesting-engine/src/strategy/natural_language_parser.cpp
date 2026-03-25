#include "strategy/natural_language_parser.h"
#include "strategy/strategy_factory.h"
#include "strategy/moving_average_strategy.h"
#include "strategy/rsi_strategy.h"
#include "strategy/macd_strategy.h"
#include "strategy/bollinger_bands_strategy.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <iostream>
#include <cctype>

namespace backtesting {

// ParsedStrategy implementation
nlohmann::json ParsedStrategy::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["description"] = description;
    j["indicators"] = indicators;
    j["parameters"] = parameters;
    j["entry_condition"] = entry_condition;
    j["exit_condition"] = exit_condition;
    j["risk_management"] = risk_management;
    if (!filters.is_null() && !filters.empty()) j["filters"] = filters;
    return j;
}

bool ParsedStrategy::validate() const {
    return !name.empty() && !indicators.empty();
}

// NaturalLanguageParser implementation
NaturalLanguageParser::NaturalLanguageParser() {
    initialize_patterns();
    initialize_mappings();
}

ParsedStrategy NaturalLanguageParser::parse_strategy(const std::string& natural_language_text) {
    ParsedStrategy parsed;
    
    // Preprocess the text
    std::string processed_text = preprocess_text(natural_language_text);
    
    // Extract strategy name (first sentence or line)
    std::istringstream iss(processed_text);
    std::string first_line;
    std::getline(iss, first_line);
    parsed.name = first_line.substr(0, first_line.find('.'));
    if (parsed.name.empty()) {
        parsed.name = "Natural Language Strategy";
    }
    
    // Extract components
    parsed.indicators = extract_indicators(processed_text);
    parsed.parameters = extract_parameters(processed_text);
    parsed.entry_condition = extract_entry_condition(processed_text);
    parsed.exit_condition = extract_exit_condition(processed_text);
    parsed.risk_management = extract_risk_management(processed_text);
    parsed.filters = extract_context_filters(processed_text);
    parsed.description = natural_language_text;
    
    return parsed;
}

std::unique_ptr<Strategy> NaturalLanguageParser::convert_to_strategy(const ParsedStrategy& parsed) {
    StrategyBuilder builder;
    return builder.build_strategy(parsed);
}

std::vector<std::string> NaturalLanguageParser::validate_strategy(const std::string& text) {
    std::vector<std::string> errors;
    // Normalize and preprocess like parse_strategy to make detection robust
    std::string normalized = normalize_text(text);
    std::string processed = preprocess_text(normalized);

    // Check for required components
    if (processed.find("buy") == std::string::npos && processed.find("sell") == std::string::npos) {
        errors.push_back("No buy/sell conditions found");
    }
    
    // Check for indicators
    bool has_indicator = false;
    for (const auto& pattern : indicator_patterns_) {
        if (processed.find(pattern) != std::string::npos) {
            has_indicator = true;
            break;
        }
    }
    
    if (!has_indicator) {
        errors.push_back("No technical indicators found");
    }
    
    return errors;
}

bool NaturalLanguageParser::is_valid_strategy(const std::string& text) {
    return validate_strategy(text).empty();
}

std::vector<std::string> NaturalLanguageParser::get_supported_patterns() const {
    return indicator_patterns_;
}

std::vector<std::string> NaturalLanguageParser::get_example_strategies() const {
    return {
        "MA crossover (5/15): Buy when 5-day SMA crosses above 15-day SMA; sell on cross below",
        "RSI momentum: Buy when RSI < 30 and price > 50-day SMA; sell when RSI > 70",
        "MACD trend: Buy on MACD line crossing above signal; sell on cross below; stop loss 2%",
        "Bollinger mean reversion: Buy at lower band when RSI < 30; exit at middle band",
        "Stochastic oversold: Buy when Stochastic %K < 20 and %D crosses above %K; sell when %K > 80",
        "ADX trend strength: Buy when ADX > 25 and price above 20-day EMA; sell when ADX < 20",
        "ATR breakout: Buy when price breaks above 20-day high with ATR(14) > 1.5x average; stop loss at ATR(14) below entry",
        "Williams %R reversal: Buy when Williams %R < -80; sell when %R > -20",
        "CCI divergence: Buy when CCI < -100 and price makes higher low; sell when CCI > 100",
        "Ichimoku cloud: Buy when price crosses above cloud and Tenkan > Kijun; sell when price crosses below cloud",
        "OBV confirmation: Buy when price makes new high and OBV confirms; sell when OBV diverges",
        "Multi-indicator: Buy when RSI < 30, Stochastic < 20, and price at support; sell when RSI > 70 or price hits resistance"
    };
}

// Private methods
std::vector<std::string> NaturalLanguageParser::extract_indicators(const std::string& text) {
    std::vector<std::string> found_indicators;
    
    for (const auto& pattern : indicator_patterns_) {
        if (text.find(pattern) != std::string::npos) {
            found_indicators.push_back(map_indicator_name(pattern));
        }
    }
    
    return found_indicators;
}

std::unordered_map<std::string, double> NaturalLanguageParser::extract_parameters(const std::string& text) {
    std::unordered_map<std::string, double> params;
    
    // Extract moving average periods
    std::vector<double> ma_periods = extract_numbers(text, "day");
    if (!ma_periods.empty()) {
        if (ma_periods.size() >= 2) {
            params["short_window"] = ma_periods[0];
            params["long_window"] = ma_periods[1];
        } else {
            params["long_window"] = ma_periods[0];
            params["short_window"] = ma_periods[0] * 0.5; // Default short period
        }
    }
    
    // Extract RSI parameters
    double rsi_period = extract_number(text, "RSI");
    if (rsi_period > 0) {
        params["period"] = rsi_period;
    }
    
    // Extract MACD parameters
    std::vector<double> macd_periods = extract_numbers(text, "MACD");
    if (macd_periods.size() >= 3) {
        params["fast_period"] = macd_periods[0];
        params["slow_period"] = macd_periods[1];
        params["signal_period"] = macd_periods[2];
    }
    
    // Extract Bollinger Band parameters
    double bb_period = extract_number(text, "Bollinger");
    if (bb_period > 0) {
        params["period"] = bb_period;
        params["std_dev"] = 2.0; // Default standard deviation
    }
    
    // Extract Stochastic parameters
    double stoch_period = extract_number(text, "Stochastic");
    if (stoch_period > 0) {
        params["k_period"] = stoch_period;
        params["d_period"] = 3.0; // Default
        params["oversold"] = 20.0;
        params["overbought"] = 80.0;
    }
    
    // Extract Williams %R parameters
    double wr_period = extract_number(text, "Williams");
    if (wr_period > 0) {
        params["period"] = wr_period;
        params["oversold"] = -80.0;
        params["overbought"] = -20.0;
    }
    
    // Extract CCI parameters
    double cci_period = extract_number(text, "CCI");
    if (cci_period > 0) {
        params["period"] = cci_period;
        params["oversold"] = -100.0;
        params["overbought"] = 100.0;
    }
    
    // Extract ATR parameters
    double atr_period = extract_number(text, "ATR");
    if (atr_period > 0) {
        params["period"] = atr_period;
    }
    
    // Extract ADX parameters
    double adx_period = extract_number(text, "ADX");
    if (adx_period > 0) {
        params["period"] = adx_period;
        params["trend_threshold"] = 25.0; // Default
    }
    
    // Extract Ichimoku parameters
    if (text.find("Ichimoku") != std::string::npos || text.find("ichimoku") != std::string::npos) {
        params["tenkan_period"] = 9.0;
        params["kijun_period"] = 26.0;
        params["senkou_period"] = 52.0;
    }
    
    return params;
}

std::string NaturalLanguageParser::extract_entry_condition(const std::string& text) {
    std::string condition;
    
    // Look for entry patterns
    std::vector<std::string> entry_patterns = {
        "buy when", "buy if", "enter when", "enter if", "go long when", "go long if"
    };
    
    for (const auto& pattern : entry_patterns) {
        size_t pos = text.find(pattern);
        if (pos != std::string::npos) {
            size_t end_pos = text.find(".", pos);
            if (end_pos == std::string::npos) {
                end_pos = text.find(",", pos);
            }
            if (end_pos == std::string::npos) {
                end_pos = text.length();
            }
            condition = text.substr(pos, end_pos - pos);
            break;
        }
    }
    
    return condition;
}

std::string NaturalLanguageParser::extract_exit_condition(const std::string& text) {
    std::string condition;
    
    // Look for exit patterns
    std::vector<std::string> exit_patterns = {
        "sell when", "sell if", "exit when", "exit if", "close when", "close if"
    };
    
    for (const auto& pattern : exit_patterns) {
        size_t pos = text.find(pattern);
        if (pos != std::string::npos) {
            size_t end_pos = text.find(".", pos);
            if (end_pos == std::string::npos) {
                end_pos = text.find(",", pos);
            }
            if (end_pos == std::string::npos) {
                end_pos = text.length();
            }
            condition = text.substr(pos, end_pos - pos);
            break;
        }
    }
    
    return condition;
}

nlohmann::json NaturalLanguageParser::extract_context_filters(const std::string& text) {
    nlohmann::json out = nlohmann::json::object();
    std::string lower;
    lower.resize(text.size());
    std::transform(text.begin(), text.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
    // "only trade when volatility is low", "low volatility", "avoid high volatility", "max ATR 5%"
    if (lower.find("volatility") != std::string::npos && (lower.find("low") != std::string::npos || lower.find("avoid high") != std::string::npos)) {
        out["volatility"] = {{"enabled", true}, {"maxATRPercent", 5.0}};
    }
    double atr_pct = extract_number(text, "atr");
    if (atr_pct > 0 && atr_pct <= 20) {
        if (!out.contains("volatility")) out["volatility"] = nlohmann::json::object();
        out["volatility"]["enabled"] = true;
        out["volatility"]["maxATRPercent"] = atr_pct;
    }
    // "avoid bear", "only bull", "trade in quiet regime", "avoid bear markets"
    if (lower.find("avoid bear") != std::string::npos || lower.find("only bull") != std::string::npos ||
        lower.find("quiet regime") != std::string::npos || lower.find("bull_quiet") != std::string::npos) {
        out["regime"] = {{"enabled", true}, {"allowedRegimes", std::vector<std::string>{"Bull_Quiet", "Neutral"}}};
    }
    return out;
}

std::string NaturalLanguageParser::extract_risk_management(const std::string& text) {
    std::string risk;
    
    // Look for risk management patterns
    std::vector<std::string> risk_patterns = {
        "stop loss", "stop-loss", "stop loss at", "stop at", "risk", "limit"
    };
    
    for (const auto& pattern : risk_patterns) {
        size_t pos = text.find(pattern);
        if (pos != std::string::npos) {
            size_t end_pos = text.find(".", pos);
            if (end_pos == std::string::npos) {
                end_pos = text.find(",", pos);
            }
            if (end_pos == std::string::npos) {
                end_pos = text.length();
            }
            risk = text.substr(pos, end_pos - pos);
            break;
        }
    }
    
    return risk;
}

std::string NaturalLanguageParser::preprocess_text(const std::string& text) {
    std::string processed = text;
    
    // Convert to lowercase
    std::transform(processed.begin(), processed.end(), processed.begin(), ::tolower);
    
    // Remove extra whitespace
    std::regex whitespace_regex("\\s+");
    processed = std::regex_replace(processed, whitespace_regex, " ");
    
    // Remove punctuation except for numbers
    std::regex punct_regex("[^a-z0-9\\s]");
    processed = std::regex_replace(processed, punct_regex, " ");
    
    return processed;
}

std::vector<std::string> NaturalLanguageParser::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::string NaturalLanguageParser::normalize_text(const std::string& text) {
    std::string normalized = text;
    
    // Replace common variations
    std::unordered_map<std::string, std::string> replacements = {
        {"moving average", "ma"},
        {"simple moving average", "sma"},
        {"exponential moving average", "ema"},
        {"relative strength index", "rsi"},
        {"macd", "macd"},
        {"bollinger bands", "bollinger"},
        {"bollinger band", "bollinger"}
    };
    
    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = normalized.find(from, pos)) != std::string::npos) {
            normalized.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
    
    return normalized;
}

bool NaturalLanguageParser::matches_pattern(const std::string& text, const std::string& pattern) {
    return text.find(pattern) != std::string::npos;
}

std::vector<std::string> NaturalLanguageParser::find_matches(const std::string& text, const std::vector<std::string>& patterns) {
    std::vector<std::string> matches;
    
    for (const auto& pattern : patterns) {
        if (matches_pattern(text, pattern)) {
            matches.push_back(pattern);
        }
    }
    
    return matches;
}

double NaturalLanguageParser::extract_number(const std::string& text, const std::string& keyword) {
    std::regex number_regex("(\\d+)\\s*" + keyword);
    std::smatch match;
    
    if (std::regex_search(text, match, number_regex)) {
        return std::stod(match[1]);
    }
    
    return 0.0;
}

std::vector<double> NaturalLanguageParser::extract_numbers(const std::string& text, const std::string& keyword) {
    std::vector<double> numbers;
    std::regex number_regex("(\\d+)\\s*" + keyword);
    std::sregex_iterator iter(text.begin(), text.end(), number_regex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        numbers.push_back(std::stod((*iter)[1]));
    }
    
    return numbers;
}

std::string NaturalLanguageParser::map_indicator_name(const std::string& natural_name) {
    auto it = indicator_mapping_.find(natural_name);
    if (it != indicator_mapping_.end()) {
        return it->second;
    }
    return natural_name;
}

std::unordered_map<std::string, double> NaturalLanguageParser::map_parameters(const std::string& indicator, const std::vector<double>& values) {
    std::unordered_map<std::string, double> params;
    
    auto it = parameter_mapping_.find(indicator);
    if (it != parameter_mapping_.end() && values.size() <= it->second.size()) {
        for (size_t i = 0; i < values.size(); ++i) {
            params[it->second[i]] = values[i];
        }
    }
    
    return params;
}

void NaturalLanguageParser::initialize_patterns() {
    indicator_patterns_ = {
        // Moving Averages
        "moving average", "ma", "sma", "ema", "simple moving average", "exponential moving average",
        // Momentum
        "rsi", "relative strength index", "relative strength",
        "macd", "moving average convergence divergence",
        "stochastic", "stochastic oscillator", "stoch",
        "williams", "williams %r", "williams percent r", "wr",
        "cci", "commodity channel index",
        // Volatility
        "bollinger bands", "bollinger band", "bollinger", "bb",
        "atr", "average true range",
        // Trend
        "adx", "average directional index", "directional movement",
        "ichimoku", "ichimoku cloud", "ichimoku kinko hyo",
        // Volume
        "obv", "on balance volume", "on-balance volume",
        "volume profile", "vpoc", "volume point of control",
        // Support/Resistance
        "support", "resistance", "support level", "resistance level",
        "fibonacci", "fibonacci retracement", "fib retracement"
    };
    
    parameter_patterns_ = {
        "period", "length", "window", "days", "weeks", "months"
    };
    
    condition_patterns_ = {
        "crosses above", "crosses below", "crosses over", "crosses under",
        "above", "below", "greater than", "less than", "higher than", "lower than"
    };
    
    risk_patterns_ = {
        "stop loss", "stop-loss", "stop", "limit", "risk", "max loss"
    };
}

void NaturalLanguageParser::initialize_mappings() {
    // Indicator name mappings
    indicator_mapping_ = {
        // Moving Averages
        {"moving average", "sma"},
        {"ma", "sma"},
        {"sma", "sma"},
        {"ema", "ema"},
        {"simple moving average", "sma"},
        {"exponential moving average", "ema"},
        // Momentum
        {"rsi", "rsi"},
        {"relative strength index", "rsi"},
        {"relative strength", "rsi"},
        {"macd", "macd"},
        {"moving average convergence divergence", "macd"},
        {"stochastic", "stochastic"},
        {"stochastic oscillator", "stochastic"},
        {"stoch", "stochastic"},
        {"williams", "williams_r"},
        {"williams %r", "williams_r"},
        {"williams percent r", "williams_r"},
        {"wr", "williams_r"},
        {"cci", "cci"},
        {"commodity channel index", "cci"},
        // Volatility
        {"bollinger bands", "bollinger_bands"},
        {"bollinger band", "bollinger_bands"},
        {"bollinger", "bollinger_bands"},
        {"bb", "bollinger_bands"},
        {"atr", "atr"},
        {"average true range", "atr"},
        // Trend
        {"adx", "adx"},
        {"average directional index", "adx"},
        {"directional movement", "adx"},
        {"ichimoku", "ichimoku"},
        {"ichimoku cloud", "ichimoku"},
        {"ichimoku kinko hyo", "ichimoku"},
        // Volume
        {"obv", "obv"},
        {"on balance volume", "obv"},
        {"on-balance volume", "obv"},
        {"volume profile", "volume_profile"},
        {"vpoc", "volume_profile"},
        {"volume point of control", "volume_profile"},
        // Support/Resistance
        {"support", "support_resistance"},
        {"resistance", "support_resistance"},
        {"support level", "support_resistance"},
        {"resistance level", "support_resistance"},
        {"fibonacci", "fibonacci"},
        {"fibonacci retracement", "fibonacci"},
        {"fib retracement", "fibonacci"}
    };
    
    // Parameter mappings
    parameter_mapping_ = {
        {"sma", {"short_window", "long_window"}},
        {"ema", {"short_window", "long_window"}},
        {"rsi", {"period", "oversold_threshold", "overbought_threshold"}},
        {"macd", {"fast_period", "slow_period", "signal_period"}},
        {"bollinger_bands", {"period", "std_dev"}},
        {"stochastic", {"k_period", "d_period", "oversold", "overbought"}},
        {"williams_r", {"period", "oversold", "overbought"}},
        {"cci", {"period", "oversold", "overbought"}},
        {"atr", {"period"}},
        {"adx", {"period", "trend_threshold"}},
        {"ichimoku", {"tenkan_period", "kijun_period", "senkou_period"}},
        {"obv", {}},
        {"volume_profile", {"bins"}},
        {"support_resistance", {"lookback", "strength"}},
        {"fibonacci", {"levels"}}
    };
}

// StrategyBuilder implementation
StrategyBuilder::StrategyBuilder() {
    initialize_default_params();
}

std::unique_ptr<Strategy> StrategyBuilder::build_strategy(const ParsedStrategy& parsed) {
    if (!validate_strategy_components(parsed)) {
        return nullptr;
    }
    
    // Determine strategy type based on indicators
    if (parsed.indicators.size() == 1) {
        std::string indicator = parsed.indicators[0];
        
        if (indicator == "sma" || indicator == "ema") {
            return create_moving_average_strategy(parsed);
        } else if (indicator == "rsi") {
            return create_rsi_strategy(parsed);
        } else if (indicator == "macd") {
            return create_macd_strategy(parsed);
        } else if (indicator == "bollinger_bands") {
            return create_bollinger_strategy(parsed);
        }
    } else if (parsed.indicators.size() > 1) {
        return create_combined_strategy(parsed);
    }
    
    return nullptr;
}

bool StrategyBuilder::validate_strategy_components(const ParsedStrategy& parsed) {
    return !parsed.indicators.empty() && !parsed.entry_condition.empty() && !parsed.exit_condition.empty();
}

std::vector<std::string> StrategyBuilder::get_validation_errors(const ParsedStrategy& parsed) {
    std::vector<std::string> errors;
    
    if (parsed.indicators.empty()) {
        errors.push_back("No indicators specified");
    }
    
    if (parsed.entry_condition.empty()) {
        errors.push_back("No entry condition specified");
    }
    
    if (parsed.exit_condition.empty()) {
        errors.push_back("No exit condition specified");
    }
    
    return errors;
}

ParsedStrategy StrategyBuilder::optimize_strategy(const ParsedStrategy& parsed) {
    ParsedStrategy optimized = parsed;
    
    // Apply default parameters if missing
    for (const auto& indicator : parsed.indicators) {
        auto it = default_params_.find(indicator);
        if (it != default_params_.end()) {
            for (const auto& [param, value] : it->second) {
                if (optimized.parameters.find(param) == optimized.parameters.end()) {
                    optimized.parameters[param] = value;
                }
            }
        }
    }
    
    return optimized;
}

std::vector<ParsedStrategy> StrategyBuilder::generate_variations(const ParsedStrategy& parsed) {
    std::vector<ParsedStrategy> variations;
    
    // Generate parameter variations
    for (const auto& indicator : parsed.indicators) {
        auto it = default_params_.find(indicator);
        if (it != default_params_.end()) {
            for (const auto& [param, default_value] : it->second) {
                ParsedStrategy variation = parsed;
                
                // Try different parameter values
                std::vector<double> test_values = {default_value * 0.5, default_value * 1.5, default_value * 2.0};
                for (double value : test_values) {
                    variation.parameters[param] = value;
                    variations.push_back(variation);
                }
            }
        }
    }
    
    return variations;
}

// Private strategy creation methods
std::unique_ptr<Strategy> StrategyBuilder::create_moving_average_strategy(const ParsedStrategy& parsed) {
    double short_window = parsed.parameters.count("short_window") ? parsed.parameters.at("short_window") : 10.0;
    double long_window = parsed.parameters.count("long_window") ? parsed.parameters.at("long_window") : 20.0;
    
    return std::make_unique<MovingAverageStrategy>(static_cast<int>(short_window), static_cast<int>(long_window));
}

std::unique_ptr<Strategy> StrategyBuilder::create_rsi_strategy(const ParsedStrategy& parsed) {
    double period = parsed.parameters.count("period") ? parsed.parameters.at("period") : 14.0;
    double oversold = parsed.parameters.count("oversold_threshold") ? parsed.parameters.at("oversold_threshold") : 30.0;
    double overbought = parsed.parameters.count("overbought_threshold") ? parsed.parameters.at("overbought_threshold") : 70.0;
    
    return std::make_unique<RSIStrategy>(static_cast<int>(period), oversold, overbought);
}

std::unique_ptr<Strategy> StrategyBuilder::create_macd_strategy(const ParsedStrategy& parsed) {
    double fast_period = parsed.parameters.count("fast_period") ? parsed.parameters.at("fast_period") : 12.0;
    double slow_period = parsed.parameters.count("slow_period") ? parsed.parameters.at("slow_period") : 26.0;
    double signal_period = parsed.parameters.count("signal_period") ? parsed.parameters.at("signal_period") : 9.0;
    
    return std::make_unique<MACDStrategy>(static_cast<int>(fast_period), static_cast<int>(slow_period), static_cast<int>(signal_period));
}

std::unique_ptr<Strategy> StrategyBuilder::create_bollinger_strategy(const ParsedStrategy& parsed) {
    double period = parsed.parameters.count("period") ? parsed.parameters.at("period") : 20.0;
    double std_dev = parsed.parameters.count("std_dev") ? parsed.parameters.at("std_dev") : 2.0;
    
    return std::make_unique<BollingerBandsStrategy>(static_cast<int>(period), std_dev);
}

std::unique_ptr<Strategy> StrategyBuilder::create_combined_strategy(const ParsedStrategy& parsed) {
    // For now, return the first indicator strategy
    // In the future, this could create a composite strategy
    if (!parsed.indicators.empty()) {
        ParsedStrategy single_indicator = parsed;
        single_indicator.indicators = {parsed.indicators[0]};
        return build_strategy(single_indicator);
    }
    
    return nullptr;
}

// Parameter validation methods
bool StrategyBuilder::validate_moving_average_params(const std::unordered_map<std::string, double>& params) {
    auto short_it = params.find("short_window");
    auto long_it = params.find("long_window");
    
    if (short_it != params.end() && long_it != params.end()) {
        return short_it->second < long_it->second && short_it->second > 0 && long_it->second > 0;
    }
    
    return true; // Use defaults if not specified
}

bool StrategyBuilder::validate_rsi_params(const std::unordered_map<std::string, double>& params) {
    auto period_it = params.find("period");
    if (period_it != params.end()) {
        return period_it->second > 0;
    }
    return true;
}

bool StrategyBuilder::validate_macd_params(const std::unordered_map<std::string, double>& params) {
    auto fast_it = params.find("fast_period");
    auto slow_it = params.find("slow_period");
    
    if (fast_it != params.end() && slow_it != params.end()) {
        return fast_it->second < slow_it->second && fast_it->second > 0 && slow_it->second > 0;
    }
    
    return true;
}

bool StrategyBuilder::validate_bollinger_params(const std::unordered_map<std::string, double>& params) {
    auto period_it = params.find("period");
    auto std_dev_it = params.find("std_dev");
    
    if (period_it != params.end()) {
        return period_it->second > 0;
    }
    
    if (std_dev_it != params.end()) {
        return std_dev_it->second > 0;
    }
    
    return true;
}

void StrategyBuilder::initialize_default_params() {
    default_params_ = {
        {"sma", {{"short_window", 10.0}, {"long_window", 20.0}}},
        {"ema", {{"short_window", 10.0}, {"long_window", 20.0}}},
        {"rsi", {{"period", 14.0}, {"oversold_threshold", 30.0}, {"overbought_threshold", 70.0}}},
        {"macd", {{"fast_period", 12.0}, {"slow_period", 26.0}, {"signal_period", 9.0}}},
        {"bollinger_bands", {{"period", 20.0}, {"std_dev", 2.0}}}
    };
}

} // namespace backtesting
