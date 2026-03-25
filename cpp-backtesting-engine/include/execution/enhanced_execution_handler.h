#pragma once

#include \"execution_handler.h\"
#include <deque>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace backtesting {

// Market microstructure data
struct MarketMicrostructure {
    Price bid_ask_spread = 0.001;
    Volume total_volume = 1000000;
    Volume bid_volume = 500000;
    Volume ask_volume = 500000;
    int order_book_depth = 5;
    double volatility = 0.02;
    double liquidity_factor = 1.0;
    
    // Time-based factors
    bool is_market_open = true;
    bool is_pre_market = false;
    bool is_after_hours = false;
    
    nlohmann::json to_json() const;
};

// Enhanced execution model with market impact and liquidity
class EnhancedExecutionHandler : public ExecutionHandler {
private:
    // Configuration
    struct ExecutionConfig {
        // Commission structure
        Price base_commission_rate = 0.001;
        Price min_commission = 1.0;
        Price max_commission_rate = 0.005;
        bool use_tiered_commissions = true;
        
        // Market impact model
        double permanent_impact_factor = 0.5;
        double temporary_impact_factor = 0.3;
        double impact_decay_rate = 0.1;
        
        // Slippage model
        double base_slippage_bps = 2.0; // 2 basis points
        double volume_slippage_factor = 0.1;
        double volatility_slippage_factor = 0.2;
        
        // Liquidity modeling
        bool model_liquidity = true;
        double min_liquidity_factor = 0.1;
        double liquidity_recovery_rate = 0.05;
        
        // Timing effects
        bool model_timing_effects = true;
        double opening_volatility_multiplier = 2.0;
        double closing_volatility_multiplier = 1.5;
        
        nlohmann::json to_json() const;
    };
    
    ExecutionConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // Market state tracking
    std::unordered_map<std::string, MarketMicrostructure> market_state_;
    std::unordered_map<std::string, std::deque<double>> impact_history_;
    std::unordered_map<std::string, double> cumulative_impact_;
    
    // Execution statistics
    mutable int total_orders_ = 0;
    mutable Price total_commission_ = 0.0;
    mutable Price total_slippage_ = 0.0;
    mutable Price total_market_impact_ = 0.0;
    mutable int rejected_orders_ = 0;
    mutable std::vector<Fill> execution_history_;
    
    // Random number generation
    mutable std::mt19937 rng_;
    mutable std::normal_distribution<double> normal_dist_;
    
    // Internal methods
    MarketMicrostructure& get_market_state(const std::string& symbol);
    void update_market_state(const std::string& symbol, const OHLC& market_data);
    
    Price calculate_enhanced_commission(const Order& order, Price execution_price) const;
    Price calculate_market_impact(const Order& order, const MarketMicrostructure& market_state) const;
    Price calculate_enhanced_slippage(const Order& order, const MarketMicrostructure& market_state) const;
    Price calculate_timing_adjustment(const Order& order, const OHLC& market_data) const;
    
    bool check_liquidity_constraints(const Order& order, const MarketMicrostructure& market_state) const;
    void update_impact_history(const std::string& symbol, double impact);
    
    Price get_realistic_execution_price(const Order& order, const OHLC& market_data, 
                                       const MarketMicrostructure& market_state) const;
    
public:
    explicit EnhancedExecutionHandler(const ExecutionConfig& config = ExecutionConfig{});
    ~EnhancedExecutionHandler() override = default;
    
    // ExecutionHandler interface
    Fill execute_order(const Order& order, const OHLC& current_data) override;
    Price calculate_commission(Quantity quantity, Price price) const override;
    Price calculate_slippage(Price price, OrderSide side) const override;
    std::unordered_map<std::string, double> get_execution_stats() const override;
    
    // Enhanced functionality
    void set_market_hours(const std::string& symbol, bool is_open, bool is_pre_market = false, bool is_after_hours = false);
    void update_liquidity_state(const std::string& symbol, double liquidity_factor);
    
    // Advanced analytics
    nlohmann::json get_detailed_execution_analytics() const;
    std::vector<Fill> get_execution_history() const { return execution_history_; }
    void reset_statistics();
    
    // Configuration
    void set_config(const ExecutionConfig& config) { config_ = config; }
    const ExecutionConfig& get_config() const { return config_; }
};

// Smart order routing for optimal execution
class SmartOrderRouter {
private:
    struct VenueInfo {
        std::string name;
        std::unique_ptr<ExecutionHandler> handler;
        double fee_rate;
        double liquidity_score;
        double speed_score;
        bool is_dark_pool;
        
        VenueInfo(std::string n, std::unique_ptr<ExecutionHandler> h, 
                 double fee, double liquidity, double speed, bool dark = false)
            : name(std::move(n)), handler(std::move(h)), fee_rate(fee),
              liquidity_score(liquidity), speed_score(speed), is_dark_pool(dark) {}
    };
    
    std::vector<VenueInfo> venues_;
    std::unordered_map<std::string, std::vector<Fill>> execution_cache_;
    
    // Routing logic
    std::vector<std::pair<int, Quantity>> route_order(const Order& order, const OHLC& market_data) const;
    double calculate_venue_score(const VenueInfo& venue, const Order& order, const OHLC& market_data) const;
    
public:
    SmartOrderRouter() = default;
    
    // Venue management
    void add_venue(const std::string& name, std::unique_ptr<ExecutionHandler> handler, 
                  double fee_rate, double liquidity_score, double speed_score, bool is_dark_pool = false);
    void remove_venue(const std::string& name);
    
    // Order execution
    std::vector<Fill> execute_order(const Order& order, const OHLC& market_data);
    
    // Analytics
    nlohmann::json get_routing_statistics() const;
    std::vector<std::string> get_available_venues() const;
};

// Transaction cost analysis
class TransactionCostAnalyzer {
private:
    struct CostBreakdown {
        Price commission = 0.0;
        Price slippage = 0.0;
        Price market_impact = 0.0;
        Price opportunity_cost = 0.0;
        Price total_cost = 0.0;
        double cost_basis_points = 0.0;
        
        nlohmann::json to_json() const;
    };
    
    std::vector<std::pair<Fill, CostBreakdown>> cost_history_;
    std::unordered_map<std::string, std::vector<CostBreakdown>> symbol_costs_;
    
public:
    TransactionCostAnalyzer() = default;
    
    // Cost analysis
    CostBreakdown analyze_fill(const Fill& fill, const OHLC& market_data_before, 
                              const OHLC& market_data_after) const;
    void record_execution(const Fill& fill, const OHLC& before, const OHLC& after);
    
    // Reporting
    nlohmann::json get_cost_summary() const;
    nlohmann::json get_symbol_cost_analysis(const std::string& symbol) const;
    double calculate_implementation_shortfall(const std::vector<Fill>& fills, 
                                            Price benchmark_price) const;
    
    // Benchmarking
    struct BenchmarkResult {
        double vs_vwap_bps = 0.0;
        double vs_arrival_price_bps = 0.0;
        double vs_close_price_bps = 0.0;
        double total_cost_bps = 0.0;
        
        nlohmann::json to_json() const;
    };
    
    BenchmarkResult benchmark_execution(const std::vector<Fill>& fills, 
                                       const std::vector<OHLC>& market_data,
                                       Price vwap_benchmark) const;
};

// Execution algorithm factory
class ExecutionAlgorithmFactory {
public:
    enum class AlgorithmType {
        MARKET,           // Immediate market execution
        TWAP,            // Time-weighted average price
        VWAP,            // Volume-weighted average price
        IMPLEMENTATION_SHORTFALL, // Minimize implementation shortfall
        PARTICIPATION_RATE,       // Target participation rate
        SMART_ROUTING            // Smart order routing
    };
    
    static std::unique_ptr<ExecutionHandler> create_algorithm(
        AlgorithmType type, 
        const nlohmann::json& config = nlohmann::json{}
    );
    
    static std::vector<std::string> get_available_algorithms();
    static nlohmann::json get_algorithm_description(AlgorithmType type);
};

// TWAP (Time-Weighted Average Price) execution algorithm
class TWAPExecutionHandler : public ExecutionHandler {
private:
    int total_slices_;
    std::chrono::minutes slice_duration_;
    std::unique_ptr<ExecutionHandler> base_handler_;
    
    struct TWAPState {
        Quantity remaining_quantity;
        int slices_executed;
        std::chrono::system_clock::time_point start_time;
        std::vector<Fill> slice_fills;
    };
    
    std::unordered_map<int, TWAPState> active_orders_;
    
public:
    TWAPExecutionHandler(int slices, std::chrono::minutes duration, 
                        std::unique_ptr<ExecutionHandler> base_handler);
    
    Fill execute_order(const Order& order, const OHLC& current_data) override;
    std::unordered_map<std::string, double> get_execution_stats() const override;
    
    // TWAP specific methods
    std::vector<Fill> process_time_slice(const OHLC& current_data);
    bool is_order_complete(int order_id) const;
};

} // namespace backtesting 