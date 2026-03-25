#pragma once

#include "common/types.h"
#include <queue>
#include <memory>
#include <random>

namespace backtesting {

class ExecutionHandler {
protected:
    Price commission_rate_;
    Price slippage_rate_;
    
public:
    ExecutionHandler(Price commission_rate = 0.001, Price slippage_rate = 0.001);
    virtual ~ExecutionHandler() = default;
    
    // Execute an order and return a fill
    virtual Fill execute_order(const Order& order, const OHLC& current_data) = 0;
    
    // Calculate commission for a trade
    virtual Price calculate_commission(Quantity quantity, Price price) const;
    
    // Calculate slippage for a trade
    virtual Price calculate_slippage(Price price, OrderSide side) const;
    
    // Configuration methods
    virtual void set_commission_rate(Price rate) { commission_rate_ = rate; }
    virtual void set_slippage_rate(Price rate) { slippage_rate_ = rate; }
    
    // Get execution statistics
    virtual std::unordered_map<std::string, double> get_execution_stats() const = 0;
    
    // Get current rates
    Price get_commission_rate() const { return commission_rate_; }
    Price get_slippage_rate() const { return slippage_rate_; }
};

// Simple execution handler with fixed costs
class SimpleExecutionHandler : public ExecutionHandler {
private:
    Price commission_rate_;
    Price slippage_rate_;
    mutable std::mt19937 rng_;
    mutable std::uniform_real_distribution<double> slippage_dist_;
    
    // Execution statistics
    mutable int total_orders_;
    mutable Price total_commission_;
    mutable Price total_slippage_;
    
    // Calculate commission
    Price calculate_commission(Quantity quantity, Price price) const override;
    
    // Calculate slippage - override base class method
    Price calculate_slippage(Price price, OrderSide side) const override;
    
    // Additional slippage calculation with volume
    Price calculate_slippage_with_volume(OrderSide side, Price price, Volume volume) const;
    
public:
    SimpleExecutionHandler(Price commission_rate = 0.001, Price slippage_rate = 0.001);
    
    Fill execute_order(const Order& order, const OHLC& current_data) override;
    
    void set_commission_rate(Price rate) override { commission_rate_ = rate; }
    void set_slippage_rate(Price rate) override { slippage_rate_ = rate; }
    
    std::unordered_map<std::string, double> get_execution_stats() const override;
    
    // Reset statistics
    void reset_stats();
};

// Realistic execution handler with market impact
class RealisticExecutionHandler : public ExecutionHandler {
private:
    Price commission_rate_;
    Price min_commission_;
    Price max_commission_;
    Price slippage_rate_;
    Price market_impact_factor_;
    mutable std::mt19937 rng_;
    mutable std::normal_distribution<double> slippage_dist_;
    
    // Execution statistics
    mutable int total_orders_;
    mutable Price total_commission_;
    mutable Price total_slippage_;
    mutable Price total_market_impact_;
    
    // Calculate realistic commission
    Price calculate_realistic_commission(Quantity quantity, Price price) const;
    
    // Calculate market impact based on order size and volume
    Price calculate_market_impact(Quantity order_quantity, Volume market_volume, Price price) const;
    
    // Calculate realistic slippage
    Price calculate_realistic_slippage(OrderSide side, Price price, Volume volume, Quantity quantity) const;
    
    // Get execution price based on order type
    Price get_execution_price(const Order& order, const OHLC& current_data) const;
    
    // Validate execution price stays within OHLC range
    Price validate_execution_price(Price execution_price, OrderSide side, const OHLC& current_data) const;
    
public:
    RealisticExecutionHandler(Price commission_rate = 0.001, 
                            Price min_commission = 1.0,
                            Price max_commission = 0.005,
                            Price slippage_rate = 0.001,
                            Price market_impact_factor = 0.001);
    
    Fill execute_order(const Order& order, const OHLC& current_data) override;
    
    void set_commission_rate(Price rate) override { commission_rate_ = rate; }
    void set_slippage_rate(Price rate) override { slippage_rate_ = rate; }
    void set_market_impact_factor(Price factor) { market_impact_factor_ = factor; }
    
    std::unordered_map<std::string, double> get_execution_stats() const override;
    
    // Reset statistics
    void reset_stats();
};

// Order book simulation for more realistic execution
class OrderBookExecutionHandler : public ExecutionHandler {
private:
    struct OrderBookLevel {
        Price price;
        Quantity quantity;
    };
    
    std::vector<OrderBookLevel> bid_levels_;
    std::vector<OrderBookLevel> ask_levels_;
    Price commission_rate_;
    Price tick_size_;
    
    // Execution statistics
    mutable int total_orders_;
    mutable Price total_commission_;
    mutable int partial_fills_;
    
    // Simulate order book based on OHLC data
    void simulate_order_book(const OHLC& current_data);
    
    // Execute against order book
    Fill execute_against_book(const Order& order);
    
    // Calculate realistic spread
    Price calculate_spread(const OHLC& current_data) const;
    
public:
    OrderBookExecutionHandler(Price commission_rate = 0.001, Price tick_size = 0.01);
    
    Fill execute_order(const Order& order, const OHLC& current_data) override;
    
    void set_commission_rate(Price rate) override { commission_rate_ = rate; }
    void set_slippage_rate(Price /* rate */) override { /* Not applicable for order book */ }
    void set_tick_size(Price tick_size) { tick_size_ = tick_size; }
    
    std::unordered_map<std::string, double> get_execution_stats() const override;
    
    // Reset statistics
    void reset_stats();
};

// Execution delay simulation
class DelayedExecutionHandler : public ExecutionHandler {
private:
    std::unique_ptr<ExecutionHandler> base_handler_;
    std::queue<std::pair<Order, OHLC>> delayed_orders_;
    int delay_bars_;
    
public:
    DelayedExecutionHandler(std::unique_ptr<ExecutionHandler> base_handler, int delay_bars = 1);
    
    Fill execute_order(const Order& order, const OHLC& current_data) override;
    
    void set_commission_rate(Price rate) override { base_handler_->set_commission_rate(rate); }
    void set_slippage_rate(Price rate) override { base_handler_->set_slippage_rate(rate); }
    
    std::unordered_map<std::string, double> get_execution_stats() const override;
    
    // Process delayed orders
    std::vector<Fill> process_delayed_orders(const OHLC& current_data);
};

} // namespace backtesting 