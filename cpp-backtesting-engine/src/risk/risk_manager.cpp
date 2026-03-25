#include "risk/risk_manager.h"
#include "portfolio/portfolio_manager.h"
#include <cmath>

namespace backtesting {

// BasicRiskManager Implementation
BasicRiskManager::BasicRiskManager(Price max_position_size, Price max_portfolio_risk, 
                                 Price stop_loss_percentage, Price max_daily_loss, Price min_cash_reserve,
                                 bool enable_breakeven, Price breakeven_trigger_R,
                                 bool enable_trailing_stop, const std::string& trailing_type, Price trailing_value,
                                 Price take_profit_percentage)
    : max_position_size_(max_position_size), max_portfolio_risk_(max_portfolio_risk),
      stop_loss_percentage_(stop_loss_percentage), max_daily_loss_(max_daily_loss),
      min_cash_reserve_(min_cash_reserve), 
      enable_breakeven_(enable_breakeven), breakeven_trigger_R_(breakeven_trigger_R),
      enable_trailing_stop_(enable_trailing_stop), trailing_type_(trailing_type), trailing_value_(trailing_value),
      take_profit_percentage_(take_profit_percentage), daily_pnl_(0.0) {
    last_reset_time_ = std::chrono::system_clock::now();
}

bool BasicRiskManager::check_signal(const SignalEvent& signal, const PortfolioManager& portfolio) {
    // Check daily loss limit
    if (!check_daily_loss_limit(portfolio)) {
        return false;
    }
    
    // For buy signals, check if we can afford the position
    if (signal.signal == Signal::BUY) {
        // Check position size limit
        auto position = portfolio.get_position(signal.symbol);
        Price current_exposure = std::abs(position.quantity * position.market_value) / portfolio.get_total_value();
        
        if (current_exposure >= max_position_size_) {
            return false;
        }
        
        // Check cash reserve
        Price cash_after_trade = portfolio.get_cash() - (position.market_value * 0.1); // Estimate 10% allocation
        if (cash_after_trade / portfolio.get_total_value() < min_cash_reserve_) {
            return false;
        }
    }
    
    return true;
}

bool BasicRiskManager::is_order_allowed(const Order& order, const PortfolioManager& portfolio) {
    return check_position_size_limit(order, portfolio) && 
           check_cash_reserve_limit(order, portfolio) &&
           check_daily_loss_limit(portfolio);
}

Order BasicRiskManager::adjust_order_size(const Order& order, const PortfolioManager& portfolio) {
    Quantity max_size = calculate_max_position_size(order, portfolio);
    Quantity adjusted_quantity = std::min(order.quantity, max_size);
    
    Order adjusted_order = order;
    adjusted_order.quantity = adjusted_quantity;
    return adjusted_order;
}

bool BasicRiskManager::should_trigger_stop_loss(const std::string& symbol, Price current_price, 
                                               [[maybe_unused]] const PortfolioManager& portfolio) {
    auto it = entry_prices_.find(symbol);
    if (it == entry_prices_.end()) return false;
    
    Price entry_price = it->second;
    Price stop_price = entry_price * (1.0 - stop_loss_percentage_);
    
    return current_price <= stop_price;
}

std::vector<Order> BasicRiskManager::generate_stop_loss_orders(const PortfolioManager& portfolio, 
                                                             const std::unordered_map<std::string, Price>& current_prices) {
    std::vector<Order> stop_orders;
    auto positions = portfolio.get_all_positions();
    
    for (const auto& [symbol, position] : positions) {
        if (position.quantity <= 0) continue;
        
        auto price_it = current_prices.find(symbol);
        if (price_it == current_prices.end()) continue;
        
        if (should_trigger_stop_loss(symbol, price_it->second, portfolio)) {
            // Execute stops at the worst price observed within the bar to stay conservative.
            // For long positions, a stop-loss SELL uses the bar's LOW.
            auto market_data = portfolio.get_current_market_data(symbol);
            Price execution_price = (market_data.close > 0.0) ? market_data.low : price_it->second;
            
            Order stop_order(0, symbol, OrderType::MARKET, OrderSide::SELL, 
                           position.quantity, execution_price, 0);
            stop_orders.push_back(stop_order);
        }
    }
    
    return stop_orders;
}

std::vector<Order> BasicRiskManager::check_stop_losses(const PortfolioManager& portfolio, [[maybe_unused]] const Timestamp& current_time) {
    std::vector<Order> stop_orders;
    auto positions = portfolio.get_all_positions();
    
    for (const auto& [symbol, position] : positions) {
        if (position.quantity <= 0) continue;
        
        Price current_price = position.market_value / position.quantity;
        auto entry_it = entry_prices_.find(symbol);
        auto stop_it = stop_prices_.find(symbol);
        
        if (entry_it == entry_prices_.end() || stop_it == stop_prices_.end()) continue;
        
        Price entry_price = entry_it->second;
        Price stop_price = stop_it->second;
        
        // Update highest price for trailing stops
        auto highest_it = highest_prices_.find(symbol);
        if (highest_it == highest_prices_.end()) {
            highest_prices_[symbol] = current_price;
        } else {
            highest_prices_[symbol] = std::max(highest_it->second, current_price);
        }
        Price highest_price = highest_prices_[symbol];
        
        // Breakeven logic: move stop to entry after R-multiple profit
        if (enable_breakeven_ && entry_price > 0) {
            auto risk_it = initial_risk_per_share_.find(symbol);
            if (risk_it != initial_risk_per_share_.end() && risk_it->second > 0) {
                Price R_profit = (current_price - entry_price) / risk_it->second;
                if (R_profit >= breakeven_trigger_R_ && stop_price < entry_price) {
                    // Move stop to breakeven
                    stop_prices_[symbol] = entry_price;
                    stop_price = entry_price;
                }
            }
        }
        
        // Trailing stop logic: update stop as price moves favorably
        if (enable_trailing_stop_ && highest_price > entry_price) {
            Price new_stop_price = stop_price;
            
            if (trailing_type_ == "PERCENT") {
                // Trail by percentage
                new_stop_price = highest_price * (1.0 - trailing_value_ / 100.0);
            } else if (trailing_type_ == "ATR") {
                // Trail by ATR multiples (ATR is not computed in this basic manager).
                // Use a percentage proxy instead.
                Price atr_approx = entry_price * 0.02; // Approximate 2% ATR
                new_stop_price = highest_price - (atr_approx * trailing_value_);
            }
            
            // Only move stop up, never down
            if (new_stop_price > stop_price) {
                stop_prices_[symbol] = new_stop_price;
                stop_price = new_stop_price;
            }
        }
        
        // Check if stop loss triggered
        if (current_price <= stop_price) {
            // Execute stops at the worst price observed within the bar to stay conservative.
            // For long positions, a stop-loss SELL uses the bar's LOW.
            // Get market data to access OHLC
            auto market_data = portfolio.get_current_market_data(symbol);
            Price execution_price = (market_data.close > 0.0) ? market_data.low : current_price;
            
            Order stop_order(0, symbol, OrderType::MARKET, OrderSide::SELL, 
                           position.quantity, execution_price, 0);
            stop_orders.push_back(stop_order);
        }
    }
    
    return stop_orders;
}

bool BasicRiskManager::should_trigger_take_profit(const std::string& symbol, Price current_price, 
                                                  [[maybe_unused]] const PortfolioManager& portfolio) const {
    auto it = entry_prices_.find(symbol);
    auto tp_it = take_profit_prices_.find(symbol);
    
    if (it == entry_prices_.end() || tp_it == take_profit_prices_.end()) {
        return false;
    }

    Price take_profit_price = tp_it->second;
    
    // For long positions, trigger when price reaches or exceeds take profit
    return current_price >= take_profit_price;
}

std::vector<Order> BasicRiskManager::generate_take_profit_orders(const PortfolioManager& portfolio, 
                                                                 const std::unordered_map<std::string, Price>& current_prices) const {
    std::vector<Order> tp_orders;
    auto positions = portfolio.get_all_positions();
    
    for (const auto& [symbol, position] : positions) {
        if (position.quantity <= 0) continue; // Only long positions
        
        auto price_it = current_prices.find(symbol);
        if (price_it == current_prices.end()) continue;
        
        if (should_trigger_take_profit(symbol, price_it->second, portfolio)) {
            // Execute take-profits conservatively using the worst price within the bar.
            // For long positions, a take-profit SELL uses the bar's LOW.
            auto market_data = portfolio.get_current_market_data(symbol);
            Price execution_price = (market_data.close > 0.0) ? market_data.low : price_it->second;
            
            Order tp_order(0, symbol, OrderType::MARKET, OrderSide::SELL, 
                          position.quantity, execution_price, 0);
            tp_orders.push_back(tp_order);
        }
    }
    
    return tp_orders;
}

std::vector<Order> BasicRiskManager::check_risk_orders(const PortfolioManager& portfolio, const Timestamp& current_time) {
    std::vector<Order> risk_orders;
    
    // Check stop losses
    auto stop_orders = check_stop_losses(portfolio, current_time);
    risk_orders.insert(risk_orders.end(), stop_orders.begin(), stop_orders.end());
    
    // Check take profits - need to get current prices
    std::unordered_map<std::string, Price> current_prices;
    auto positions = portfolio.get_all_positions();
    for (const auto& [symbol, position] : positions) {
        if (position.quantity > 0) {
            // Get current price from portfolio
            auto market_data = portfolio.get_current_market_data(symbol);
            if (market_data.close > 0.0) {
                current_prices[symbol] = market_data.close;
            }
        }
    }
    
    auto tp_orders = generate_take_profit_orders(portfolio, current_prices);
    risk_orders.insert(risk_orders.end(), tp_orders.begin(), tp_orders.end());
    
    return risk_orders;
}

std::unordered_map<std::string, double> BasicRiskManager::get_risk_metrics(const PortfolioManager& portfolio) {
    return {
        {"max_position_size", max_position_size_},
        {"stop_loss_percentage", stop_loss_percentage_},
        {"daily_pnl", daily_pnl_},
        {"cash_reserve_ratio", portfolio.get_cash() / portfolio.get_total_value()},
        {"portfolio_leverage", (portfolio.get_total_value() - portfolio.get_cash()) / portfolio.get_total_value()}
    };
}

void BasicRiskManager::update_entry_price(const std::string& symbol, Price entry_price, Price take_profit_pct) {
    entry_prices_[symbol] = entry_price;
    Price initial_stop = entry_price * (1.0 - stop_loss_percentage_);
    stop_prices_[symbol] = initial_stop;
    
    // Set take profit price (use provided percentage or default)
    Price tp_pct = (take_profit_pct > 0.0) ? take_profit_pct : take_profit_percentage_;
    take_profit_prices_[symbol] = entry_price * (1.0 + tp_pct);
    
    initial_risk_per_share_[symbol] = entry_price - initial_stop; // R per share
    highest_prices_[symbol] = entry_price; // Initialize highest price
}

void BasicRiskManager::update_daily_pnl(Price pnl) {
    daily_pnl_ += pnl;
}

void BasicRiskManager::reset() {
    entry_prices_.clear();
    stop_prices_.clear();
    take_profit_prices_.clear();
    initial_risk_per_share_.clear();
    highest_prices_.clear();
    daily_pnl_ = 0.0;
    last_reset_time_ = std::chrono::system_clock::now();
}

// Helper methods
bool BasicRiskManager::check_position_size_limit(const Order& order, const PortfolioManager& portfolio) {
    auto position = portfolio.get_position(order.symbol);
    Price new_exposure = std::abs((position.quantity + order.quantity) * order.price) / portfolio.get_total_value();
    return new_exposure <= max_position_size_;
}

bool BasicRiskManager::check_portfolio_risk_limit(const Order& order, const PortfolioManager& portfolio) {
    // Simple risk check - ensure we don't exceed portfolio risk limit
    Price position_risk = calculate_position_risk(order.symbol, order.quantity, order.price);
    return position_risk <= max_portfolio_risk_ * portfolio.get_total_value();
}

bool BasicRiskManager::check_cash_reserve_limit(const Order& order, const PortfolioManager& portfolio) {
    if (order.side == OrderSide::BUY) {
        Price order_cost = order.quantity * order.price * 1.001; // Include commission estimate
        Price remaining_cash = portfolio.get_cash() - order_cost;
        return remaining_cash / portfolio.get_total_value() >= min_cash_reserve_;
    }
    return true;
}

bool BasicRiskManager::check_daily_loss_limit(const PortfolioManager& portfolio) {
    Price current_return = portfolio.get_total_return();
    return current_return > -max_daily_loss_;
}

Quantity BasicRiskManager::calculate_max_position_size(const Order& order, const PortfolioManager& portfolio) {
    Price max_value = portfolio.get_total_value() * max_position_size_;
    return static_cast<Quantity>(max_value / order.price);
}

Price BasicRiskManager::calculate_position_risk([[maybe_unused]] const std::string& symbol, Quantity quantity, Price price) {
    // Simple risk calculation - assume 2% volatility
    return quantity * price * 0.02;
}

} // namespace backtesting 