#pragma once

#include "common/types.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <deque>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace backtesting {

// Enhanced position structure with more detailed tracking
struct EnhancedPosition {
    std::string symbol;
    Quantity quantity = 0;
    Price avg_price = 0.0;
    Price market_value = 0.0;
    Price unrealized_pnl = 0.0;
    Price realized_pnl = 0.0;
    
    // Risk metrics
    Price stop_loss_price = 0.0;
    Price take_profit_price = 0.0;
    double risk_per_trade = 0.0;
    double position_size_pct = 0.0;
    
    // Time tracking
    Timestamp entry_time;
    Timestamp last_update_time;
    int days_held = 0;
    
    // Performance tracking
    double max_favorable_excursion = 0.0; // MFE
    double max_adverse_excursion = 0.0;   // MAE
    Price highest_price = 0.0;
    Price lowest_price = 0.0;
    
    EnhancedPosition() = default;
    explicit EnhancedPosition(const std::string& sym) : symbol(sym) {}
    
    nlohmann::json to_json() const;
    bool is_long() const { return quantity > 0; }
    bool is_short() const { return quantity < 0; }
    bool is_open() const { return quantity != 0; }
};

// Risk management configuration
struct RiskConfig {
    // Position sizing
    double max_position_size_pct = 0.10; // Max 10% of portfolio per position
    double max_total_exposure_pct = 0.80; // Max 80% total exposure
    double min_position_size = 100.0;    // Minimum position value
    
    // Risk limits
    double max_portfolio_risk_pct = 0.02; // Max 2% portfolio risk per trade
    double max_daily_loss_pct = 0.05;     // Max 5% daily loss
    double max_drawdown_pct = 0.20;       // Max 20% drawdown
    
    // Stop losses and take profits
    double default_stop_loss_pct = 0.05;  // 5% stop loss
    double default_take_profit_pct = 0.15; // 15% take profit
    bool use_trailing_stops = true;
    double trailing_stop_pct = 0.03;      // 3% trailing stop
    
    // Position management
    int max_positions = 10;               // Max concurrent positions
    int max_correlation_positions = 3;    // Max correlated positions
    bool allow_shorting = false;
    
    nlohmann::json to_json() const;
    static RiskConfig from_json(const nlohmann::json& j);
};

// Portfolio statistics
struct PortfolioStats {
    Price total_return;
    Price annualized_return;
    Price sharpe_ratio;
    Price max_drawdown;
    Price current_drawdown;
    Price volatility;
    Price win_rate;
    Price profit_factor;
    int total_trades;
    int winning_trades;
    int losing_trades;
    Price largest_win;
    Price largest_loss;
    Price average_win;
    Price average_loss;
    Price average_trade_duration;
    
    // Risk metrics
    Price value_at_risk_95;
    Price expected_shortfall;
    Price calmar_ratio;
    Price sortino_ratio;
    
    // Exposure metrics
    Price current_exposure;
    Price max_exposure;
    Price cash_utilization;
    
    PortfolioStats();
    nlohmann::json to_json() const;
};

// Abstract base class for position sizing strategies
class PositionSizer {
public:
    virtual ~PositionSizer() = default;
    virtual Quantity calculate_position_size(Price cash, Price price, Price risk_per_trade = 0.01) = 0;
};

// Enhanced portfolio manager
class PortfolioManager {
private:
    Price initial_capital_;
    Price current_cash_;
    Price total_value_;
    Price peak_value_; // For drawdown calculation
    
    // Enhanced position tracking
    std::unordered_map<std::string, EnhancedPosition> positions_;
    std::unordered_map<std::string, Price> current_prices_;
    std::unordered_map<std::string, OHLC> current_market_data_;
    
    // Trade and performance tracking
    std::vector<Fill> trade_history_;
    std::vector<std::pair<Timestamp, Price>> equity_curve_;
    std::vector<std::pair<Timestamp, Price>> drawdown_curve_;

    // Lot ledger for correct realized P&L (FIFO)
    struct Lot {
        Quantity quantity = 0.0;
        Price cost_per_share = 0.0; // includes buy commission allocated per share
    };
    std::unordered_map<std::string, std::deque<Lot>> open_lots_;
    std::unordered_map<std::string, Price> realized_pnl_by_symbol_;
    Price realized_pnl_total_ = 0.0;
    
    // Risk management
    RiskConfig risk_config_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // Position sizing strategies
    std::unique_ptr<PositionSizer> position_sizer_;
    
    // State tracking
    int order_id_counter_;
    Timestamp last_update_time_;
    
    // Risk monitoring
    Price daily_start_value_;
    Price daily_pnl_;
    std::vector<std::pair<Timestamp, Price>> daily_pnl_history_;
    
    // Multi-asset portfolio tracking
    std::unordered_map<std::string, std::string> symbol_sectors_; // symbol -> sector
    std::unordered_map<std::string, std::string> symbol_industries_; // symbol -> industry
    std::unordered_map<std::string, std::vector<Price>> symbol_returns_; // symbol -> returns history
    mutable std::unordered_map<std::string, std::unordered_map<std::string, double>> correlation_cache_; // symbol1 -> symbol2 -> correlation
    
    // Internal methods
    void update_position_metrics(EnhancedPosition& position, const OHLC& market_data);
    void check_risk_limits();
    void update_stop_losses();
    bool validate_new_position(const std::string& symbol, Quantity quantity, Price price);
    double calculate_portfolio_correlation(const std::string& new_symbol) const;
    void rebalance_if_needed();
    
    // Multi-asset portfolio methods
    void update_symbol_metadata(const std::string& symbol, const std::string& sector, const std::string& industry);
    double calculate_correlation(const std::string& symbol1, const std::string& symbol2) const;
    // Correlation matrix computation
    std::vector<std::vector<double>> compute_correlation_matrix(const std::vector<std::string>& symbols) const;
    std::unordered_map<std::string, double> get_sector_exposure() const;
    std::unordered_map<std::string, double> get_industry_exposure() const;
    double calculate_portfolio_variance() const;
    double calculate_portfolio_volatility() const;
    double calculate_diversification_ratio() const;
    
public:
    explicit PortfolioManager(Price initial_capital, const RiskConfig& risk_config = RiskConfig{});
    ~PortfolioManager() = default;
    
    // Market data updates
    void update_market_data(const std::string& symbol, const OHLC& data);
    OHLC get_current_market_data(const std::string& symbol) const;
    
    // Portfolio updates
    void update_portfolio(const Timestamp& current_time);
    void start_new_trading_day(const Timestamp& date);
    
    // Order generation with enhanced risk management
    Order generate_order(const SignalEvent& signal);
    Order generate_stop_loss_order(const std::string& symbol);
    Order generate_take_profit_order(const std::string& symbol);
    std::vector<Order> generate_risk_management_orders();
    
    // Position management
    void update_fill(const Fill& fill);
    bool pre_validate_order(const Order& order, const OHLC& market_data) const;
    EnhancedPosition get_position(const std::string& symbol) const;
    std::unordered_map<std::string, EnhancedPosition> get_all_positions() const;
    
    // Position sizing and risk management
    Quantity calculate_optimal_position_size(
        const std::string& symbol, 
        Price entry_price, 
        Price stop_loss_price,
        double risk_per_trade = 0.0 // Use default if 0
    ) const;
    
    double calculate_position_risk(const std::string& symbol, Quantity quantity, Price entry_price, Price stop_price) const;
    Quantity calculate_position_size(const std::string& symbol, Price price, double allocation) const;
    bool can_afford_trade(const std::string& symbol, Quantity quantity, Price price) const;
    bool is_within_risk_limits(const std::string& symbol, Quantity quantity, Price price) const;
    
    // Portfolio metrics
    Price get_cash() const { return current_cash_; }
    Price get_total_value() const { return total_value_; }
    Price get_unrealized_pnl() const;
    Price get_realized_pnl() const;
    Price get_total_return() const;
    
    // Multi-asset portfolio analytics
    nlohmann::json get_portfolio_analytics() const;
    nlohmann::json get_correlation_matrix_json() const;
    nlohmann::json get_sector_exposure_json() const;
    Price get_current_drawdown() const;
    Price get_max_drawdown() const;
    double get_current_exposure() const;
    
    // Performance analysis
    PortfolioStats calculate_portfolio_stats() const;
    std::vector<std::pair<Timestamp, Price>> get_equity_curve() const;
    std::vector<std::pair<Timestamp, Price>> get_drawdown_curve() const;
    std::vector<Fill> get_trade_history() const;
    
    // Risk monitoring
    bool is_risk_limit_exceeded() const;
    std::vector<std::string> get_risk_warnings() const;
    nlohmann::json get_risk_report() const;
    
    // Configuration
    void set_risk_config(const RiskConfig& config) { risk_config_ = config; }
    const RiskConfig& get_risk_config() const { return risk_config_; }
    void set_position_sizer(std::unique_ptr<PositionSizer> sizer);
    
    // Reporting
    nlohmann::json get_portfolio_summary() const;
    nlohmann::json get_position_summary() const;
    void save_portfolio_state(const std::string& filename) const;
    bool load_portfolio_state(const std::string& filename);
    
private:
    void record_equity_point(const Timestamp& timestamp);
    Price calculate_commission(Quantity quantity, Price price) const;
    void update_position(const Fill& fill);
    
    static constexpr Price COMMISSION_RATE = 0.001; // 0.1%
};

// Advanced position sizers
class PercentEquitySizer : public PositionSizer {
private:
    double equity_percentage_;
    
public:
    explicit PercentEquitySizer(double equity_percentage = 0.1);
    
    Quantity calculate_position_size(Price cash, Price price, Price risk_per_trade = 0.01) override;
};

class KellyPositionSizer : public PositionSizer {
private:
    double win_rate_;
    double avg_win_loss_ratio_;
    int lookback_trades_;
    
public:
    KellyPositionSizer(double win_rate = 0.5, double avg_win_loss_ratio = 1.0, int lookback_trades = 50);
    
    Quantity calculate_position_size(Price cash, Price price, Price risk_per_trade = 0.01) override;
    void update_from_trade_history(const std::vector<Fill>& trades);
};

class VolatilityPositionSizer : public PositionSizer {
private:
    double target_volatility_;
    int lookback_period_;
    
public:
    VolatilityPositionSizer(double target_volatility = 0.15, int lookback_period = 20);
    
    Quantity calculate_position_size(Price cash, Price price, Price risk_per_trade = 0.01) override;
    void set_price_history(const std::vector<OHLC>& price_history) { price_history_ = price_history; }
    
private:
    std::vector<OHLC> price_history_;
    double calculate_historical_volatility() const;
};

class RiskParityPositionSizer : public PositionSizer {
private:
    std::unordered_map<std::string, double> symbol_volatilities_;
    double target_portfolio_volatility_;
    
public:
    explicit RiskParityPositionSizer(double target_volatility = 0.12);
    
    Quantity calculate_position_size(Price cash, Price price, Price risk_per_trade = 0.01) override;
    void update_symbol_volatility(const std::string& symbol, double volatility);
};

} // namespace backtesting 