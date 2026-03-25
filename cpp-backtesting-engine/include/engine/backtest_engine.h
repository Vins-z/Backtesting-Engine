#pragma once

#include "common/types.h"
#include "context/regime_classifier.h"
#include "data/data_handler.h"
#include "strategy/strategy.h"
#include "portfolio/portfolio_manager.h"
#include "execution/execution_handler.h"
#include "risk/risk_manager.h"
#include "performance/performance_analyzer.h"
#include <queue>
#include <memory>
#include <deque>
#include <spdlog/spdlog.h>
#include <thread>
#include <nlohmann/json.hpp>

namespace backtesting {

// Configuration structure
struct BacktestConfig {
    std::string name;
    std::string description;
    std::vector<std::string> symbols;
    std::string start_date;
    std::string end_date;
    Price initial_capital;
    Price commission_rate;
    Price slippage_rate;
    std::string data_source;
    std::string data_path;
    std::string data_interval;  // Data interval: 1m, 5m, 15m, 30m, 1h, 1d
    std::string api_key;  // For API-based data sources like Alpha Vantage
    std::string output_path;
    bool verbose_logging;
    int max_concurrent_positions;
    
    // Account and market configuration
    // account_type: "CASH" (default) or "MARGIN" (reserved for future use)
    std::string account_type;
    // market_type: "IN_EQUITY" (NSE/BSE), "US_EQUITY", or "OTHER"
    std::string market_type;
    
    // Strategy configuration
    std::string strategy_name;
    std::unordered_map<std::string, double> strategy_params;
    // Optional string/JSON strategy parameters (e.g. multi-leg definitions)
    std::unordered_map<std::string, std::string> strategy_string_params;
    // Raw JSON strategy definition for complex multi-leg strategies
    nlohmann::json strategy_definition_json;
    
    // Risk configuration
    Price max_position_size;
    Price max_portfolio_risk;
    Price stop_loss_percentage;
    Price max_daily_loss;
    
    // Load from YAML file
    static BacktestConfig load_from_yaml(const std::string& yaml_file);
    
    // Convert to JSON
    nlohmann::json to_json() const;
    
    // Validate configuration
    bool validate() const;
};

// Backtest result structure
struct BacktestResult {
    BacktestConfig config;
    PerformanceMetrics metrics;
    std::vector<Fill> trade_history;
    std::vector<std::pair<Timestamp, Price>> equity_curve;
    std::unordered_map<std::string, double> execution_stats;
    std::unordered_map<std::string, double> risk_metrics;
    std::string start_time;
    std::string end_time;
    double duration_seconds;
    
    // Convert to JSON
    nlohmann::json to_json() const;
    
    // Save to file
    bool save_to_file(const std::string& filename) const;
    
    // Load from file
    static BacktestResult load_from_file(const std::string& filename);
};

// Main backtest engine class
class BacktestEngine {
private:
    // Core components
    std::unique_ptr<DataHandler> data_handler_;
    std::unique_ptr<Strategy> strategy_;
    std::unique_ptr<PortfolioManager> portfolio_manager_;
    std::unique_ptr<ExecutionHandler> execution_handler_;
    std::unique_ptr<RiskManager> risk_manager_;
    std::unique_ptr<PerformanceAnalyzer> performance_analyzer_;
    
    // Configuration
    BacktestConfig config_;
    
    // Event queue for event-driven architecture
    std::queue<std::unique_ptr<Event>> event_queue_;
    
    // State tracking
    bool is_running_;
    bool is_paused_;
    int current_bar_;
    Timestamp current_time_;
    int order_id_counter_;

    // Context filters (volatility, regime) - for "when not to trade"
    bool use_volatility_filter_ = false;
    double max_volatility_pct_ = 5.0;  // e.g. 5.0 = skip if ATR% > 5%
    bool use_regime_filter_ = false;
    std::vector<std::string> allowed_regimes_;
    std::unordered_map<std::string, std::deque<OHLC>> ohlc_history_;
    RegimeClassifier regime_classifier_;
    struct BarContext {
        std::string regime;
        double volatility_pct = 0.0;
        double atr_at_bar = 0.0;
        std::string filter_reason;
    };
    BarContext current_bar_context_;

    // Adaptive position sizing (streak, regime)
    bool use_streak_sizing_ = false;
    int streak_after_losses_ = 3;
    double streak_size_multiplier_ = 0.5;
    bool use_regime_sizing_ = false;
    std::unordered_map<std::string, double> regime_size_multipliers_;
    int consecutive_wins_ = 0;
    int consecutive_losses_ = 0;
    double get_position_size_multiplier() const;

    
    // Logging
    std::shared_ptr<spdlog::logger> logger_;
    
    // Performance tracking
    std::chrono::high_resolution_clock::time_point start_time_;
    std::chrono::high_resolution_clock::time_point end_time_;
    
    // Event processing methods
    void process_market_event(const MarketEvent& event);
    void process_signal_event(const SignalEvent& event);
    void process_order_event(const OrderEvent& event);
    void process_fill_event(const FillEvent& event);
    
    // Helper methods
    void generate_signals();
    void update_portfolio();
    void check_stop_losses();
    void log_state();
    
    // Initialization methods
    bool initialize_components();
    bool load_data();
    bool validate_setup();
    
public:
    BacktestEngine();
    ~BacktestEngine();
    
    // Configuration
    bool configure(const BacktestConfig& config);
    bool configure_from_yaml(const std::string& yaml_file);
    
    // Component setters (for custom implementations)
    void set_data_handler(std::unique_ptr<DataHandler> handler);
    void set_strategy(std::unique_ptr<Strategy> strategy);
    void set_portfolio_manager(std::unique_ptr<PortfolioManager> manager);
    void set_execution_handler(std::unique_ptr<ExecutionHandler> handler);
    void set_risk_manager(std::unique_ptr<RiskManager> manager);
    void set_performance_analyzer(std::unique_ptr<PerformanceAnalyzer> analyzer);
    
    // Execution methods
    BacktestResult run_backtest();
    void run_backtest_async(std::function<void(const BacktestResult&)> callback);
    
    // Progress reporting
    using ProgressCallback = std::function<void(const nlohmann::json&)>;
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = std::move(callback); }
    
private:
    ProgressCallback progress_callback_;
    std::thread async_backtest_thread_;
    
    public:
    // Control methods
    void pause();
    void resume();
    void stop();
    void reset();
    
    // Real-time monitoring
    void add_event_listener(std::function<void(const Event&)> listener);
    
    // State getters
    bool is_running() const { return is_running_; }
    bool is_paused() const { return is_paused_; }
    int get_current_bar() const { return current_bar_; }
    Timestamp get_current_time() const { return current_time_; }
    
    // Component getters (for inspection)
    const PortfolioManager* get_portfolio_manager() const { return portfolio_manager_.get(); }
    PortfolioManager& get_portfolio_manager() { return *portfolio_manager_; }
    DataHandler* get_data_handler() const { return data_handler_.get(); }
    const Strategy* get_strategy() const { return strategy_.get(); }
    const BacktestConfig& get_config() const { return config_; }
    
    // Utility methods
    void enable_logging(const std::string& log_file = "", spdlog::level::level_enum level = spdlog::level::info);
    void save_state(const std::string& filename) const;
    bool load_state(const std::string& filename);
    
    // Static factory methods
    static std::unique_ptr<BacktestEngine> create_simple_engine(
        const std::string& strategy_name,
        const std::vector<std::string>& symbols,
        const std::string& data_path,
        Price initial_capital = 100000.0
    );
    
    static std::unique_ptr<BacktestEngine> create_from_config(const BacktestConfig& config);
};

// Batch backtesting for parameter optimization
class BatchBacktester {
private:
    BacktestConfig base_config_;
    std::vector<std::unordered_map<std::string, double>> parameter_sets_;
    std::vector<BacktestResult> results_;
    
public:
    BatchBacktester(const BacktestConfig& base_config);
    
    // Add parameter set for testing
    void add_parameter_set(const std::unordered_map<std::string, double>& params);
    
    // Generate parameter grid
    void generate_parameter_grid(const std::unordered_map<std::string, std::vector<double>>& param_ranges);
    
    // Run all backtests
    std::vector<BacktestResult> run_all_backtests(int max_parallel = 4);
    
    // Get best result by metric
    BacktestResult get_best_result(const std::string& metric = "sharpe_ratio") const;
    
    // Export results
    bool export_results(const std::string& filename) const;
    
    // Clear results
    void clear_results() { results_.clear(); }
};

// Live trading adapter (for paper trading)
class LiveTradingAdapter {
private:
    std::unique_ptr<BacktestEngine> engine_;
    bool is_live_;
    std::thread trading_thread_;
    
public:
    LiveTradingAdapter(std::unique_ptr<BacktestEngine> engine);
    ~LiveTradingAdapter();
    
    // Start live trading
    void start_live_trading();
    
    // Stop live trading
    void stop_live_trading();
    
    // Update with live data
    void update_live_data(const std::string& symbol, const OHLC& data);
    
    // Get current portfolio state
    nlohmann::json get_portfolio_state() const;
    
    // Get current positions
    nlohmann::json get_positions() const;
};

} // namespace backtesting 