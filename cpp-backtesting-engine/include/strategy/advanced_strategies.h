#pragma once

#include "strategy.h"
#include "strategy_factory.h"
#include <deque>
#include <algorithm>

namespace backtesting {

// Forward declaration
struct StrategyMetrics;

// Momentum strategy using multiple indicators
class MomentumStrategy : public Strategy {
private:
    // Parameters
    int lookback_period_;
    int rsi_period_;
    int volume_sma_period_;
    double rsi_oversold_threshold_;
    double rsi_overbought_threshold_;
    double momentum_threshold_;
    double volume_factor_threshold_;
    bool use_volume_confirmation_;
    
    // Data storage
    std::deque<OHLC> price_history_;
    std::deque<double> rsi_values_;
    std::deque<double> momentum_values_;
    std::deque<double> volume_sma_values_;
    
    // Internal state
    bool is_initialized_;
    Signal last_signal_;
    int bars_since_signal_;
    double entry_price_;
    
    // Calculation methods
    double calculate_price_momentum(int period) const;
    double calculate_volume_momentum(int period) const;
    double calculate_rsi() const;
    double calculate_volume_sma() const;
    bool is_volume_confirming() const;
    bool is_trend_strong() const;
    Signal determine_signal(const OHLC& current_data);
    
public:
    MomentumStrategy(
        int lookback_period = 14,
        int rsi_period = 14,
        int volume_sma_period = 20,
        double rsi_oversold = 30.0,
        double rsi_overbought = 70.0,
        double momentum_threshold = 0.02,
        double volume_factor = 1.5,
        bool use_volume_confirmation = true
    );
    
    ~MomentumStrategy() override = default;
    
    // Strategy interface implementation
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "Momentum Strategy"; }
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
    // Strategy-specific methods
    double get_current_momentum() const;
    double get_current_rsi() const;
    bool is_in_position() const { return last_signal_ != Signal::NONE; }
    int get_bars_since_signal() const { return bars_since_signal_; }
    
    // Configuration
    void set_momentum_threshold(double threshold) { momentum_threshold_ = threshold; }
    void set_volume_confirmation(bool use_volume) { use_volume_confirmation_ = use_volume; }
};

// Mean reversion strategy
class MeanReversionStrategy : public Strategy {
private:
    // Parameters
    int bollinger_period_;
    double bollinger_std_dev_;
    int rsi_period_;
    double rsi_oversold_;
    double rsi_overbought_;
    double mean_reversion_threshold_;
    int max_hold_period_;
    
    // Data storage
    std::deque<OHLC> price_history_;
    std::deque<double> bollinger_upper_;
    std::deque<double> bollinger_lower_;
    std::deque<double> bollinger_middle_;
    std::deque<double> rsi_values_;
    
    // State tracking
    bool is_initialized_;
    Signal current_position_;
    int bars_in_position_;
    double entry_price_;
    
    // Calculation methods
    void calculate_bollinger_bands();
    double calculate_current_rsi() const;
    bool is_oversold() const;
    bool is_overbought() const;
    bool is_near_bollinger_bands() const;
    Signal determine_mean_reversion_signal(const OHLC& current_data);
    
public:
    MeanReversionStrategy(
        int bollinger_period = 20,
        double bollinger_std_dev = 2.0,
        int rsi_period = 14,
        double rsi_oversold = 30.0,
        double rsi_overbought = 70.0,
        double mean_reversion_threshold = 0.02,
        int max_hold_period = 10
    );
    
    ~MeanReversionStrategy() override = default;
    
    // Strategy interface implementation
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "Mean Reversion Strategy"; }
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
    // Strategy-specific methods
    double get_current_bollinger_position() const;
    bool is_in_oversold_territory() const;
    bool is_in_overbought_territory() const;
};

// Breakout strategy
class BreakoutStrategy : public Strategy {
private:
    // Parameters
    int lookback_period_;
    double breakout_threshold_;
    int volume_confirmation_period_;
    double volume_multiplier_;
    int atr_period_;
    double atr_multiplier_;
    bool use_dynamic_stops_;
    
    // Data storage
    std::deque<OHLC> price_history_;
    std::deque<double> volume_sma_;
    std::deque<double> atr_values_;
    
    // Breakout levels
    double resistance_level_;
    double support_level_;
    bool breakout_confirmed_;
    
    // State tracking
    bool is_initialized_;
    Signal current_direction_;
    double entry_price_;
    double stop_loss_level_;
    double take_profit_level_;
    
    // Calculation methods
    void calculate_breakout_levels();
    double calculate_atr() const;
    double calculate_volume_average() const;
    bool is_volume_confirming(const OHLC& current_data) const;
    bool is_price_breaking_out(const OHLC& current_data) const;
    void update_stop_levels(const OHLC& current_data);
    Signal determine_breakout_signal(const OHLC& current_data);
    
public:
    BreakoutStrategy(
        int lookback_period = 20,
        double breakout_threshold = 0.01,
        int volume_confirmation_period = 10,
        double volume_multiplier = 1.5,
        int atr_period = 14,
        double atr_multiplier = 2.0,
        bool use_dynamic_stops = true
    );
    
    ~BreakoutStrategy() override = default;
    
    // Strategy interface implementation
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "Breakout Strategy"; }
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
    // Strategy-specific methods
    double get_resistance_level() const { return resistance_level_; }
    double get_support_level() const { return support_level_; }
    bool is_breakout_confirmed() const { return breakout_confirmed_; }
    double get_current_atr() const;
};

// Ensemble strategy that combines multiple strategies
class EnsembleStrategy : public Strategy {
private:
    struct StrategyWeight {
        std::unique_ptr<Strategy> strategy;
        double weight;
        double confidence;
        StrategyMetrics performance;
    };
    
    std::vector<StrategyWeight> strategies_;
    std::string combination_method_;
    double confidence_threshold_;
    bool adaptive_weighting_;
    
    // Performance tracking
    std::unordered_map<std::string, std::vector<double>> strategy_returns_;
    int rebalance_frequency_;
    int bars_since_rebalance_;
    
    // Signal combination methods
    Signal combine_signals_weighted_average() const;
    Signal combine_signals_majority_vote() const;
    Signal combine_signals_confidence_weighted() const;
    void update_strategy_weights();
    
public:
    EnsembleStrategy(
        std::vector<std::unique_ptr<Strategy>> strategies,
        std::vector<double> weights,
        const std::string& combination_method = "weighted_average",
        double confidence_threshold = 0.6,
        bool adaptive_weighting = true
    );
    
    ~EnsembleStrategy() override = default;
    
    // Strategy interface implementation
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override;
    
    void initialize(const std::vector<OHLC>& historical_data) override;
    void update(const OHLC& new_data) override;
    std::string get_name() const override { return "Ensemble Strategy"; }
    std::unordered_map<std::string, double> get_parameters() const override;
    void reset() override;
    bool is_ready() const override;
    
    // Ensemble-specific methods
    void add_strategy(std::unique_ptr<Strategy> strategy, double weight = 1.0);
    void remove_strategy(const std::string& strategy_name);
    void set_strategy_weight(const std::string& strategy_name, double weight);
    std::vector<std::pair<std::string, double>> get_strategy_weights() const;
    nlohmann::json get_ensemble_status() const;
};

} // namespace backtesting 