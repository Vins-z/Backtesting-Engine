#pragma once

#include "common/types.h"
#include "portfolio/portfolio_manager.h"
#include <memory>
#include <limits>

namespace backtesting {

class RiskManager {
public:
    virtual ~RiskManager() = default;
    
    // Check if signal should be processed based on risk rules
    virtual bool check_signal(const SignalEvent& signal, const PortfolioManager& portfolio) = 0;
    
    // Check if order is allowed based on risk rules
    virtual bool is_order_allowed(const Order& order, const PortfolioManager& portfolio) = 0;
    
    // Adjust order size based on risk parameters
    virtual Order adjust_order_size(const Order& order, const PortfolioManager& portfolio) = 0;
    
    // Check for stop-loss conditions
    virtual bool should_trigger_stop_loss(const std::string& symbol, Price current_price, 
                                         const PortfolioManager& portfolio) = 0;
    
    // Generate stop-loss orders
    virtual std::vector<Order> generate_stop_loss_orders(const PortfolioManager& portfolio, 
                                                        const std::unordered_map<std::string, Price>& current_prices) = 0;
    
    // Check for stop losses and generate orders  
    virtual std::vector<Order> check_stop_losses(const PortfolioManager& portfolio, const Timestamp& current_time) = 0;
    
    // Get risk metrics
    virtual std::unordered_map<std::string, double> get_risk_metrics(const PortfolioManager& portfolio) = 0;
    
    // Reset risk manager state
    virtual void reset() = 0;
};

class BasicRiskManager : public RiskManager {
private:
    Price max_position_size_;           // Maximum position size as % of portfolio
    Price max_portfolio_risk_;          // Maximum portfolio risk as % of equity
    Price stop_loss_percentage_;        // Stop loss as % of entry price
    Price max_correlation_;             // Maximum correlation between positions
    Price max_sector_exposure_;         // Maximum exposure to single sector
    Price max_daily_loss_;              // Maximum daily loss as % of equity
    Price min_cash_reserve_;            // Minimum cash reserve as % of equity
    
    // Breakeven and trailing stop settings
    bool enable_breakeven_;
    Price breakeven_trigger_R_;         // Move SL to entry after this R-multiple profit
    bool enable_trailing_stop_;
    std::string trailing_type_;          // "PERCENT" or "ATR"
    Price trailing_value_;              // Trailing distance (% or ATR multiples)
    
    // Position sizing parameters
    std::unique_ptr<PositionSizer> position_sizer_;
    
    // Risk tracking
    std::unordered_map<std::string, Price> entry_prices_;
    std::unordered_map<std::string, Price> stop_prices_;
    std::unordered_map<std::string, Price> take_profit_prices_;   // Take profit target prices
    std::unordered_map<std::string, Price> initial_risk_per_share_; // R = (entry - initial_stop) per share
    std::unordered_map<std::string, Price> highest_prices_;        // For trailing stops
    Price take_profit_percentage_;                                  // Default take profit percentage
    Price daily_pnl_;
    Timestamp last_reset_time_;
    
    // Helper functions
    bool check_position_size_limit(const Order& order, const PortfolioManager& portfolio);
    bool check_portfolio_risk_limit(const Order& order, const PortfolioManager& portfolio);
    bool check_cash_reserve_limit(const Order& order, const PortfolioManager& portfolio);
    bool check_daily_loss_limit(const PortfolioManager& portfolio);
    bool check_correlation_limit(const Order& order, const PortfolioManager& portfolio);
    
    Quantity calculate_max_position_size(const Order& order, const PortfolioManager& portfolio);
    Price calculate_position_risk(const std::string& symbol, Quantity quantity, Price price);
    
public:
    BasicRiskManager(Price max_position_size = 0.1,
                    Price max_portfolio_risk = 0.02,
                    Price stop_loss_percentage = 0.05,
                    Price max_daily_loss = 0.05,
                    Price min_cash_reserve = 0.1,
                    bool enable_breakeven = false,
                    Price breakeven_trigger_R = 1.0,
                    bool enable_trailing_stop = false,
                    const std::string& trailing_type = "PERCENT",
                    Price trailing_value = 2.0,
                    Price take_profit_percentage = 0.15);
    
    // Set position sizer
    void set_position_sizer(std::unique_ptr<PositionSizer> sizer);
    
    // Signal checks
    bool check_signal(const SignalEvent& signal, const PortfolioManager& portfolio) override;
    
    // Risk checks
    bool is_order_allowed(const Order& order, const PortfolioManager& portfolio) override;
    Order adjust_order_size(const Order& order, const PortfolioManager& portfolio) override;
    
    // Stop loss management
    bool should_trigger_stop_loss(const std::string& symbol, Price current_price, 
                                 const PortfolioManager& portfolio) override;
    std::vector<Order> generate_stop_loss_orders(const PortfolioManager& portfolio, 
                                                const std::unordered_map<std::string, Price>& current_prices) override;
    
    // Check for stop losses with timestamp
    std::vector<Order> check_stop_losses(const PortfolioManager& portfolio, const Timestamp& current_time) override;
    
    // Check for take profit conditions
    bool should_trigger_take_profit(const std::string& symbol, Price current_price, 
                                   const PortfolioManager& portfolio) const;
    
    // Generate take profit orders
    std::vector<Order> generate_take_profit_orders(const PortfolioManager& portfolio, 
                                                  const std::unordered_map<std::string, Price>& current_prices) const;
    
    // Check for both stop losses and take profits
    std::vector<Order> check_risk_orders(const PortfolioManager& portfolio, const Timestamp& current_time);
    
    // Update entry prices for stop loss calculations
    void update_entry_price(const std::string& symbol, Price entry_price, Price take_profit_pct = 0.15);
    
    // Risk metrics
    std::unordered_map<std::string, double> get_risk_metrics(const PortfolioManager& portfolio) override;
    
    // Update daily P&L
    void update_daily_pnl(Price pnl);
    
    // Reset for new day
    void reset_daily() { daily_pnl_ = 0.0; last_reset_time_ = std::chrono::system_clock::now(); }
    
    // Reset all
    void reset() override;
    
    // Setters
    void set_max_position_size(Price size) { max_position_size_ = size; }
    void set_max_portfolio_risk(Price risk) { max_portfolio_risk_ = risk; }
    void set_stop_loss_percentage(Price pct) { stop_loss_percentage_ = pct; }
    void set_max_daily_loss(Price loss) { max_daily_loss_ = loss; }
    void set_min_cash_reserve(Price reserve) { min_cash_reserve_ = reserve; }
    void set_breakeven_enabled(bool enabled) { enable_breakeven_ = enabled; }
    void set_breakeven_trigger_R(Price R) { breakeven_trigger_R_ = R; }
    void set_trailing_stop_enabled(bool enabled) { enable_trailing_stop_ = enabled; }
    void set_trailing_stop_type(const std::string& type) { trailing_type_ = type; }
    void set_trailing_stop_value(Price value) { trailing_value_ = value; }
    void set_take_profit_percentage(Price pct) { take_profit_percentage_ = pct; }
};

// Advanced risk manager with correlation and sector analysis
class AdvancedRiskManager : public RiskManager {
private:
    Price max_position_size_;
    Price max_portfolio_risk_;
    Price stop_loss_percentage_;
    Price max_correlation_;
    Price max_sector_exposure_;
    Price max_daily_loss_;
    Price min_cash_reserve_;
    Price var_confidence_;              // VaR confidence level
    int var_lookback_;                  // VaR lookback period
    
    // Sector mappings
    std::unordered_map<std::string, std::string> symbol_sectors_;
    
    // Correlation matrix
    std::unordered_map<std::string, std::unordered_map<std::string, Price>> correlation_matrix_;
    
    // Historical data for VaR calculation
    std::unordered_map<std::string, std::vector<Price>> historical_returns_;
    
    // Position sizing
    std::unique_ptr<PositionSizer> position_sizer_;
    
    // Risk tracking
    std::unordered_map<std::string, Price> entry_prices_;
    std::unordered_map<std::string, Price> stop_prices_;
    Price daily_pnl_;
    Timestamp last_reset_time_;
    
    // Advanced risk calculations
    Price calculate_var(const PortfolioManager& portfolio, Price confidence = 0.95);
    Price calculate_expected_shortfall(const PortfolioManager& portfolio, Price confidence = 0.95);
    Price calculate_portfolio_correlation(const Order& order, const PortfolioManager& portfolio);
    Price calculate_sector_exposure(const std::string& sector, const PortfolioManager& portfolio);
    
    // Update correlation matrix
    void update_correlation_matrix(const std::unordered_map<std::string, std::vector<Price>>& returns);
    
public:
    AdvancedRiskManager(Price max_position_size = 0.1,
                       Price max_portfolio_risk = 0.02,
                       Price stop_loss_percentage = 0.05,
                       Price max_correlation = 0.7,
                       Price max_sector_exposure = 0.3,
                       Price max_daily_loss = 0.05,
                       Price min_cash_reserve = 0.1,
                       Price var_confidence = 0.95,
                       int var_lookback = 252);
    
    // Set sector for symbol
    void set_symbol_sector(const std::string& symbol, const std::string& sector);
    
    // Set position sizer
    void set_position_sizer(std::unique_ptr<PositionSizer> sizer);
    
    // Signal checks
    bool check_signal(const SignalEvent& signal, const PortfolioManager& portfolio) override;
    
    // Risk checks
    bool is_order_allowed(const Order& order, const PortfolioManager& portfolio) override;
    Order adjust_order_size(const Order& order, const PortfolioManager& portfolio) override;
    
    // Stop loss management
    bool should_trigger_stop_loss(const std::string& symbol, Price current_price, 
                                 const PortfolioManager& portfolio) override;
    std::vector<Order> generate_stop_loss_orders(const PortfolioManager& portfolio, 
                                                const std::unordered_map<std::string, Price>& current_prices) override;
    
    // Check for stop losses with timestamp
    std::vector<Order> check_stop_losses(const PortfolioManager& portfolio, const Timestamp& current_time) override;
    
    // Update entry prices
    void update_entry_price(const std::string& symbol, Price entry_price);
    
    // Risk metrics
    std::unordered_map<std::string, double> get_risk_metrics(const PortfolioManager& portfolio) override;
    
    // Update historical data
    void update_historical_returns(const std::string& symbol, const std::vector<Price>& returns);
    
    // Reset
    void reset() override;
};

// Utility functions for risk calculations
namespace risk_utils {
    
    // Calculate correlation between two return series
    Price calculate_correlation(const std::vector<Price>& returns1, const std::vector<Price>& returns2);
    
    // Calculate Value at Risk
    Price calculate_var(const std::vector<Price>& returns, Price confidence = 0.95);
    
    // Calculate Expected Shortfall (Conditional VaR)
    Price calculate_expected_shortfall(const std::vector<Price>& returns, Price confidence = 0.95);
    
    // Calculate maximum drawdown
    Price calculate_max_drawdown(const std::vector<Price>& equity_curve);
    
    // Calculate Sharpe ratio
    Price calculate_sharpe_ratio(const std::vector<Price>& returns, Price risk_free_rate = 0.02);
    
    // Calculate portfolio volatility
    Price calculate_portfolio_volatility(const std::vector<std::string>& symbols,
                                        const std::vector<Price>& weights,
                                        const std::unordered_map<std::string, std::unordered_map<std::string, Price>>& correlation_matrix,
                                        const std::unordered_map<std::string, Price>& volatilities);
    
} // namespace risk_utils

} // namespace backtesting 