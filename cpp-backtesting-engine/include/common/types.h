#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <memory>

namespace backtesting {

// Basic types
using Price = double;
using Volume = long long;
using Quantity = double;
using Timestamp = std::chrono::system_clock::time_point;

// OHLC data structure
struct OHLC {
    Timestamp timestamp;
    std::string symbol;
    Price open;
    Price high;
    Price low;
    Price close;
    Volume volume;
    
    OHLC() = default;
    OHLC(Timestamp ts, Price o, Price h, Price l, Price c, Volume v, const std::string& sym = "")
        : timestamp(ts), symbol(sym), open(o), high(h), low(l), close(c), volume(v) {}
};

// Signal types
enum class Signal {
    NONE = 0,
    BUY = 1,
    SELL = -1
};

// Order types
enum class OrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT
};

// Order side
enum class OrderSide {
    BUY,
    SELL
};

// Order structure
struct Order {
    int id;
    std::string symbol;
    OrderType type;
    OrderSide side;
    Quantity quantity;
    Price price;  // For limit/stop orders
    Price stop_price;  // For stop orders
    Timestamp timestamp;
    
    Order(int order_id, const std::string& sym, OrderType t, OrderSide s, 
          Quantity qty, Price p = 0.0, Price sp = 0.0)
        : id(order_id), symbol(sym), type(t), side(s), quantity(qty), 
          price(p), stop_price(sp), timestamp(std::chrono::system_clock::now()) {}
};

// Fill structure with optional decision context (regime, volatility at entry)
struct Fill {
    int order_id;
    std::string symbol;
    OrderSide side;
    Quantity quantity;
    Price price;
    Price commission;
    Price slippage;
    Timestamp timestamp;

    // Decision context at entry (for "why it worked/failed" analysis)
    std::string regime;           // e.g. "Bull_Quiet", "Bear_Volatile"
    double volatility_pct = 0.0;  // ATR as % of price at entry
    double atr_at_entry = 0.0;    // Raw ATR value at entry
    std::string filter_reason;    // If trade was allowed despite filter, or "vol_filter" if suppressed
    
    Fill(int oid, const std::string& sym, OrderSide s, Quantity qty, 
         Price p, Price comm = 0.0, Price slip = 0.0)
        : order_id(oid), symbol(sym), side(s), quantity(qty), price(p),
          commission(comm), slippage(slip), timestamp(std::chrono::system_clock::now()) {}
};

// Position structure
struct Position {
    std::string symbol;
    Quantity quantity;
    Price avg_price;
    Price market_value;
    Price unrealized_pnl;
    
    Position() : symbol(""), quantity(0), avg_price(0), market_value(0), unrealized_pnl(0) {}
    Position(const std::string& sym) 
        : symbol(sym), quantity(0), avg_price(0), market_value(0), unrealized_pnl(0) {}
};

// Performance metrics
struct PerformanceMetrics {
    Price total_return;
    Price annualized_return;
    Price sharpe_ratio;
    Price max_drawdown;
    Price volatility;
    Price win_rate;
    Price profit_factor;
    int total_trades;
    int winning_trades;
    int losing_trades;
    
    PerformanceMetrics() = default;
};

// Event types for event-driven architecture
enum class EventType {
    MARKET,
    SIGNAL,
    ORDER,
    FILL
};

// Base event class
class Event {
public:
    EventType type;
    Timestamp timestamp;
    
    Event(EventType t) : type(t), timestamp(std::chrono::system_clock::now()) {}
    virtual ~Event() = default;
};

// Market event
class MarketEvent : public Event {
public:
    std::string symbol;
    OHLC data;
    
    MarketEvent(const std::string& sym, const OHLC& ohlc)
        : Event(EventType::MARKET), symbol(sym), data(ohlc) {}
};

// Signal event
class SignalEvent : public Event {
public:
    std::string symbol;
    Signal signal;
    Price strength;
    
    SignalEvent(const std::string& sym, Signal sig, Price str = 1.0)
        : Event(EventType::SIGNAL), symbol(sym), signal(sig), strength(str) {}
};

// Order event
class OrderEvent : public Event {
public:
    Order order;
    
    OrderEvent(const Order& ord) : Event(EventType::ORDER), order(ord) {}
};

// Fill event
class FillEvent : public Event {
public:
    Fill fill;
    
    FillEvent(const Fill& f) : Event(EventType::FILL), fill(f) {}
};

} // namespace backtesting 