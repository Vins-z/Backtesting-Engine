#include "portfolio/portfolio_manager.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <fstream>
#include <iomanip>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace backtesting {

// EnhancedPosition implementation
nlohmann::json EnhancedPosition::to_json() const {
    nlohmann::json j;
    j["symbol"] = symbol;
    j["quantity"] = quantity;
    j["avg_price"] = avg_price;
    j["market_value"] = market_value;
    j["unrealized_pnl"] = unrealized_pnl;
    j["realized_pnl"] = realized_pnl;
    j["stop_loss_price"] = stop_loss_price;
    j["take_profit_price"] = take_profit_price;
    j["risk_per_trade"] = risk_per_trade;
    j["position_size_pct"] = position_size_pct;
    j["entry_time"] = std::chrono::system_clock::to_time_t(entry_time);
    j["last_update_time"] = std::chrono::system_clock::to_time_t(last_update_time);
    j["days_held"] = days_held;
    j["max_favorable_excursion"] = max_favorable_excursion;
    j["max_adverse_excursion"] = max_adverse_excursion;
    j["highest_price"] = highest_price;
    j["lowest_price"] = lowest_price;
    return j;
}

// RiskConfig implementation
nlohmann::json RiskConfig::to_json() const {
    nlohmann::json j;
    j["max_position_size_pct"] = max_position_size_pct;
    j["max_total_exposure_pct"] = max_total_exposure_pct;
    j["min_position_size"] = min_position_size;
    j["max_portfolio_risk_pct"] = max_portfolio_risk_pct;
    j["max_daily_loss_pct"] = max_daily_loss_pct;
    j["max_drawdown_pct"] = max_drawdown_pct;
    j["default_stop_loss_pct"] = default_stop_loss_pct;
    j["default_take_profit_pct"] = default_take_profit_pct;
    j["use_trailing_stops"] = use_trailing_stops;
    j["trailing_stop_pct"] = trailing_stop_pct;
    j["max_positions"] = max_positions;
    j["max_correlation_positions"] = max_correlation_positions;
    j["allow_shorting"] = allow_shorting;
    return j;
}

RiskConfig RiskConfig::from_json(const nlohmann::json& j) {
    RiskConfig config;
    config.max_position_size_pct = j.value("max_position_size_pct", 0.10);
    config.max_total_exposure_pct = j.value("max_total_exposure_pct", 0.80);
    config.min_position_size = j.value("min_position_size", 100.0);
    config.max_portfolio_risk_pct = j.value("max_portfolio_risk_pct", 0.02);
    config.max_daily_loss_pct = j.value("max_daily_loss_pct", 0.05);
    config.max_drawdown_pct = j.value("max_drawdown_pct", 0.20);
    config.default_stop_loss_pct = j.value("default_stop_loss_pct", 0.05);
    config.default_take_profit_pct = j.value("default_take_profit_pct", 0.15);
    config.use_trailing_stops = j.value("use_trailing_stops", true);
    config.trailing_stop_pct = j.value("trailing_stop_pct", 0.03);
    config.max_positions = j.value("max_positions", 10);
    config.max_correlation_positions = j.value("max_correlation_positions", 3);
    config.allow_shorting = j.value("allow_shorting", false);
    return config;
}

// PortfolioStats implementation
PortfolioStats::PortfolioStats() 
    : total_return(0), annualized_return(0), sharpe_ratio(0), max_drawdown(0),
      current_drawdown(0), volatility(0), win_rate(0), profit_factor(0),
      total_trades(0), winning_trades(0), losing_trades(0),
      largest_win(0), largest_loss(0), average_win(0), average_loss(0),
      average_trade_duration(0), value_at_risk_95(0), expected_shortfall(0),
      calmar_ratio(0), sortino_ratio(0), current_exposure(0), max_exposure(0),
      cash_utilization(0) {}

nlohmann::json PortfolioStats::to_json() const {
    nlohmann::json j;
    j["total_return"] = total_return;
    j["annualized_return"] = annualized_return;
    j["sharpe_ratio"] = sharpe_ratio;
    j["max_drawdown"] = max_drawdown;
    j["current_drawdown"] = current_drawdown;
    j["volatility"] = volatility;
    j["win_rate"] = win_rate;
    j["profit_factor"] = profit_factor;
    j["total_trades"] = total_trades;
    j["winning_trades"] = winning_trades;
    j["losing_trades"] = losing_trades;
    j["largest_win"] = largest_win;
    j["largest_loss"] = largest_loss;
    j["average_win"] = average_win;
    j["average_loss"] = average_loss;
    j["average_trade_duration"] = average_trade_duration;
    j["value_at_risk_95"] = value_at_risk_95;
    j["expected_shortfall"] = expected_shortfall;
    j["calmar_ratio"] = calmar_ratio;
    j["sortino_ratio"] = sortino_ratio;
    j["current_exposure"] = current_exposure;
    j["max_exposure"] = max_exposure;
    j["cash_utilization"] = cash_utilization;
    return j;
}

// Enhanced PortfolioManager implementation
PortfolioManager::PortfolioManager(Price initial_capital, const RiskConfig& risk_config)
    : initial_capital_(initial_capital), 
      current_cash_(initial_capital), 
      total_value_(initial_capital),
      peak_value_(initial_capital),
      risk_config_(risk_config),
      order_id_counter_(1),
      daily_start_value_(initial_capital),
      daily_pnl_(0.0) {
    
    // Initialize logger
    try {
        logger_ = spdlog::get("portfolio_manager");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("portfolio_manager");
        }
    } catch (...) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("portfolio_manager", console_sink);
        spdlog::register_logger(logger_);
    }
    
    // Set default position sizer
    position_sizer_ = std::make_unique<PercentEquitySizer>(risk_config_.max_position_size_pct);
    
    logger_->info("Portfolio manager initialized with capital: ${:.2f}", initial_capital);
}

void PortfolioManager::update_market_data(const std::string& symbol, const OHLC& data) {
    // Update price and market data
    Price prev_price = current_prices_.find(symbol) != current_prices_.end() ? current_prices_[symbol] : data.close;
    current_prices_[symbol] = data.close;
    current_market_data_[symbol] = data;
    
    // Update returns history for correlation calculation
    if (symbol_returns_.find(symbol) == symbol_returns_.end()) {
        symbol_returns_[symbol] = std::vector<Price>();
    }
    
    auto& returns = symbol_returns_[symbol];
    if (prev_price > 0.0 && prev_price != data.close) {
        Price current_return = (data.close - prev_price) / prev_price;
        returns.push_back(current_return);
        
        // Keep only last 252 returns (1 year of trading days)
        if (returns.size() > 252) {
            returns.erase(returns.begin());
        }
    }
    
    // Update position metrics if we have a position in this symbol
    auto pos_it = positions_.find(symbol);
    if (pos_it != positions_.end() && pos_it->second.is_open()) {
        update_position_metrics(pos_it->second, data);
    }
}

void PortfolioManager::update_portfolio(const Timestamp& current_time) {
    last_update_time_ = current_time;
    
    // Update total value and position metrics
    total_value_ = current_cash_;
    double total_exposure = 0.0;
    
    for (auto& [symbol, position] : positions_) {
        if (position.is_open() && current_prices_.find(symbol) != current_prices_.end()) {
            Price current_price = current_prices_[symbol];
            position.market_value = std::abs(position.quantity) * current_price;
            position.unrealized_pnl = (current_price - position.avg_price) * position.quantity;
            
            total_value_ += position.market_value;
            total_exposure += position.market_value;
            
            // Update days held
            auto days = std::chrono::duration_cast<std::chrono::hours>(current_time - position.entry_time).count() / 24;
            position.days_held = static_cast<int>(days);
            
            position.last_update_time = current_time;
        }
    }
    
    // Update peak value and drawdown tracking
    if (total_value_ > peak_value_) {
        peak_value_ = total_value_;
    }
    
    // Record equity curve point
    record_equity_point(current_time);
    
    // Update daily P&L
    daily_pnl_ = total_value_ - daily_start_value_;
    
    // Check risk limits
    check_risk_limits();
    
    // Update trailing stops if enabled
    if (risk_config_.use_trailing_stops) {
        update_stop_losses();
    }
    
    logger_->debug("Portfolio updated: Value=${:.2f}, Cash=${:.2f}, Exposure={:.1f}%", 
                  total_value_, current_cash_, (total_exposure / total_value_) * 100.0);
}

void PortfolioManager::start_new_trading_day(const Timestamp& date) {
    daily_start_value_ = total_value_;
    daily_pnl_ = 0.0;
    
    // Record daily P&L from previous day
    if (!daily_pnl_history_.empty()) {
        auto prev_day_pnl = total_value_ - daily_pnl_history_.back().second;
        daily_pnl_history_.emplace_back(date, prev_day_pnl);
    } else {
        daily_pnl_history_.emplace_back(date, 0.0);
    }
    
    logger_->info("Starting new trading day: ${:.2f} portfolio value", total_value_);
}

Order PortfolioManager::generate_order(const SignalEvent& signal) {
    // Get current market data
    auto market_it = current_market_data_.find(signal.symbol);
    if (market_it == current_market_data_.end()) {
        logger_->warn("No market data available for {}", signal.symbol);
        return Order(0, signal.symbol, OrderType::MARKET, OrderSide::BUY, 0, 0, 0);
    }
    
    const OHLC& market_data = market_it->second;
    Price current_price = market_data.close;
    
    if (signal.signal == Signal::BUY) {
        // Check if we already have a position
        auto& position = positions_[signal.symbol];
        if (position.is_long()) {
            logger_->debug("Already long {}, ignoring buy signal", signal.symbol);
            return Order(0, signal.symbol, OrderType::MARKET, OrderSide::BUY, 0, 0, 0);
        }
        
        // Calculate stop loss and position size
        Price stop_loss_price = current_price * (1.0 - risk_config_.default_stop_loss_pct);

        Quantity quantity = calculate_optimal_position_size(
            signal.symbol, current_price, stop_loss_price, risk_config_.max_portfolio_risk_pct
        );

        // Enforce cash-only capital constraint before validating the new position.
        // This ensures we never attempt to open a position that costs more than available cash,
        // even if the risk-based sizing suggests a larger quantity.
        if (quantity > 0) {
            // Use the same rough estimates as validate_new_position / pre_validate_order
            const Price COMMISSION_ESTIMATE = 0.001;
            const Price SLIPPAGE_ESTIMATE = 0.001;
            Price per_share_cost = current_price * (1.0 + COMMISSION_ESTIMATE + SLIPPAGE_ESTIMATE);

            if (per_share_cost <= 0.0) {
                logger_->warn("Invalid per-share cost for {}: price=${:.4f}", signal.symbol, current_price);
                quantity = 0;
            } else {
                Quantity max_affordable_quantity = static_cast<Quantity>(std::floor(current_cash_ / per_share_cost));

                if (max_affordable_quantity <= 0) {
                    // Cannot afford even a single share – skip this trade entirely
                    logger_->info(
                        "Skipping BUY for {} due to insufficient capital: price=${:.2f}, cash=${:.2f}",
                        signal.symbol,
                        current_price,
                        current_cash_
                    );
                    quantity = 0;
                } else if (quantity > max_affordable_quantity) {
                    logger_->debug(
                        "Capping BUY size for {} from {} to {} shares based on available cash ${:.2f}",
                        signal.symbol,
                        quantity,
                        max_affordable_quantity,
                        current_cash_
                    );
                    quantity = max_affordable_quantity;
                }
            }
        }

        // Enforce whole-share quantities (no fractional shares)
        if (quantity > 0) {
            Quantity int_quantity = static_cast<Quantity>(std::floor(quantity));
            if (int_quantity <= 0) {
                quantity = 0;
            } else if (int_quantity != quantity) {
                logger_->debug("Rounding position size for {} from {} to {} shares (no fractional shares allowed)",
                               signal.symbol, quantity, int_quantity);
                quantity = int_quantity;
            }
        }

        if (quantity > 0 && validate_new_position(signal.symbol, quantity, current_price)) {
            logger_->info("Generating BUY order for {}: {} shares @ ${:.2f}", 
                         signal.symbol, quantity, current_price);
            return Order(order_id_counter_++, signal.symbol, OrderType::MARKET, 
                        OrderSide::BUY, quantity, current_price, 0);
        }
        
    } else if (signal.signal == Signal::SELL) {
        // Sell existing position
        auto& position = positions_[signal.symbol];
        if (position.is_long()) {
            Quantity quantity = position.quantity;
            logger_->info("Generating SELL order for {}: {} shares @ ${:.2f}", 
                         signal.symbol, quantity, current_price);
            return Order(order_id_counter_++, signal.symbol, OrderType::MARKET, 
                        OrderSide::SELL, quantity, current_price, 0);
        }
    }
    
    return Order(0, signal.symbol, OrderType::MARKET, OrderSide::BUY, 0, 0, 0);
}

Quantity PortfolioManager::calculate_optimal_position_size(
    const std::string& symbol, 
    Price entry_price, 
    Price stop_loss_price,
    double risk_per_trade
) const {
    
    if (risk_per_trade <= 0.0) {
        risk_per_trade = risk_config_.max_portfolio_risk_pct;
    }
    
    // Use initial_capital when total_value_ is 0 (e.g. before first update_portfolio) so we can size first order
    Price portfolio_value = (total_value_ > 0.0) ? total_value_ : initial_capital_;
    
    // Calculate maximum dollar risk
    Price max_risk_amount = portfolio_value * risk_per_trade;
    
    // Calculate risk per share
    Price risk_per_share = std::abs(entry_price - stop_loss_price);
    
    if (risk_per_share <= 0.0) {
        logger_->warn("Invalid risk per share for {}: entry=${:.2f}, stop=${:.2f}", 
                     symbol, entry_price, stop_loss_price);
        return 0;
    }
    
    // Calculate position size based on risk
    Quantity risk_based_quantity = static_cast<Quantity>(max_risk_amount / risk_per_share);
    
    // Apply maximum position size constraint (use at least 10% if max_position_size_pct is 0)
    double max_pct = (risk_config_.max_position_size_pct > 0.0) ? risk_config_.max_position_size_pct : 0.10;
    Price max_position_value = portfolio_value * max_pct;
    Quantity max_quantity = static_cast<Quantity>(max_position_value / entry_price);
    
    // Use the smaller of the two
    Quantity final_quantity = std::min(risk_based_quantity, max_quantity);
    
    // Check minimum position size
    if (final_quantity * entry_price < risk_config_.min_position_size) {
        logger_->debug("Position size too small for {}: ${:.2f} < ${:.2f}", 
                      symbol, final_quantity * entry_price, risk_config_.min_position_size);
        return 0;
    }
    
    logger_->debug("Calculated position size for {}: {} shares (risk=${:.2f}, max=${:.2f})", 
                  symbol, final_quantity, risk_based_quantity, max_quantity);
    
    return final_quantity;
}

void PortfolioManager::update_fill(const Fill& fill) {
    // Validate fill before processing
    if (fill.quantity <= 0 || fill.price <= 0.0) {
        logger_->warn("Invalid fill rejected: symbol={}, quantity={}, price={}", 
                     fill.symbol, fill.quantity, fill.price);
        return;
    }
    
    // Check if we can afford the trade (for buys) - REJECT if insufficient cash
    if (fill.side == OrderSide::BUY) {
        Price total_cost = fill.quantity * fill.price + fill.commission;
        if (total_cost > current_cash_) {
            logger_->error("Insufficient cash for fill: required=${:.2f}, available=${:.2f} - REJECTING FILL", 
                         total_cost, current_cash_);
            // Reject the fill - do not process it
            return;
        }
    }
    
    // For SELL orders, validate we have the position
    if (fill.side == OrderSide::SELL) {
        auto& position = positions_[fill.symbol];
        if (position.quantity < fill.quantity) {
            logger_->error("Insufficient position for sell fill: requested={}, available={} - REJECTING FILL",
                         fill.quantity, position.quantity);
            return;
        }
    }
    
    // Update lot ledger + realized P&L before mutating position state.
    if (fill.side == OrderSide::BUY) {
        // Allocate commission per share into the lot cost basis.
        Price cost_per_share = fill.price;
        if (fill.quantity > 0) {
            cost_per_share += (fill.commission / fill.quantity);
        }
        open_lots_[fill.symbol].push_back(Lot{fill.quantity, cost_per_share});
    } else if (fill.side == OrderSide::SELL) {
        Quantity remaining = fill.quantity;
        Price proceeds_per_share = fill.price;
        if (fill.quantity > 0) {
            proceeds_per_share -= (fill.commission / fill.quantity);
        }

        auto& lots = open_lots_[fill.symbol];
        while (remaining > 0 && !lots.empty()) {
            Lot& lot = lots.front();
            Quantity matched = std::min(remaining, lot.quantity);
            Price pnl = (proceeds_per_share - lot.cost_per_share) * matched;
            realized_pnl_total_ += pnl;
            realized_pnl_by_symbol_[fill.symbol] += pnl;

            lot.quantity -= matched;
            remaining -= matched;
            if (lot.quantity <= 0) {
                lots.pop_front();
            }
        }

        // If we sold more than we had in lots, something is inconsistent; keep state safe.
        if (remaining > 0) {
            logger_->error("Lot ledger underflow for {}: sold {}, unmatched {}", fill.symbol, fill.quantity, remaining);
        }
    }

    // Update position with fill (cash + avg price + qty)
    update_position(fill);
    
    // Record in trade history
    trade_history_.push_back(fill);
    
    // Log fill for debugging and verification
    logger_->debug("Portfolio updated with fill: {} {} {} @ ${:.2f}, new cash=${:.2f}", 
                  fill.symbol, fill.side == OrderSide::BUY ? "BUY" : "SELL",
                  fill.quantity, fill.price, current_cash_);
}

bool PortfolioManager::pre_validate_order(const Order& order, const OHLC& market_data) const {
    // Comprehensive order validation before execution
    // This catches issues early before execution handler processes the order
    
    if (order.quantity <= 0) {
        logger_->warn("Order validation failed: invalid quantity={}", order.quantity);
        return false;
    }
    
    // Validate market data is available and valid
    if (market_data.close <= 0.0 || market_data.volume < 0) {
        logger_->warn("Order validation failed: invalid market data for {}", order.symbol);
        return false;
    }
    
    // For BUY orders: check cash availability
    if (order.side == OrderSide::BUY) {
        // Use worst-case execution price (high for buys) for validation
        Price execution_price = (order.type == OrderType::MARKET) ? market_data.high : order.price;
        
        // Estimate commission and slippage
        const Price COMMISSION_ESTIMATE = 0.001;
        const Price SLIPPAGE_ESTIMATE = 0.001;
        Price total_cost = order.quantity * execution_price * (1.0 + COMMISSION_ESTIMATE + SLIPPAGE_ESTIMATE);
        
        if (total_cost > current_cash_) {
            logger_->debug("Order validation failed: insufficient cash. Required=${:.2f}, Available=${:.2f}",
                          total_cost, current_cash_);
            return false;
        }
        
        // Check position size limits (use initial_capital if total_value_ is 0 so first orders are not rejected).
        // Use close for position value so we match generate_order/validate_new_position (they use close);
        // using high here would reject orders that already passed when high > close.
        Price position_value = order.quantity * market_data.close;
        Price portfolio_value = (total_value_ > 0.0) ? total_value_ : initial_capital_;
        double max_pct = (risk_config_.max_position_size_pct > 0.0) ? risk_config_.max_position_size_pct : 1.0;
        Price max_position_value = portfolio_value * max_pct;
        if (max_position_value > 0.0 && position_value > max_position_value) {
            logger_->debug("Order validation failed: position size exceeds limit. ${:.2f} > ${:.2f}",
                          position_value, max_position_value);
            return false;
        }
    }
    
    // For SELL orders: check position exists
    if (order.side == OrderSide::SELL) {
        auto it = positions_.find(order.symbol);
        if (it == positions_.end() || it->second.quantity <= 0) {
            logger_->warn("Order validation failed: no position to sell for {}", order.symbol);
            return false;
        }
        
        if (it->second.quantity < order.quantity) {
            logger_->warn("Order validation failed: insufficient position. Requested={}, Available={}",
                         order.quantity, it->second.quantity);
            return false;
        }
    }
    
    // Validate limit order prices are within market range
    if (order.type == OrderType::LIMIT) {
        if (order.side == OrderSide::BUY && order.price < market_data.low) {
            logger_->debug("Order validation failed: limit buy price below market low");
            return false;
        }
        if (order.side == OrderSide::SELL && order.price > market_data.high) {
            logger_->debug("Order validation failed: limit sell price above market high");
            return false;
        }
    }
    
    return true;
}

EnhancedPosition PortfolioManager::get_position(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        return it->second;
    }
    return EnhancedPosition(symbol);
}

std::unordered_map<std::string, EnhancedPosition> PortfolioManager::get_all_positions() const {
    return positions_;
}

std::vector<std::pair<Timestamp, Price>> PortfolioManager::get_equity_curve() const {
    return equity_curve_;
}

std::vector<Fill> PortfolioManager::get_trade_history() const {
    return trade_history_;
}

Quantity PortfolioManager::calculate_position_size([[maybe_unused]] const std::string& symbol, Price price, double allocation) const {
    Price available_cash = current_cash_ * allocation;
    Price total_cost = price * (1 + COMMISSION_RATE);
    return std::floor(available_cash / total_cost);
}

bool PortfolioManager::can_afford_trade([[maybe_unused]] const std::string& symbol, Quantity quantity, Price price) const {
    Price total_cost = quantity * price * (1 + COMMISSION_RATE);
    return total_cost <= current_cash_;
}

Price PortfolioManager::get_unrealized_pnl() const {
    Price unrealized = 0;
    for (const auto& [symbol, position] : positions_) {
        unrealized += position.unrealized_pnl;
    }
    return unrealized;
}

Price PortfolioManager::get_realized_pnl() const {
    return realized_pnl_total_;
}

Price PortfolioManager::get_total_return() const {
    return (total_value_ - initial_capital_) / initial_capital_;
}

void PortfolioManager::update_position(const Fill& fill) {
    EnhancedPosition& position = positions_[fill.symbol];
    
    if (fill.side == OrderSide::BUY) {
        // Update average price including allocated commission per share.
        Price buy_cost_per_share = fill.price;
        if (fill.quantity > 0) {
            buy_cost_per_share += (fill.commission / fill.quantity);
        }
        Price total_cost = position.quantity * position.avg_price + fill.quantity * buy_cost_per_share;
        position.quantity += fill.quantity;
        position.avg_price = total_cost / position.quantity;
        
        // Update cash
        current_cash_ -= fill.quantity * fill.price + fill.commission;
    } else if (fill.side == OrderSide::SELL) {
        position.quantity -= fill.quantity;

        // Update realized P&L (for reporting) from ledger totals.
        position.realized_pnl = realized_pnl_by_symbol_[fill.symbol];
        
        // Update cash
        current_cash_ += fill.quantity * fill.price - fill.commission;
        
        // If position is closed, reset average price
        if (position.quantity <= 0) {
            position.avg_price = 0;
            position.quantity = 0;
        }
    }
}

void PortfolioManager::record_equity_point(const Timestamp& timestamp) {
    // Append an equity-curve point for each bar.
    // This supports consistent performance tracking and drawdown calculations.
    equity_curve_.emplace_back(timestamp, total_value_);
    
    // Update peak value and calculate drawdown
    if (total_value_ > peak_value_) {
        peak_value_ = total_value_;
    }
    
    // Calculate current drawdown
    Price current_drawdown = 0.0;
    if (peak_value_ > 0.0 && total_value_ < peak_value_) {
        current_drawdown = (total_value_ - peak_value_) / peak_value_;
    }
    
    drawdown_curve_.emplace_back(timestamp, current_drawdown);
    
    // Verify equity curve integrity (debug only)
    if (equity_curve_.size() > 1) {
        auto prev_value = equity_curve_[equity_curve_.size() - 2].second;
        if (prev_value <= 0.0 || total_value_ <= 0.0) {
            logger_->warn("Invalid equity curve values detected: prev={:.2f}, current={:.2f}", 
                         prev_value, total_value_);
        }
    }
}

Price PortfolioManager::calculate_commission(Quantity quantity, Price price) const {
    return quantity * price * COMMISSION_RATE;
}

OHLC PortfolioManager::get_current_market_data(const std::string& symbol) const {
    auto it = current_market_data_.find(symbol);
    if (it != current_market_data_.end()) {
        return it->second;
    }
    // Return empty OHLC if not found
    return OHLC{};
}

// PercentEquitySizer implementation
PercentEquitySizer::PercentEquitySizer(double equity_percentage) 
    : equity_percentage_(equity_percentage) {
}

Quantity PercentEquitySizer::calculate_position_size(Price cash, Price price, [[maybe_unused]] Price risk_per_trade) {
    Price position_value = cash * equity_percentage_;
    return static_cast<Quantity>(position_value / price);
}

// Missing PortfolioManager method implementations
void PortfolioManager::check_risk_limits() {
    const double exposure_pct = get_current_exposure();
    const double max_exposure_pct = risk_config_.max_total_exposure_pct * 100.0;
    if (max_exposure_pct > 0.0 && exposure_pct > max_exposure_pct) {
        logger_->warn(
            "Exposure risk limit exceeded: {:.2f}% > {:.2f}%",
            exposure_pct,
            max_exposure_pct
        );
    }

    if (daily_start_value_ > 0.0) {
        const double daily_loss_pct = (-daily_pnl_ / daily_start_value_) * 100.0;
        const double max_daily_loss_pct = risk_config_.max_daily_loss_pct * 100.0;
        if (daily_pnl_ < 0.0 && daily_loss_pct > max_daily_loss_pct) {
            logger_->warn(
                "Daily loss limit exceeded: {:.2f}% > {:.2f}%",
                daily_loss_pct,
                max_daily_loss_pct
            );
        }
    }
}

void PortfolioManager::update_stop_losses() {
    for (auto& [symbol, position] : positions_) {
        if (!position.is_open()) {
            continue;
        }
        const auto price_it = current_prices_.find(symbol);
        if (price_it == current_prices_.end()) {
            continue;
        }

        const Price current_price = price_it->second;
        if (position.quantity > 0.0) {
            position.highest_price = std::max(position.highest_price, current_price);
            if (risk_config_.trailing_stop_pct > 0.0) {
                const Price trailing_stop = position.highest_price * (1.0 - risk_config_.trailing_stop_pct);
                position.stop_loss_price = std::max(position.stop_loss_price, trailing_stop);
            }
        }
    }
}

bool PortfolioManager::validate_new_position(const std::string& symbol, Quantity quantity, Price price) {
    // Comprehensive validation for new positions
    // Check if the position meets all risk and portfolio constraints
    
    if (quantity <= 0 || price <= 0.0) {
        logger_->warn("Invalid position parameters: symbol={}, quantity={}, price={}", symbol, quantity, price);
        return false;
    }
    
    Price position_value = quantity * price;
    // Use initial_capital when total_value_ is 0 so first orders can pass (consistent with pre_validate_order)
    Price portfolio_value = (total_value_ > 0.0) ? total_value_ : initial_capital_;
    double max_pct = (risk_config_.max_position_size_pct > 0.0) ? risk_config_.max_position_size_pct : 1.0;
    Price max_position_value = portfolio_value * max_pct;
    
    // Check position size limit
    if (max_position_value > 0.0 && position_value > max_position_value) {
        logger_->debug("Position exceeds max size: ${:.2f} > ${:.2f}", position_value, max_position_value);
        return false;
    }
    
    // Check cash availability with commission and slippage estimates
    // Estimate commission (0.1% default) and slippage (0.1% default)
    const Price COMMISSION_ESTIMATE = 0.001;
    const Price SLIPPAGE_ESTIMATE = 0.001;
    Price total_cost = position_value * (1.0 + COMMISSION_ESTIMATE + SLIPPAGE_ESTIMATE);
    
    if (total_cost > current_cash_) {
        logger_->debug("Insufficient cash for position: required=${:.2f}, available=${:.2f}", 
                      total_cost, current_cash_);
        return false;
    }
    
    // Check if we can afford the trade (using existing method)
    if (!can_afford_trade(symbol, quantity, price)) {
        return false;
    }
    
    return true;
}

void PortfolioManager::update_position_metrics(EnhancedPosition& position, const OHLC& market_data) {
    // Implementation for updating position metrics
    if (position.quantity != 0) {
        position.market_value = position.quantity * market_data.close;
        position.unrealized_pnl = (market_data.close - position.avg_price) * position.quantity;
        position.last_update_time = market_data.timestamp;
        
        // Update MFE/MAE tracking
        if (position.quantity > 0) { // Long position
            position.max_favorable_excursion = std::max(position.max_favorable_excursion, 
                                                       (market_data.high - position.avg_price) * position.quantity);
            position.max_adverse_excursion = std::min(position.max_adverse_excursion, 
                                                     (market_data.low - position.avg_price) * position.quantity);
        } else { // Short position
            position.max_favorable_excursion = std::max(position.max_favorable_excursion, 
                                                       (position.avg_price - market_data.low) * (-position.quantity));
            position.max_adverse_excursion = std::min(position.max_adverse_excursion, 
                                                     (position.avg_price - market_data.high) * (-position.quantity));
        }
    }
}

// Multi-asset portfolio methods
void PortfolioManager::update_symbol_metadata(const std::string& symbol, const std::string& sector, const std::string& industry) {
    symbol_sectors_[symbol] = sector;
    symbol_industries_[symbol] = industry;
}

double PortfolioManager::calculate_correlation(const std::string& symbol1, const std::string& symbol2) const {
    // Check cache first
    auto it1 = correlation_cache_.find(symbol1);
    if (it1 != correlation_cache_.end()) {
        auto it2 = it1->second.find(symbol2);
        if (it2 != it1->second.end()) {
            return it2->second;
        }
    }
    
    // Calculate correlation from returns
    auto ret1_it = symbol_returns_.find(symbol1);
    auto ret2_it = symbol_returns_.find(symbol2);
    
    if (ret1_it == symbol_returns_.end() || ret2_it == symbol_returns_.end()) {
        return 0.0; // No data available
    }
    
    const auto& returns1 = ret1_it->second;
    const auto& returns2 = ret2_it->second;
    
    if (returns1.size() != returns2.size() || returns1.size() < 2) {
        return 0.0;
    }
    
    // Calculate mean returns
    double mean1 = std::accumulate(returns1.begin(), returns1.end(), 0.0) / returns1.size();
    double mean2 = std::accumulate(returns2.begin(), returns2.end(), 0.0) / returns2.size();
    
    // Calculate covariance and variances
    double covariance = 0.0;
    double var1 = 0.0;
    double var2 = 0.0;
    
    for (size_t i = 0; i < returns1.size(); ++i) {
        double diff1 = returns1[i] - mean1;
        double diff2 = returns2[i] - mean2;
        covariance += diff1 * diff2;
        var1 += diff1 * diff1;
        var2 += diff2 * diff2;
    }
    
    covariance /= (returns1.size() - 1);
    var1 /= (returns1.size() - 1);
    var2 /= (returns2.size() - 1);
    
    // Calculate correlation
    double stddev1 = std::sqrt(var1);
    double stddev2 = std::sqrt(var2);
    
    if (stddev1 == 0.0 || stddev2 == 0.0) {
        return 0.0;
    }
    
    double correlation = covariance / (stddev1 * stddev2);
    
    // Cache the result
    correlation_cache_[symbol1][symbol2] = correlation;
    correlation_cache_[symbol2][symbol1] = correlation;
    
    return correlation;
}

std::vector<std::vector<double>> PortfolioManager::compute_correlation_matrix(const std::vector<std::string>& symbols) const {
    size_t n = symbols.size();
    std::vector<std::vector<double>> corr_matrix(n, std::vector<double>(n, 0.0));
    
    // Initialize diagonal to 1.0 (identity matrix)
    for (size_t i = 0; i < n; ++i) {
        corr_matrix[i][i] = 1.0;
    }
    
    // Fill correlation values
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            double corr = calculate_correlation(symbols[i], symbols[j]);
            corr_matrix[i][j] = corr;
            corr_matrix[j][i] = corr;
        }
    }
    
    return corr_matrix;
}

std::unordered_map<std::string, double> PortfolioManager::get_sector_exposure() const {
    std::unordered_map<std::string, double> exposure;
    double total_value = get_total_value();
    
    if (total_value == 0.0) {
        return exposure;
    }
    
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open() && symbol_sectors_.find(symbol) != symbol_sectors_.end()) {
            const std::string& sector = symbol_sectors_.at(symbol);
            exposure[sector] += position.market_value / total_value;
        }
    }
    
    return exposure;
}

std::unordered_map<std::string, double> PortfolioManager::get_industry_exposure() const {
    std::unordered_map<std::string, double> exposure;
    double total_value = get_total_value();
    
    if (total_value == 0.0) {
        return exposure;
    }
    
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open() && symbol_industries_.find(symbol) != symbol_industries_.end()) {
            const std::string& industry = symbol_industries_.at(symbol);
            exposure[industry] += position.market_value / total_value;
        }
    }
    
    return exposure;
}

double PortfolioManager::calculate_portfolio_variance() const {
    // Get all open positions
    std::vector<std::string> symbols;
    std::vector<double> weights;
    double total_value = get_total_value();
    
    if (total_value == 0.0) {
        return 0.0;
    }
    
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            symbols.push_back(symbol);
            weights.push_back(position.market_value / total_value);
        }
    }
    
    if (symbols.size() < 2) {
        return 0.0;
    }
    
    // Compute correlation matrix
    auto corr_matrix = compute_correlation_matrix(symbols);
    
    // Compute variance (simplified - assumes equal volatility for all assets)
    // In practice, you'd use actual volatility estimates
    double avg_volatility = 0.20; // 20% annualized volatility assumption
    double variance = 0.0;
    
    for (size_t i = 0; i < symbols.size(); ++i) {
        for (size_t j = 0; j < symbols.size(); ++j) {
            variance += weights[i] * weights[j] * corr_matrix[i][j] * avg_volatility * avg_volatility;
        }
    }
    
    return variance;
}

double PortfolioManager::calculate_portfolio_volatility() const {
    return std::sqrt(calculate_portfolio_variance());
}

double PortfolioManager::calculate_diversification_ratio() const {
    // Diversification ratio = weighted average volatility / portfolio volatility
    double total_value = get_total_value();
    if (total_value == 0.0) {
        return 1.0;
    }
    
    double weighted_avg_vol = 0.0;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            double weight = position.market_value / total_value;
            weighted_avg_vol += weight * 0.20; // Assume 20% volatility per asset
        }
    }
    
    double portfolio_vol = calculate_portfolio_volatility();
    if (portfolio_vol == 0.0) {
        return 1.0;
    }
    
    return weighted_avg_vol / portfolio_vol;
}

nlohmann::json PortfolioManager::get_portfolio_analytics() const {
    nlohmann::json analytics;
    
    analytics["total_value"] = get_total_value();
    analytics["cash"] = get_cash();
    analytics["exposure"] = get_current_exposure();
    analytics["num_positions"] = positions_.size();
    analytics["portfolio_volatility"] = calculate_portfolio_volatility();
    analytics["portfolio_variance"] = calculate_portfolio_variance();
    analytics["diversification_ratio"] = calculate_diversification_ratio();
    
    // Sector and industry exposure
    auto sector_exp = get_sector_exposure();
    auto industry_exp = get_industry_exposure();
    
    analytics["sector_exposure"] = sector_exp;
    analytics["industry_exposure"] = industry_exp;
    
    return analytics;
}

nlohmann::json PortfolioManager::get_correlation_matrix_json() const {
    std::vector<std::string> symbols;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            symbols.push_back(symbol);
        }
    }
    
    auto corr_matrix = compute_correlation_matrix(symbols);
    
    nlohmann::json result;
    result["symbols"] = symbols;
    result["matrix"] = nlohmann::json::array();
    
    for (size_t i = 0; i < corr_matrix.size(); ++i) {
        nlohmann::json row = nlohmann::json::array();
        for (size_t j = 0; j < corr_matrix[i].size(); ++j) {
            row.push_back(corr_matrix[i][j]);
        }
        result["matrix"].push_back(row);
    }
    
    return result;
}

nlohmann::json PortfolioManager::get_sector_exposure_json() const {
    auto sector_exp = get_sector_exposure();
    auto industry_exp = get_industry_exposure();
    
    nlohmann::json result;
    result["sectors"] = sector_exp;
    result["industries"] = industry_exp;
    
    return result;
}

Price PortfolioManager::get_current_drawdown() const {
    if (peak_value_ == 0.0 || total_value_ >= peak_value_) {
        return 0.0;
    }
    return (total_value_ - peak_value_) / peak_value_;
}

std::vector<std::pair<Timestamp, Price>> PortfolioManager::get_drawdown_curve() const {
    return drawdown_curve_;
}

Price PortfolioManager::get_max_drawdown() const {
    if (drawdown_curve_.empty()) {
        return 0.0;
    }
    
    Price max_dd = 0.0;
    for (const auto& [timestamp, drawdown] : drawdown_curve_) {
        if (drawdown < max_dd) {
            max_dd = drawdown;
        }
    }
    
    return max_dd;
}

double PortfolioManager::get_current_exposure() const {
    double total_exposure = 0.0;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            total_exposure += position.market_value;
        }
    }
    
    double total_value = get_total_value();
    if (total_value == 0.0) {
        return 0.0;
    }
    
    return (total_exposure / total_value) * 100.0; // Return as percentage
}

PortfolioStats PortfolioManager::calculate_portfolio_stats() const {
    PortfolioStats stats;
    
    // Basic return metrics
    stats.total_return = get_total_return();
    
    // Calculate annualized return (simplified - assumes 252 trading days per year)
    if (!equity_curve_.empty()) {
        auto days = std::chrono::duration_cast<std::chrono::hours>(
            equity_curve_.back().first - equity_curve_.front().first
        ).count() / 24.0;
        
        if (days > 0) {
            double years = days / 365.25;
            if (years > 0) {
                stats.annualized_return = (std::pow(1.0 + stats.total_return, 1.0 / years) - 1.0);
            }
        }
    }
    
    // Drawdown metrics
    stats.max_drawdown = get_max_drawdown();
    stats.current_drawdown = get_current_drawdown();
    
    // Calculate volatility from equity curve
    if (equity_curve_.size() > 1) {
        std::vector<double> returns;
        for (size_t i = 1; i < equity_curve_.size(); ++i) {
            double ret = (equity_curve_[i].second - equity_curve_[i-1].second) / equity_curve_[i-1].second;
            returns.push_back(ret);
        }
        
        if (!returns.empty()) {
            double mean = 0.0;
            for (double r : returns) {
                mean += r;
            }
            mean /= returns.size();
            
            double variance = 0.0;
            for (double r : returns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= returns.size();
            
            // Annualized volatility (assuming daily returns)
            stats.volatility = std::sqrt(variance * 252.0);
        }
    }
    
    // Trade statistics
    stats.total_trades = trade_history_.size();
    stats.winning_trades = 0;
    stats.losing_trades = 0;
    Price total_wins = 0.0;
    Price total_losses = 0.0;
    stats.largest_win = 0.0;
    stats.largest_loss = 0.0;
    
    // Calculate realized P&L per trade
    std::unordered_map<std::string, std::vector<Fill>> symbol_trades;
    for (const auto& fill : trade_history_) {
        symbol_trades[fill.symbol].push_back(fill);
    }
    
    for (const auto& [symbol, fills] : symbol_trades) {
        Price total_cost = 0.0;
        Quantity total_qty = 0.0;
        
        for (const auto& fill : fills) {
            if (fill.side == OrderSide::BUY) {
                total_cost += fill.price * fill.quantity + fill.commission + fill.slippage;
                total_qty += fill.quantity;
            } else if (fill.side == OrderSide::SELL) {
                if (total_qty > 0) {
                    Price avg_cost = total_cost / total_qty;
                    Price trade_pnl = (fill.price - avg_cost) * fill.quantity - fill.commission - fill.slippage;
                    
                    if (trade_pnl > 0) {
                        stats.winning_trades++;
                        total_wins += trade_pnl;
                        if (trade_pnl > stats.largest_win) {
                            stats.largest_win = trade_pnl;
                        }
                    } else if (trade_pnl < 0) {
                        stats.losing_trades++;
                        total_losses += std::abs(trade_pnl);
                        if (trade_pnl < stats.largest_loss) {
                            stats.largest_loss = trade_pnl;
                        }
                    }
                    
                    total_cost -= avg_cost * fill.quantity;
                    total_qty -= fill.quantity;
                }
            }
        }
    }
    
    // Win rate
    if (stats.total_trades > 0) {
        stats.win_rate = (static_cast<double>(stats.winning_trades) / stats.total_trades);
    }
    
    // Average win/loss
    if (stats.winning_trades > 0) {
        stats.average_win = total_wins / stats.winning_trades;
    }
    if (stats.losing_trades > 0) {
        stats.average_loss = total_losses / stats.losing_trades;
    }
    
    // Profit factor
    if (total_losses > 0) {
        stats.profit_factor = total_wins / total_losses;
    }
    
    // Sharpe ratio (simplified)
    if (stats.volatility > 0) {
        // Risk-free rate assumed to be 0 for simplicity
        stats.sharpe_ratio = stats.annualized_return / stats.volatility;
    }
    
    // Calmar ratio
    if (stats.max_drawdown < 0 && std::abs(stats.max_drawdown) > 0.0001) {
        stats.calmar_ratio = stats.annualized_return / std::abs(stats.max_drawdown);
    }
    
    // Sortino ratio (simplified - uses volatility instead of downside deviation)
    if (stats.volatility > 0) {
        stats.sortino_ratio = stats.annualized_return / stats.volatility;
    }
    
    // Exposure metrics
    stats.current_exposure = get_current_exposure();
    stats.cash_utilization = (1.0 - (get_cash() / get_total_value()));
    
    // Value at Risk and Expected Shortfall (simplified calculations)
    if (equity_curve_.size() > 20) {
        std::vector<double> returns;
        for (size_t i = 1; i < equity_curve_.size(); ++i) {
            double ret = (equity_curve_[i].second - equity_curve_[i-1].second) / equity_curve_[i-1].second;
            returns.push_back(ret);
        }
        
        if (!returns.empty()) {
            std::sort(returns.begin(), returns.end());
            size_t var_index = static_cast<size_t>(returns.size() * 0.05); // 5% tail
            if (var_index < returns.size()) {
                stats.value_at_risk_95 = returns[var_index] * get_total_value();
                
                // Expected shortfall (average of worst 5%)
                double es_sum = 0.0;
                for (size_t i = 0; i <= var_index && i < returns.size(); ++i) {
                    es_sum += returns[i];
                }
                if (var_index > 0) {
                    stats.expected_shortfall = (es_sum / (var_index + 1)) * get_total_value();
                }
            }
        }
    }
    
    return stats;
}

bool PortfolioManager::is_risk_limit_exceeded() const {
    // Check daily loss limit
    if (daily_pnl_ < 0 && std::abs(daily_pnl_) > (daily_start_value_ * risk_config_.max_daily_loss_pct)) {
        return true;
    }
    
    // Check drawdown limit
    Price current_dd = get_current_drawdown();
    if (std::abs(current_dd) > risk_config_.max_drawdown_pct) {
        return true;
    }
    
    // Check position count limit
    int open_positions = 0;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            open_positions++;
        }
    }
    if (open_positions > risk_config_.max_positions) {
        return true;
    }
    
    // Check exposure limit
    double exposure = get_current_exposure();
    if (exposure > (risk_config_.max_total_exposure_pct * 100.0)) {
        return true;
    }
    
    return false;
}

std::vector<std::string> PortfolioManager::get_risk_warnings() const {
    std::vector<std::string> warnings;
    
    // Check daily loss
    if (daily_pnl_ < 0 && std::abs(daily_pnl_) > (daily_start_value_ * risk_config_.max_daily_loss_pct * 0.8)) {
        warnings.push_back("Daily loss approaching limit");
    }
    
    // Check drawdown
    Price current_dd = get_current_drawdown();
    if (std::abs(current_dd) > risk_config_.max_drawdown_pct * 0.8) {
        warnings.push_back("Drawdown approaching maximum limit");
    }
    
    // Check position count
    int open_positions = 0;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            open_positions++;
        }
    }
    if (open_positions > risk_config_.max_positions * 0.8) {
        warnings.push_back("Position count approaching limit");
    }
    
    // Check exposure
    double exposure = get_current_exposure();
    if (exposure > (risk_config_.max_total_exposure_pct * 0.8 * 100.0)) {
        warnings.push_back("Portfolio exposure approaching limit");
    }
    
    // Check cash
    double cash_pct = (get_cash() / get_total_value()) * 100.0;
    if (cash_pct < 10.0) {
        warnings.push_back("Low cash reserves");
    }
    
    return warnings;
}

nlohmann::json PortfolioManager::get_risk_report() const {
    nlohmann::json report;
    
    report["risk_limit_exceeded"] = is_risk_limit_exceeded();
    report["warnings"] = get_risk_warnings();
    
    // Risk metrics
    nlohmann::json metrics;
    metrics["current_drawdown"] = get_current_drawdown();
    metrics["max_drawdown"] = get_max_drawdown();
    metrics["current_exposure"] = get_current_exposure();
    metrics["cash_percentage"] = (get_cash() / get_total_value()) * 100.0;
    metrics["daily_pnl"] = daily_pnl_;
    metrics["daily_pnl_percent"] = (daily_pnl_ / daily_start_value_) * 100.0;
    
    // Position count
    int open_positions = 0;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            open_positions++;
        }
    }
    metrics["open_positions"] = open_positions;
    metrics["max_positions"] = risk_config_.max_positions;
    
    // Risk limits
    nlohmann::json limits;
    limits["max_daily_loss_pct"] = risk_config_.max_daily_loss_pct;
    limits["max_drawdown_pct"] = risk_config_.max_drawdown_pct;
    limits["max_total_exposure_pct"] = risk_config_.max_total_exposure_pct;
    limits["max_position_size_pct"] = risk_config_.max_position_size_pct;
    
    report["metrics"] = metrics;
    report["limits"] = limits;
    
    // Portfolio stats for risk context
    auto stats = calculate_portfolio_stats();
    report["portfolio_stats"] = stats.to_json();
    
    return report;
}

nlohmann::json PortfolioManager::get_portfolio_summary() const {
    nlohmann::json summary;
    
    summary["total_value"] = get_total_value();
    summary["initial_capital"] = initial_capital_;
    summary["cash"] = get_cash();
    summary["total_return"] = get_total_return();
    summary["total_return_pct"] = get_total_return() * 100.0;
    summary["unrealized_pnl"] = get_unrealized_pnl();
    summary["realized_pnl"] = get_realized_pnl();
    summary["current_drawdown"] = get_current_drawdown();
    summary["max_drawdown"] = get_max_drawdown();
    summary["current_exposure"] = get_current_exposure();
    
    // Position count
    int open_positions = 0;
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            open_positions++;
        }
    }
    summary["num_positions"] = open_positions;
    summary["total_trades"] = trade_history_.size();
    
    // Portfolio stats
    auto stats = calculate_portfolio_stats();
    summary["stats"] = stats.to_json();
    
    return summary;
}

nlohmann::json PortfolioManager::get_position_summary() const {
    nlohmann::json summary;
    summary["positions"] = nlohmann::json::array();
    
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            nlohmann::json pos_json = position.to_json();
            pos_json["current_price"] = current_prices_.find(symbol) != current_prices_.end() 
                ? current_prices_.at(symbol) 
                : position.avg_price;
            summary["positions"].push_back(pos_json);
        }
    }
    
    summary["total_positions"] = summary["positions"].size();
    summary["total_market_value"] = get_total_value() - get_cash();
    summary["total_unrealized_pnl"] = get_unrealized_pnl();
    
    return summary;
}

void PortfolioManager::save_portfolio_state(const std::string& filename) const {
    nlohmann::json state;
    state["initial_capital"] = initial_capital_;
    state["current_cash"] = current_cash_;
    state["total_value"] = total_value_;
    state["peak_value"] = peak_value_;
    state["positions"] = nlohmann::json::array();
    
    for (const auto& [symbol, position] : positions_) {
        if (position.is_open()) {
            state["positions"].push_back(position.to_json());
        }
    }
    
    state["risk_config"] = risk_config_.to_json();
    
    std::ofstream file(filename);
    if (file.is_open()) {
        file << state.dump(2);
        file.close();
    }
}

bool PortfolioManager::load_portfolio_state(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json state = nlohmann::json::parse(file);
        
        initial_capital_ = state.value("initial_capital", initial_capital_);
        current_cash_ = state.value("current_cash", current_cash_);
        total_value_ = state.value("total_value", total_value_);
        peak_value_ = state.value("peak_value", peak_value_);
        
        if (state.contains("risk_config")) {
            risk_config_ = RiskConfig::from_json(state["risk_config"]);
        }
        
        // Note: Positions would need to be reconstructed from saved data
        // This is a simplified version
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

} // namespace backtesting 