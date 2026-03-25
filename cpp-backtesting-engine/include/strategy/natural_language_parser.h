#pragma once

#include "strategy/strategy.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>

namespace backtesting {

// Strategy components extracted from natural language
struct ParsedStrategy {
    std::string name;
    std::string description;
    std::vector<std::string> indicators;
    std::unordered_map<std::string, double> parameters;
    std::string entry_condition;
    std::string exit_condition;
    std::string risk_management;
    // Context filters from phrases like "only trade when volatility is low", "avoid bear markets"
    nlohmann::json filters;  // { volatility: { enabled, maxATRPercent }, regime: { enabled, allowedRegimes } }
    
    // Convert to JSON for API response
    nlohmann::json to_json() const;
    
    // Validate parsed strategy
    bool validate() const;
};

// Natural language parser for trading strategies
class NaturalLanguageParser {
public:
    NaturalLanguageParser();
    ~NaturalLanguageParser() = default;
    
    // Main parsing method
    ParsedStrategy parse_strategy(const std::string& natural_language_text);
    
    // Strategy conversion
    std::unique_ptr<Strategy> convert_to_strategy(const ParsedStrategy& parsed);
    
    // Validation and error checking
    std::vector<std::string> validate_strategy(const std::string& text);
    bool is_valid_strategy(const std::string& text);
    
    // Get available patterns and examples
    std::vector<std::string> get_supported_patterns() const;
    std::vector<std::string> get_example_strategies() const;

private:
    // Pattern matching and extraction
    std::vector<std::string> extract_indicators(const std::string& text);
    std::unordered_map<std::string, double> extract_parameters(const std::string& text);
    std::string extract_entry_condition(const std::string& text);
    std::string extract_exit_condition(const std::string& text);
    std::string extract_risk_management(const std::string& text);
    nlohmann::json extract_context_filters(const std::string& text);
    
    // Text preprocessing
    std::string preprocess_text(const std::string& text);
    std::vector<std::string> tokenize(const std::string& text);
    std::string normalize_text(const std::string& text);
    
    // Pattern matching
    bool matches_pattern(const std::string& text, const std::string& pattern);
    std::vector<std::string> find_matches(const std::string& text, const std::vector<std::string>& patterns);
    
    // Parameter extraction helpers
    double extract_number(const std::string& text, const std::string& keyword);
    std::vector<double> extract_numbers(const std::string& text, const std::string& keyword);
    
    // Strategy mapping
    std::string map_indicator_name(const std::string& natural_name);
    std::unordered_map<std::string, double> map_parameters(const std::string& indicator, const std::vector<double>& values);
    
    // Supported patterns and keywords
    std::vector<std::string> indicator_patterns_;
    std::vector<std::string> parameter_patterns_;
    std::vector<std::string> condition_patterns_;
    std::vector<std::string> risk_patterns_;
    
    // Mapping dictionaries
    std::unordered_map<std::string, std::string> indicator_mapping_;
    std::unordered_map<std::string, std::vector<std::string>> parameter_mapping_;
    
    // Initialize patterns and mappings
    void initialize_patterns();
    void initialize_mappings();
};

// Strategy builder from parsed components
class StrategyBuilder {
public:
    StrategyBuilder();
    ~StrategyBuilder() = default;
    
    // Build strategy from parsed components
    std::unique_ptr<Strategy> build_strategy(const ParsedStrategy& parsed);
    
    // Strategy validation
    bool validate_strategy_components(const ParsedStrategy& parsed);
    std::vector<std::string> get_validation_errors(const ParsedStrategy& parsed);
    
    // Strategy optimization
    ParsedStrategy optimize_strategy(const ParsedStrategy& parsed);
    std::vector<ParsedStrategy> generate_variations(const ParsedStrategy& parsed);

private:
    // Strategy creation helpers
    std::unique_ptr<Strategy> create_moving_average_strategy(const ParsedStrategy& parsed);
    std::unique_ptr<Strategy> create_rsi_strategy(const ParsedStrategy& parsed);
    std::unique_ptr<Strategy> create_macd_strategy(const ParsedStrategy& parsed);
    std::unique_ptr<Strategy> create_bollinger_strategy(const ParsedStrategy& parsed);
    std::unique_ptr<Strategy> create_combined_strategy(const ParsedStrategy& parsed);
    
    // Parameter validation
    bool validate_moving_average_params(const std::unordered_map<std::string, double>& params);
    bool validate_rsi_params(const std::unordered_map<std::string, double>& params);
    bool validate_macd_params(const std::unordered_map<std::string, double>& params);
    bool validate_bollinger_params(const std::unordered_map<std::string, double>& params);
    
    // Default parameters
    std::unordered_map<std::string, std::unordered_map<std::string, double>> default_params_;
    
    // Initialize default parameters
    void initialize_default_params();
};

} // namespace backtesting
