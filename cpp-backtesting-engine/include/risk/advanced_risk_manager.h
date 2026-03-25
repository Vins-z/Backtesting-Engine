#pragma once

#include "common/types.h"
#include "risk/risk_manager.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>

namespace backtesting {

// Extended risk analytics manager with advanced metrics
// Note: This extends the existing AdvancedRiskManager functionality
class ExtendedRiskAnalytics {
public:
    ExtendedRiskAnalytics();
    ~ExtendedRiskAnalytics() = default;
    
    // Portfolio-level risk management
    bool check_portfolio_risk(const PortfolioManager& portfolio) const;
    
    // Advanced position sizing
    Quantity calculate_kelly_position_size(
        const std::string& symbol,
        Price current_price,
        const PortfolioManager& portfolio,
        double win_rate,
        double avg_win,
        double avg_loss
    ) const;
    
    Quantity calculate_optimal_f_position_size(
        const std::string& symbol,
        Price current_price,
        const PortfolioManager& portfolio,
        double win_rate,
        double avg_win,
        double avg_loss
    ) const;
    
    // Dynamic risk adjustment
    void adjust_risk_parameters(
        const std::vector<Fill>& recent_trades,
        const std::vector<std::pair<Timestamp, Price>>& equity_curve
    );
    
    // Correlation-based risk
    bool check_correlation_risk(
        const std::string& symbol,
        const PortfolioManager& portfolio,
        const std::vector<std::string>& existing_positions
    ) const;
    
    // Volatility-based position sizing
    Quantity calculate_volatility_adjusted_size(
        const std::string& symbol,
        Price current_price,
        const PortfolioManager& portfolio,
        int lookback_period = 20
    ) const;
    
    // Maximum adverse excursion (MAE) tracking
    void track_mae(const std::string& symbol, Price entry_price, Price current_price);
    bool check_mae_limits(const std::string& symbol) const;
    
    // Time-based risk controls
    bool check_time_based_risk(const Timestamp& current_time) const;
    void set_trading_hours(const std::string& start_time, const std::string& end_time);
    
    // News-based risk management
    void set_news_events(const std::vector<std::string>& high_impact_events);
    bool check_news_risk(const Timestamp& current_time) const;
    
    // Regime-based risk adjustment
    void set_market_regime(const std::string& regime);
    void adjust_risk_for_regime(const std::string& regime);
    
    // Machine learning risk prediction
    double predict_risk_score(
        const std::string& symbol,
        Price current_price,
        const PortfolioManager& portfolio
    ) const;
    
    // Stress testing
    std::vector<double> stress_test_portfolio(
        const PortfolioManager& portfolio,
        const std::vector<double>& stress_scenarios
    ) const;
    
    // Risk reporting
    nlohmann::json generate_risk_report(const PortfolioManager& portfolio) const;
    
    // Value at Risk (VaR) calculations
    double calculate_var_historical(
        const PortfolioManager& portfolio,
        double confidence_level = 0.95,
        int lookback_period = 252
    ) const;
    
    double calculate_var_parametric(
        const PortfolioManager& portfolio,
        double confidence_level = 0.95
    ) const;
    
    // Conditional VaR (Expected Shortfall)
    double calculate_cvar(
        const PortfolioManager& portfolio,
        double confidence_level = 0.95,
        int lookback_period = 252
    ) const;
    
    // Sortino Ratio (downside deviation)
    double calculate_sortino_ratio(
        const PortfolioManager& portfolio,
        double risk_free_rate = 0.0
    ) const;
    
    // Calmar Ratio (return / max drawdown)
    double calculate_calmar_ratio(
        const PortfolioManager& portfolio
    ) const;
    
    // Drawdown analysis
    struct DrawdownAnalysis {
        double max_drawdown;
        double current_drawdown;
        int drawdown_duration_days;
        int recovery_duration_days;
        Timestamp drawdown_start;
        Timestamp drawdown_end;
    };
    
    DrawdownAnalysis analyze_drawdowns(
        const PortfolioManager& portfolio
    ) const;

private:
    // Advanced risk parameters
    struct AdvancedRiskParams {
        double kelly_fraction = 0.25;  // Fraction of Kelly criterion to use
        double max_correlation = 0.7;  // Maximum correlation between positions
        double volatility_multiplier = 1.0;  // Volatility adjustment multiplier
        double mae_threshold = 0.05;  // Maximum adverse excursion threshold
        double regime_risk_multiplier = 1.0;  // Risk multiplier for different regimes
        int news_impact_hours = 2;  // Hours around news events to reduce risk
    };
    
    AdvancedRiskParams params_;
    
    // Risk tracking
    std::unordered_map<std::string, Price> mae_tracking_;
    std::vector<std::string> high_impact_events_;
    std::string current_regime_;
    std::string trading_start_time_;
    std::string trading_end_time_;
    
    // Helper methods
    double calculate_correlation(
        const std::vector<double>& returns1,
        const std::vector<double>& returns2
    ) const;
    
    double calculate_volatility(
        const std::vector<Price>& prices,
        int lookback_period
    ) const;
    
    bool is_trading_hours(const Timestamp& time) const;
    bool is_news_time(const Timestamp& time) const;
    std::string detect_market_regime(const std::vector<Price>& prices) const;
};

} // namespace backtesting
