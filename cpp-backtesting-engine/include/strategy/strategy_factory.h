#pragma once

#include "strategy.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <functional>

namespace backtesting {

// Enhanced strategy configuration
struct StrategyConfig {
    std::string name;
    std::string description;
    std::unordered_map<std::string, double> parameters;
    std::unordered_map<std::string, std::string> string_parameters;
    bool is_active = true;
    double confidence_threshold = 0.5;
    
    // Risk management settings
    double max_position_size = 1.0;
    double stop_loss_pct = 0.05;
    double take_profit_pct = 0.10;
    
    // Signal filtering
    bool use_volume_filter = false;
    long min_volume = 1000;
    bool use_volatility_filter = false;
    double max_volatility = 0.05;
    
    nlohmann::json to_json() const;
    static StrategyConfig from_json(const nlohmann::json& j);
};

// Strategy performance metrics for optimization
struct StrategyMetrics {
    double total_return = 0.0;
    double sharpe_ratio = 0.0;
    double max_drawdown = 0.0;
    double win_rate = 0.0;
    int total_trades = 0;
    double profit_factor = 0.0;
    double avg_trade_duration = 0.0;
    
    nlohmann::json to_json() const;
};

class StrategyFactory {
public:
    // Enhanced strategy creation with configuration
    static std::unique_ptr<Strategy> create(
        const std::string& strategy_name,
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_from_config(
        const StrategyConfig& config
    );
    
    static std::unique_ptr<Strategy> create_from_json(
        const nlohmann::json& strategy_json
    );
    
    // Strategy discovery and information
    static std::vector<std::string> get_available_strategies();
    static std::vector<StrategyConfig> get_default_configs();
    static StrategyConfig get_default_config(const std::string& strategy_name);
    static std::string get_strategy_description(const std::string& strategy_name);
    
    // Parameter validation and optimization
    static bool validate_parameters(
        const std::string& strategy_name,
        const std::unordered_map<std::string, double>& params
    );
    static std::vector<std::unordered_map<std::string, double>> generate_parameter_grid(
        const std::string& strategy_name,
        const std::unordered_map<std::string, std::vector<double>>& param_ranges
    );
    
    // Composite strategies
    static std::unique_ptr<Strategy> create_ensemble_strategy(
        const std::vector<StrategyConfig>& strategies,
        const std::string& combination_method = "weighted_average"
    );
    
    static std::unique_ptr<Strategy> create_multi_timeframe_strategy(
        const StrategyConfig& primary_strategy,
        const std::vector<std::string>& timeframes
    );
    
private:
    // Basic strategy creators (existing)
    static std::unique_ptr<Strategy> create_moving_average_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_rsi_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_macd_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_bollinger_bands_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    // Advanced strategy creators
    static std::unique_ptr<Strategy> create_mean_reversion_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_momentum_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_pairs_trading_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_breakout_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_machine_learning_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_volatility_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unique_ptr<Strategy> create_multi_leg_strategy(
        const std::unordered_map<std::string, std::string>& string_params
    );
    
    static std::unique_ptr<Strategy> create_arbitrage_strategy(
        const std::unordered_map<std::string, double>& params
    );
    
    // Utility methods
    static void validate_common_parameters(
        const std::unordered_map<std::string, double>& params
    );
    
    static std::unordered_map<std::string, double> merge_default_parameters(
        const std::string& strategy_name,
        const std::unordered_map<std::string, double>& user_params
    );
};

} // namespace backtesting 