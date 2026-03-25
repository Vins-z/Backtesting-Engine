#include "execution/execution_handler.h"
#include <random>
#include <algorithm>

namespace backtesting {

ExecutionHandler::ExecutionHandler(Price commission_rate, Price slippage_rate)
    : commission_rate_(commission_rate), slippage_rate_(slippage_rate) {}

Price ExecutionHandler::calculate_commission(Quantity quantity, Price price) const {
    return quantity * price * commission_rate_;
}

Price ExecutionHandler::calculate_slippage(Price price, OrderSide side) const {
    // Simple slippage model - random slippage up to slippage_rate
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    
    Price slippage = price * slippage_rate_ * dis(gen);
    
    // Buy orders have positive slippage (worse price)
    // Sell orders have negative slippage (worse price)
    return (side == OrderSide::BUY) ? slippage : -slippage;
}

// SimpleExecutionHandler Implementation
SimpleExecutionHandler::SimpleExecutionHandler(Price commission_rate, Price slippage_rate)
    : ExecutionHandler(commission_rate, slippage_rate), 
      commission_rate_(commission_rate), slippage_rate_(slippage_rate),
      rng_(std::random_device{}()), slippage_dist_(-slippage_rate, slippage_rate),
      total_orders_(0), total_commission_(0.0), total_slippage_(0.0) {}

Fill SimpleExecutionHandler::execute_order(const Order& order, const OHLC& current_data) {
    if (order.quantity <= 0) {
        return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
    }
    
    // Map order type + side to a deterministic execution price for this backtest model.
    // Market orders execute at the worst OHLC price for the side.
    Price market_price;
    if (order.type == OrderType::MARKET) {
        market_price = (order.side == OrderSide::BUY) ? current_data.high : current_data.low;
    } else {
        // For limit orders, use the order price if within range, otherwise use market price
        market_price = current_data.close;
        if (order.type == OrderType::LIMIT) {
            if (order.side == OrderSide::BUY && order.price < current_data.low) {
                // Limit buy below low - order cannot execute
                return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
            }
            if (order.side == OrderSide::SELL && order.price > current_data.high) {
                // Limit sell above high - order cannot execute
                return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
            }
            // Limit order can execute - use order price
            market_price = order.price;
        }
    }
    
    // Calculate slippage
    Price slippage = calculate_slippage_with_volume(order.side, market_price, current_data.volume);
    Price execution_price = market_price + slippage;
    
    // Clamp execution price to the bar's OHLC range.
    // Clamp to [low, high] range
    execution_price = std::max(current_data.low, std::min(execution_price, current_data.high));
    
    // Ensure execution price is within valid range
    if (execution_price <= 0.0) {
        return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
    }
    
    // Calculate commission
    Price commission = calculate_commission(order.quantity, execution_price);
    
    // Update statistics
    total_orders_++;
    total_commission_ += commission;
    total_slippage_ += std::abs(slippage);
    
    // Create fill
    Fill fill(order.id, order.symbol, order.side, order.quantity, 
              execution_price, commission, slippage);
    
    return fill;
}

Price SimpleExecutionHandler::calculate_commission(Quantity quantity, Price price) const {
    return quantity * price * commission_rate_;
}

Price SimpleExecutionHandler::calculate_slippage(Price price, OrderSide side) const {
    // Simple random slippage
    Price base_slippage = price * slippage_dist_(rng_);
    
    // Buy orders have positive slippage (worse price)
    // Sell orders have negative slippage (worse price)  
    return (side == OrderSide::BUY) ? std::abs(base_slippage) : -std::abs(base_slippage);
}

Price SimpleExecutionHandler::calculate_slippage_with_volume(OrderSide side, Price price, Volume volume) const {
    // Calculate slippage considering volume
    Price base_slippage = calculate_slippage(price, side);
    
    // Adjust based on volume - lower volume means higher slippage
    if (volume > 0) {
        double volume_factor = 1.0 + (1000.0 / volume); // Simple volume impact
        base_slippage *= volume_factor;
    }
    
    return base_slippage;
}

std::unordered_map<std::string, double> SimpleExecutionHandler::get_execution_stats() const {
    return {
        {"total_orders", static_cast<double>(total_orders_)},
        {"total_commission", total_commission_},
        {"total_slippage", total_slippage_},
        {"avg_commission", total_orders_ > 0 ? total_commission_ / total_orders_ : 0.0},
        {"avg_slippage", total_orders_ > 0 ? total_slippage_ / total_orders_ : 0.0}
    };
}

void SimpleExecutionHandler::reset_stats() {
    total_orders_ = 0;
    total_commission_ = 0.0;
    total_slippage_ = 0.0;
}

// RealisticExecutionHandler Implementation
RealisticExecutionHandler::RealisticExecutionHandler(Price commission_rate, Price min_commission, 
                                                   Price max_commission, Price slippage_rate, 
                                                   Price market_impact_factor)
    : ExecutionHandler(commission_rate, slippage_rate),
      commission_rate_(commission_rate), min_commission_(min_commission), 
      max_commission_(max_commission), slippage_rate_(slippage_rate),
      market_impact_factor_(market_impact_factor), rng_(std::random_device{}()),
      slippage_dist_(0.0, slippage_rate), total_orders_(0), 
      total_commission_(0.0), total_slippage_(0.0), total_market_impact_(0.0) {}

Fill RealisticExecutionHandler::execute_order(const Order& order, const OHLC& current_data) {
    if (order.quantity <= 0) {
        return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
    }
    
    Price execution_price = get_execution_price(order, current_data);
    
    // Simplified liquidity constraint for partial fills.
    // Assume we can fill up to a percentage of the bar's volume (e.g., 20% for large orders).
    Quantity fillable_quantity = order.quantity;
    const double MAX_VOLUME_PARTICIPATION = 0.20; // Max 20% of daily volume per order
    
    if (current_data.volume > 0) {
        Quantity max_fillable = static_cast<Quantity>(current_data.volume * MAX_VOLUME_PARTICIPATION);
        if (order.quantity > max_fillable) {
            // Order exceeds available liquidity - partial fill
            fillable_quantity = max_fillable;
            // The remaining quantity is dropped in this simplified partial-fill model.
        }
    }
    
    // Ensure we have a valid fill quantity
    if (fillable_quantity <= 0) {
        return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
    }
    
    // Calculate market impact based on actual fill quantity
    Price market_impact = calculate_market_impact(fillable_quantity, current_data.volume, execution_price);
    
    // Calculate realistic slippage based on fill quantity
    Price slippage = calculate_realistic_slippage(order.side, execution_price, current_data.volume, fillable_quantity);
    
    // Apply market impact and slippage to execution price
    execution_price += market_impact + slippage;
    
    // Clamp/validate execution price against the bar's OHLC range.
    execution_price = validate_execution_price(execution_price, order.side, current_data);
    
    // Ensure execution price is valid
    if (execution_price <= 0.0) {
        return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
    }
    
    // Calculate commission for actual fill quantity
    Price commission = calculate_realistic_commission(fillable_quantity, execution_price);
    
    // Update statistics
    total_orders_++;
    total_commission_ += commission;
    total_slippage_ += std::abs(slippage);
    total_market_impact_ += std::abs(market_impact);
    
    Fill fill(order.id, order.symbol, order.side, fillable_quantity, 
              execution_price, commission, slippage + market_impact);
    
    return fill;
}

Price RealisticExecutionHandler::calculate_realistic_commission(Quantity quantity, Price price) const {
    Price commission = quantity * price * commission_rate_;
    return std::max(min_commission_, std::min(max_commission_ * quantity * price, commission));
}

Price RealisticExecutionHandler::calculate_market_impact(Quantity order_quantity, Volume market_volume, Price price) const {
    if (market_volume <= 0) return 0.0;
    
    double impact_ratio = static_cast<double>(order_quantity) / market_volume;
    return price * market_impact_factor_ * std::sqrt(impact_ratio);
}

Price RealisticExecutionHandler::calculate_realistic_slippage(OrderSide side, Price price, Volume volume, Quantity quantity) const {
    Price base_slippage = price * slippage_dist_(rng_);
    
    // Increase slippage for larger orders relative to volume
    if (volume > 0) {
        double size_factor = 1.0 + static_cast<double>(quantity) / volume;
        base_slippage *= size_factor;
    }
    
    return (side == OrderSide::BUY) ? std::abs(base_slippage) : -std::abs(base_slippage);
}

Price RealisticExecutionHandler::get_execution_price(const Order& order, const OHLC& current_data) const {
    // Use different prices based on order type
    Price base_price;
    switch (order.type) {
        case OrderType::MARKET:
            base_price = (order.side == OrderSide::BUY) ? current_data.high : current_data.low;
            break;
        case OrderType::LIMIT:
            base_price = order.price;
            // Validate limit price is within market range
            if (order.side == OrderSide::BUY && base_price < current_data.low) {
                base_price = current_data.low; // Clamp to low if below range
            } else if (order.side == OrderSide::SELL && base_price > current_data.high) {
                base_price = current_data.high; // Clamp to high if above range
            }
            break;
        default:
            base_price = current_data.close;
            break;
    }
    return base_price;
}

Price RealisticExecutionHandler::validate_execution_price(Price execution_price, OrderSide side, const OHLC& current_data) const {
    // Clamp execution price to the bar's OHLC range.
    if (side == OrderSide::BUY) {
        // Buy orders: clamp to [low, high]
        execution_price = std::max(current_data.low, std::min(execution_price, current_data.high));
    } else {
        // Sell orders: clamp to [low, high]
        execution_price = std::max(current_data.low, std::min(execution_price, current_data.high));
    }
    
    return execution_price;
}

std::unordered_map<std::string, double> RealisticExecutionHandler::get_execution_stats() const {
    return {
        {"total_orders", static_cast<double>(total_orders_)},
        {"total_commission", total_commission_},
        {"total_slippage", total_slippage_},
        {"total_market_impact", total_market_impact_},
        {"avg_commission", total_orders_ > 0 ? total_commission_ / total_orders_ : 0.0},
        {"avg_slippage", total_orders_ > 0 ? total_slippage_ / total_orders_ : 0.0},
        {"avg_market_impact", total_orders_ > 0 ? total_market_impact_ / total_orders_ : 0.0}
    };
}

void RealisticExecutionHandler::reset_stats() {
    total_orders_ = 0;
    total_commission_ = 0.0;
    total_slippage_ = 0.0;
    total_market_impact_ = 0.0;
}

// OrderBookExecutionHandler Implementation
OrderBookExecutionHandler::OrderBookExecutionHandler(Price commission_rate, Price tick_size)
    : ExecutionHandler(commission_rate, 0.0), commission_rate_(commission_rate), 
      tick_size_(tick_size), total_orders_(0), total_commission_(0.0), partial_fills_(0) {}

Fill OrderBookExecutionHandler::execute_order(const Order& order, const OHLC& current_data) {
    if (order.quantity <= 0) {
        return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
    }
    
    simulate_order_book(current_data);
    Fill fill = execute_against_book(order);
    
    total_orders_++;
    total_commission_ += fill.commission;
    if (fill.quantity < order.quantity) {
        partial_fills_++;
    }
    
    return fill;
}

void OrderBookExecutionHandler::simulate_order_book(const OHLC& current_data) {
    // Simple order book simulation
    Price spread = calculate_spread(current_data);
    Price mid_price = (current_data.high + current_data.low) / 2.0;
    
    bid_levels_.clear();
    ask_levels_.clear();
    
    // Create 5 levels on each side
    for (int i = 0; i < 5; ++i) {
        Price bid_price = mid_price - spread/2.0 - i * tick_size_;
        Price ask_price = mid_price + spread/2.0 + i * tick_size_;
        
        Quantity bid_qty = static_cast<Quantity>(1000 * (5 - i));
        Quantity ask_qty = static_cast<Quantity>(1000 * (5 - i));
        
        bid_levels_.push_back({bid_price, bid_qty});
        ask_levels_.push_back({ask_price, ask_qty});
    }
}

Fill OrderBookExecutionHandler::execute_against_book(const Order& order) {
    Quantity remaining_qty = order.quantity;
    Price weighted_price = 0.0;
    Quantity total_filled = 0;
    
    auto& levels = (order.side == OrderSide::BUY) ? ask_levels_ : bid_levels_;
    
    for (auto& level : levels) {
        if (remaining_qty <= 0) break;
        
        Quantity fill_qty = std::min(remaining_qty, level.quantity);
        weighted_price += level.price * fill_qty;
        total_filled += fill_qty;
        remaining_qty -= fill_qty;
        level.quantity -= fill_qty;
    }
    
    Price avg_price = (total_filled > 0) ? weighted_price / total_filled : order.price;
    Price commission = total_filled * avg_price * commission_rate_;
    
    return Fill(order.id, order.symbol, order.side, total_filled, avg_price, commission, 0.0);
}

Price OrderBookExecutionHandler::calculate_spread(const OHLC& current_data) const {
    // Simple spread model based on volatility
    Price volatility = (current_data.high - current_data.low) / current_data.close;
    return current_data.close * std::max(0.001, volatility * 0.1);
}

std::unordered_map<std::string, double> OrderBookExecutionHandler::get_execution_stats() const {
    return {
        {"total_orders", static_cast<double>(total_orders_)},
        {"total_commission", total_commission_},
        {"partial_fills", static_cast<double>(partial_fills_)},
        {"partial_fill_rate", total_orders_ > 0 ? static_cast<double>(partial_fills_) / total_orders_ : 0.0},
        {"avg_commission", total_orders_ > 0 ? total_commission_ / total_orders_ : 0.0}
    };
}

void OrderBookExecutionHandler::reset_stats() {
    total_orders_ = 0;
    total_commission_ = 0.0;
    partial_fills_ = 0;
}

// DelayedExecutionHandler Implementation
DelayedExecutionHandler::DelayedExecutionHandler(std::unique_ptr<ExecutionHandler> base_handler, int delay_bars)
    : ExecutionHandler(0.0, 0.0), base_handler_(std::move(base_handler)), delay_bars_(delay_bars) {}

Fill DelayedExecutionHandler::execute_order(const Order& order, const OHLC& current_data) {
    // Add order to delay queue
    delayed_orders_.push({order, current_data});
    
    // Delayed execution: order is filled only after the delay window elapses.
    return Fill(order.id, order.symbol, order.side, 0, 0, 0, 0);
}

std::vector<Fill> DelayedExecutionHandler::process_delayed_orders(const OHLC& current_data) {
    std::vector<Fill> fills;
    
    // Process orders that have been delayed long enough
    int orders_to_process = std::max(0, static_cast<int>(delayed_orders_.size()) - delay_bars_);
    
    for (int i = 0; i < orders_to_process; ++i) {
        auto [order, old_data] = delayed_orders_.front();
        delayed_orders_.pop();
        
        Fill fill = base_handler_->execute_order(order, current_data);
        fills.push_back(fill);
    }
    
    return fills;
}

std::unordered_map<std::string, double> DelayedExecutionHandler::get_execution_stats() const {
    auto stats = base_handler_->get_execution_stats();
    stats["delayed_orders"] = static_cast<double>(delayed_orders_.size());
    return stats;
}

} // namespace backtesting 