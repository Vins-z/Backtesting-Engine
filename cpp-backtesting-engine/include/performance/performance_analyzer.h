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

} // namespace backtesting