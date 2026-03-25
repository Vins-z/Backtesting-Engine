#pragma once

#include "common/types.h"
#include "data/data_handler.h"
#include "portfolio/portfolio_manager.h"
#include "execution/execution_handler.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace backtesting {

// Replay configuration
struct ReplayConfig {
    std::string symbol;
    std::string start_date;
    std::string end_date;
    Price initial_capital;
    Price commission_rate;
    Price slippage_rate;
    std::string data_source;
    std::string data_path;
    std::string data_interval;
    double speed_multiplier; // 1.0 = real-time, 2.0 = 2x speed, etc.
    
    // Convert to JSON
    nlohmann::json to_json() const;
    
    // Validate configuration
    bool validate() const;
};

// Replay state
struct ReplayState {
    std::string current_date;
    size_t current_bar_index;
    size_t total_bars;
    bool is_playing;
    bool is_paused;
    double progress_percent;
    
    // Convert to JSON
    nlohmann::json to_json() const;
};

// Replay event callback type
using ReplayEventCallback = std::function<void(const nlohmann::json&)>;

// Historical Replay Engine - allows replaying historical data day-by-day
class ReplayEngine {
public:
    ReplayEngine();
    ~ReplayEngine() = default;
    
    // Configuration
    bool configure(const ReplayConfig& config);
    
    // Control
    bool start();
    void pause();
    void resume();
    void stop();
    void seek_to_date(const std::string& date);
    void set_speed(double multiplier);
    
    // State
    ReplayState get_state() const;
    bool is_running() const { return is_running_; }
    bool is_paused() const { return is_paused_; }
    
    // Manual trading during replay
    bool place_order(const std::string& symbol, OrderSide side, Quantity quantity, Price price = 0.0);
    
    // Get current market data
    OHLC get_current_market_data() const;
    std::vector<OHLC> get_historical_data() const;
    
    // Portfolio information
    Price get_portfolio_value() const;
    std::vector<Position> get_positions() const;
    
    // Event callbacks
    void set_event_callback(ReplayEventCallback callback);
    
    // Step forward one bar (for manual control)
    bool step_forward();

private:
    // Core components
    std::unique_ptr<DataHandler> data_handler_;
    std::unique_ptr<PortfolioManager> portfolio_manager_;
    std::unique_ptr<ExecutionHandler> execution_handler_;
    
    // Configuration
    ReplayConfig config_;
    
    // Historical data
    std::vector<OHLC> historical_data_;
    std::vector<Timestamp> timestamps_;
    
    // State
    bool is_running_;
    bool is_paused_;
    size_t current_bar_index_;
    double speed_multiplier_;
    
    // Event callback
    ReplayEventCallback event_callback_;
    
    // Thread for replay loop
    std::thread replay_thread_;
    std::atomic<bool> should_stop_;
    
    // Internal methods
    bool load_data();
    void replay_loop();
    void process_bar(size_t bar_index);
    void emit_event(const std::string& type, const nlohmann::json& data);
    std::string format_date(const Timestamp& ts) const;
};

} // namespace backtesting

