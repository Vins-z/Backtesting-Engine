#pragma once

#include "strategy/strategy.h"
#include "common/types.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace backtesting {

// Machine learning strategy base class
class MachineLearningStrategy : public Strategy {
public:
    MachineLearningStrategy();
    ~MachineLearningStrategy() = default;
    
    // ML-specific methods
    virtual void train_model(const std::vector<MarketData>& training_data);
    virtual void update_model(const MarketData& new_data);
    virtual double predict_signal_strength(const MarketData& data) const;
    
    // Feature engineering
    virtual std::vector<double> extract_features(const MarketData& data) const;
    virtual void add_feature(const std::string& name, std::function<double(const MarketData&)> feature_func);
    
    // Model management
    virtual bool save_model(const std::string& filename) const;
    virtual bool load_model(const std::string& filename);
    virtual void reset_model();
    
    // Performance tracking
    virtual double get_model_accuracy() const;
    virtual double get_prediction_confidence() const;
    
    // Strategy generation
    Signal generate_signal(
        const std::string& symbol,
        const MarketData& data,
        const PortfolioManager& portfolio
    ) override;

protected:
    // ML model state
    bool model_trained_;
    double model_accuracy_;
    double prediction_confidence_;
    
    // Feature extraction
    std::unordered_map<std::string, std::function<double(const MarketData&)>> features_;
    std::vector<double> feature_weights_;
    
    // Training data
    std::vector<MarketData> training_data_;
    std::vector<Signal> training_labels_;
    
    // Helper methods
    virtual void normalize_features(std::vector<double>& features) const;
    virtual double calculate_signal_threshold() const;
};

// Neural Network Strategy
class NeuralNetworkStrategy : public MachineLearningStrategy {
public:
    NeuralNetworkStrategy();
    ~NeuralNetworkStrategy() = default;
    
    void train_model(const std::vector<MarketData>& training_data) override;
    double predict_signal_strength(const MarketData& data) const override;
    bool save_model(const std::string& filename) const override;
    bool load_model(const std::string& filename) override;

private:
    // Neural network parameters
    struct NeuralNetworkParams {
        std::vector<int> layer_sizes = {10, 20, 10, 1};  // Input, hidden, output
        double learning_rate = 0.001;
        int epochs = 1000;
        double dropout_rate = 0.2;
    };
    
    NeuralNetworkParams params_;
    std::vector<std::vector<std::vector<double>>> weights_;  // Layer weights
    std::vector<std::vector<double>> biases_;  // Layer biases
    
    // Training methods
    std::vector<double> forward_pass(const std::vector<double>& input) const;
    void backpropagate(const std::vector<double>& input, const std::vector<double>& target);
    double calculate_loss(const std::vector<double>& prediction, const std::vector<double>& target) const;
};

// Ensemble Strategy (Multiple ML models)
class EnsembleStrategy : public MachineLearningStrategy {
public:
    EnsembleStrategy();
    ~EnsembleStrategy() = default;
    
    void add_model(std::unique_ptr<MachineLearningStrategy> model, double weight = 1.0);
    void remove_model(const std::string& model_name);
    
    void train_model(const std::vector<MarketData>& training_data) override;
    double predict_signal_strength(const MarketData& data) const override;
    Signal generate_signal(
        const std::string& symbol,
        const MarketData& data,
        const PortfolioManager& portfolio
    ) override;

private:
    std::vector<std::unique_ptr<MachineLearningStrategy>> models_;
    std::vector<double> model_weights_;
    std::vector<std::string> model_names_;
    
    double ensemble_predict(const MarketData& data) const;
};

// Reinforcement Learning Strategy
class ReinforcementLearningStrategy : public MachineLearningStrategy {
public:
    ReinforcementLearningStrategy();
    ~ReinforcementLearningStrategy() = default;
    
    void train_model(const std::vector<MarketData>& training_data) override;
    void update_model(const MarketData& new_data) override;
    double predict_signal_strength(const MarketData& data) const override;
    
    // RL-specific methods
    void set_reward_function(std::function<double(const Fill&)> reward_func);
    void set_exploration_rate(double rate);
    void set_learning_rate(double rate);

private:
    // RL parameters
    struct RLParams {
        double learning_rate = 0.1;
        double discount_factor = 0.95;
        double exploration_rate = 0.1;
        int state_size = 10;
        int action_size = 3;  // Buy, Sell, Hold
    };
    
    RLParams params_;
    std::function<double(const Fill&)> reward_function_;
    std::unordered_map<std::string, std::vector<double>> q_table_;  // State-action values
    
    // RL methods
    std::string get_state(const MarketData& data) const;
    int select_action(const std::string& state) const;
    void update_q_value(const std::string& state, int action, double reward, const std::string& next_state);
    double calculate_reward(const Fill& trade) const;
};

// Adaptive Strategy (Self-modifying)
class AdaptiveStrategy : public MachineLearningStrategy {
public:
    AdaptiveStrategy();
    ~AdaptiveStrategy() = default;
    
    void train_model(const std::vector<MarketData>& training_data) override;
    void update_model(const MarketData& new_data) override;
    
    // Adaptation methods
    void adapt_to_market_regime(const std::string& regime);
    void adapt_to_volatility(double volatility);
    void adapt_to_trend_strength(double trend_strength);
    
    // Performance monitoring
    void monitor_performance(const std::vector<Fill>& recent_trades);
    bool should_adapt() const;

private:
    // Adaptation parameters
    struct AdaptationParams {
        double performance_threshold = 0.6;
        int adaptation_frequency = 100;  // trades
        double regime_change_threshold = 0.3;
        double volatility_threshold = 0.2;
    };
    
    AdaptationParams adapt_params_;
    std::string current_regime_;
    double current_volatility_;
    double current_trend_strength_;
    int trade_count_;
    double recent_performance_;
    
    // Adaptation methods
    void adapt_parameters();
    void detect_regime_change(const MarketData& data);
    void adjust_risk_parameters();
};

} // namespace backtesting
