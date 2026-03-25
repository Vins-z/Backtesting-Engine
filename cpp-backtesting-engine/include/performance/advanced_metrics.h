#pragma once

#include "common/types.h"
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace backtesting {

// Advanced performance metrics beyond basic MT5 metrics
struct AdvancedMetrics {
    // Risk-adjusted returns
    double calmar_ratio;
    double sortino_ratio;
    double information_ratio;
    double treynor_ratio;
    double jensen_alpha;
    
    // Drawdown analysis
    double avg_drawdown;
    double avg_drawdown_duration;
    double recovery_factor;
    double ulcer_index;
    
    // Trade analysis
    double avg_win;
    double avg_loss;
    double largest_win;
    double largest_loss;
    double win_loss_ratio;
    double expectancy;
    double kelly_criterion;
    
    // Volatility analysis
    double downside_deviation;
    double upside_deviation;
    double var_95;  // Value at Risk 95%
    double cvar_95; // Conditional Value at Risk 95%
    
    // Time-based analysis
    double monthly_returns[12];
    double quarterly_returns[4];
    double yearly_returns;
    double best_month;
    double worst_month;
    
    // Advanced ratios
    double gain_to_pain_ratio;
    double profit_factor_ratio;
    double risk_reward_ratio;
    double payoff_ratio;
    
    // Machine learning metrics
    double strategy_consistency;
    double market_correlation;
    double regime_adaptation;
    
    // Convert to JSON
    nlohmann::json to_json() const;
};

// Advanced performance analyzer with ML capabilities
class AdvancedPerformanceAnalyzer {
public:
    AdvancedPerformanceAnalyzer();
    ~AdvancedPerformanceAnalyzer() = default;
    
    // Calculate advanced metrics
    AdvancedMetrics calculate_advanced_metrics(
        const std::vector<std::pair<Timestamp, Price>>& equity_curve,
        const std::vector<Fill>& trades,
        Price initial_capital,
        Price risk_free_rate = 0.02
    ) const;
    
    // Machine learning analysis
    double calculate_strategy_consistency(const std::vector<Fill>& trades) const;
    double calculate_market_correlation(
        const std::vector<std::pair<Timestamp, Price>>& equity_curve,
        const std::vector<std::pair<Timestamp, Price>>& market_data
    ) const;
    
    // Risk modeling
    double calculate_var(const std::vector<double>& returns, double confidence = 0.95) const;
    double calculate_cvar(const std::vector<double>& returns, double confidence = 0.95) const;
    
    // Regime detection
    std::vector<std::string> detect_market_regimes(
        const std::vector<std::pair<Timestamp, Price>>& market_data
    ) const;
    
    // Strategy optimization suggestions
    std::unordered_map<std::string, double> suggest_optimizations(
        const std::vector<Fill>& trades,
        const AdvancedMetrics& metrics
    ) const;

private:
    // Helper methods
    std::vector<double> calculate_returns(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    double calculate_drawdown_metrics(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    double calculate_ulcer_index(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    std::vector<double> calculate_monthly_returns(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
};

} // namespace backtesting
