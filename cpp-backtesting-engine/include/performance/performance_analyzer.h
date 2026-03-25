#pragma once

#include "common/types.h"
#include <vector>
#include <memory>
#include <random>
#include <functional>
#include <nlohmann/json.hpp>

namespace backtesting {

class PerformanceAnalyzer {
private:
    std::vector<Fill> recorded_trades_;
    
public:
    PerformanceAnalyzer() = default;
    virtual ~PerformanceAnalyzer() = default;
    
    // Record a trade for analysis
    void record_trade(const Fill& trade);
    
    // Calculate comprehensive performance metrics
    virtual PerformanceMetrics calculate_metrics(
        const std::vector<std::pair<Timestamp, Price>>& equity_curve,
        const std::vector<Fill>& trades,
        Price initial_capital,
        Price risk_free_rate = 0.02
    ) const = 0;
    
    // Individual metric calculations
    Price calculate_total_return(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    Price calculate_annualized_return(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    Price calculate_sharpe_ratio(const std::vector<std::pair<Timestamp, Price>>& equity_curve, Price risk_free_rate = 0.02) const;
    Price calculate_max_drawdown(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    Price calculate_volatility(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    
    // Trade-based metrics
    Price calculate_win_rate(const std::vector<Fill>& trades) const;
    Price calculate_profit_factor(const std::vector<Fill>& trades) const;
    int count_total_trades(const std::vector<Fill>& trades) const;
    int count_winning_trades(const std::vector<Fill>& trades) const;
    int count_losing_trades(const std::vector<Fill>& trades) const;
    
    // Reset analyzer
    void reset();
    
    // Generate performance report
    virtual std::string generate_report(const PerformanceMetrics& metrics) = 0;
    
    // Export metrics to JSON
    virtual nlohmann::json export_to_json(const PerformanceMetrics& metrics) = 0;
};

class BasicPerformanceAnalyzer : public PerformanceAnalyzer {
private:
    // Helper functions
    std::vector<Price> calculate_returns(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    Price calculate_total_return(Price initial_capital, Price final_capital) const;
    Price calculate_annualized_return(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    Price calculate_sharpe_ratio(const std::vector<Price>& returns, Price risk_free_rate) const;
    Price calculate_sortino_ratio(const std::vector<Price>& returns, Price risk_free_rate) const;
    Price calculate_max_drawdown(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    Price calculate_volatility(const std::vector<Price>& returns) const;
    Price calculate_win_rate(const std::vector<Fill>& trades) const;
    Price calculate_profit_factor(const std::vector<Fill>& trades) const;
    Price calculate_calmar_ratio(Price annualized_return, Price max_drawdown) const;
    int count_total_trades(const std::vector<Fill>& trades) const;
    int count_winning_trades(const std::vector<Fill>& trades) const;
    int count_losing_trades(const std::vector<Fill>& trades) const;
    
    // Trade analysis
    std::vector<Price> group_trades_by_pnl(const std::vector<Fill>& trades) const;
    std::pair<int, int> analyze_trades(const std::vector<Fill>& trades) const;
    Price calculate_average_trade(const std::vector<Fill>& trades) const;
    Price calculate_largest_win(const std::vector<Fill>& trades) const;
    Price calculate_largest_loss(const std::vector<Fill>& trades) const;
    
    // Drawdown analysis
    struct DrawdownPeriod {
        Timestamp start;
        Timestamp end;
        Price max_drawdown;
        int duration_days;
    };
    std::vector<DrawdownPeriod> analyze_drawdowns(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    
    // Risk metrics
    Price calculate_var(const std::vector<Price>& returns, Price confidence = 0.95) const;
    Price calculate_expected_shortfall(const std::vector<Price>& returns, Price confidence = 0.95) const;
    Price calculate_beta(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns) const;
    Price calculate_alpha(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns, Price risk_free_rate) const;
    
public:
    BasicPerformanceAnalyzer() = default;
    
    PerformanceMetrics calculate_metrics(
        const std::vector<std::pair<Timestamp, Price>>& equity_curve,
        const std::vector<Fill>& trades,
        Price initial_capital,
        Price risk_free_rate = 0.02
    ) const override;
    
    std::string generate_report(const PerformanceMetrics& metrics) override;
    nlohmann::json export_to_json(const PerformanceMetrics& metrics) override;
    
    // Additional analysis methods
    std::vector<DrawdownPeriod> get_drawdown_periods(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    std::vector<Price> get_monthly_returns(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    std::vector<Price> get_rolling_sharpe(const std::vector<std::pair<Timestamp, Price>>& equity_curve, int window = 252) const;
    
    // Benchmark comparison
    PerformanceMetrics compare_to_benchmark(
        const std::vector<std::pair<Timestamp, Price>>& equity_curve,
        const std::vector<std::pair<Timestamp, Price>>& benchmark_curve,
        Price initial_capital,
        Price risk_free_rate = 0.02
    ) const;
};

// Advanced performance analyzer with additional metrics
class AdvancedPerformanceAnalyzer : public BasicPerformanceAnalyzer {
private:
    // Additional advanced metrics
    Price calculate_information_ratio(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns) const;
    Price calculate_treynor_ratio(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns, Price risk_free_rate) const;
    Price calculate_jensen_alpha(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns, Price risk_free_rate) const;
    Price calculate_tracking_error(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns) const;
    Price calculate_upside_capture_ratio(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns) const;
    Price calculate_downside_capture_ratio(const std::vector<Price>& returns, const std::vector<Price>& benchmark_returns) const;
    
    // Risk-adjusted metrics
    Price calculate_conditional_sharpe_ratio(const std::vector<Price>& returns, Price risk_free_rate) const;
    Price calculate_omega_ratio(const std::vector<Price>& returns, Price threshold = 0.0) const;
    Price calculate_kappa_ratio(const std::vector<Price>& returns, int moment = 3) const;
    
    // Tail risk metrics
    Price calculate_tail_ratio(const std::vector<Price>& returns) const;
    Price calculate_common_sense_ratio(const std::vector<Price>& returns) const;
    
public:
    AdvancedPerformanceAnalyzer() = default;
    
    PerformanceMetrics calculate_metrics(
        const std::vector<std::pair<Timestamp, Price>>& equity_curve,
        const std::vector<Fill>& trades,
        Price initial_capital,
        Price risk_free_rate = 0.02
    ) const override;
    
    std::string generate_report(const PerformanceMetrics& metrics) override;
    nlohmann::json export_to_json(const PerformanceMetrics& metrics) override;
    
    // Specialized analysis
    nlohmann::json generate_risk_analysis(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
    nlohmann::json generate_trade_analysis(const std::vector<Fill>& trades) const;
    nlohmann::json generate_drawdown_analysis(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const;
};

// Portfolio attribution analyzer
class AttributionAnalyzer {
private:
    struct SymbolAttribution {
        std::string symbol;
        Price total_return;
        Price contribution_to_return;
        Price weight;
        int trade_count;
        Price win_rate;
        Price sharpe_ratio;
    };
    
public:
    // Analyze contribution by symbol
    std::vector<SymbolAttribution> analyze_symbol_attribution(
        const std::vector<Fill>& trades,
        const std::vector<std::pair<Timestamp, Price>>& equity_curve
    ) const;
    
    // Generate attribution report
    std::string generate_attribution_report(const std::vector<SymbolAttribution>& attributions) const;
    
    // Export to JSON
    nlohmann::json export_attribution_to_json(const std::vector<SymbolAttribution>& attributions) const;
};

// Monte Carlo simulation for performance analysis
class MonteCarloAnalyzer {
private:
    int num_simulations_;
    std::mt19937 rng_;
    
public:
    MonteCarloAnalyzer(int num_simulations = 1000);
    
    // Run Monte Carlo simulation on returns
    struct MonteCarloResult {
        std::vector<Price> simulated_returns;
        Price mean_return;
        Price std_return;
        Price confidence_interval_lower;
        Price confidence_interval_upper;
        Price probability_of_loss;
    };
    
    MonteCarloResult run_simulation(const std::vector<Price>& historical_returns, int simulation_length = 252) const;
    
    // Generate Monte Carlo report
    std::string generate_monte_carlo_report(const MonteCarloResult& result) const;
    
    // Export to JSON
    nlohmann::json export_monte_carlo_to_json(const MonteCarloResult& result) const;
};

// Utility functions for performance calculations
namespace performance_utils {
    
    // Date/time utilities
    int get_trading_days_between(const Timestamp& start, const Timestamp& end);
    int get_business_days_in_year(int year = 2024);
    
    // Statistical utilities
    Price calculate_mean(const std::vector<Price>& data);
    Price calculate_std_dev(const std::vector<Price>& data);
    Price calculate_skewness(const std::vector<Price>& data);
    Price calculate_kurtosis(const std::vector<Price>& data);
    Price calculate_percentile(const std::vector<Price>& data, Price percentile);
    
    // Correlation utilities
    Price calculate_correlation(const std::vector<Price>& x, const std::vector<Price>& y);
    std::vector<std::vector<Price>> calculate_correlation_matrix(const std::vector<std::vector<Price>>& data);
    
    // Time series utilities
    std::vector<Price> calculate_rolling_metric(const std::vector<Price>& data, int window, 
                                               std::function<Price(const std::vector<Price>&)> metric_func);
    
    // Export utilities
    bool save_metrics_to_csv(const PerformanceMetrics& metrics, const std::string& filename);
    bool save_equity_curve_to_csv(const std::vector<std::pair<Timestamp, Price>>& equity_curve, 
                                  const std::string& filename);
    bool save_trades_to_csv(const std::vector<Fill>& trades, const std::string& filename);
    
} // namespace performance_utils

} // namespace backtesting 