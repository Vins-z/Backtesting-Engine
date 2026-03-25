#include "engine/replay_engine.h"
#include "data/data_handler_factory.h"
#include "execution/execution_handler.h"
#include "portfolio/portfolio_manager.h"
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace backtesting {

ReplayEngine::ReplayEngine()
    : is_running_(false), is_paused_(false), current_bar_index_(0),
      speed_multiplier_(1.0), should_stop_(false) {
}

bool ReplayEngine::configure(const ReplayConfig& config) {
    if (!config.validate()) {
        return false;
    }
    
    config_ = config;
    speed_multiplier_ = config.speed_multiplier;
    
    // Initialize data handler
    if (config.data_interval.empty()) {
        data_handler_ = DataHandlerFactory::create(config.data_source, config.data_path, "", config.data_interval);
    } else {
        data_handler_ = DataHandlerFactory::create(config.data_source, config.data_path, "", config.data_interval);
    }
    
    if (!data_handler_) {
        return false;
    }
    
    // Initialize portfolio manager
    portfolio_manager_ = std::make_unique<PortfolioManager>(config.initial_capital);
    
    // Initialize execution handler
    execution_handler_ = std::make_unique<SimpleExecutionHandler>(config.commission_rate, config.slippage_rate);
    
    // Load historical data
    return load_data();
}

bool ReplayEngine::load_data() {
    try {
        auto data = data_handler_->get_historical_data(
            config_.symbol,
            config_.start_date,
            config_.end_date
        );
        
        if (data.empty()) {
            return false;
        }
        
        historical_data_ = data;
        timestamps_.clear();
        for (const auto& bar : historical_data_) {
            timestamps_.push_back(bar.timestamp);
        }
        
        current_bar_index_ = 0;
        return true;
    } catch (...) {
        return false;
    }
}

bool ReplayEngine::start() {
    if (is_running_) {
        return false;
    }
    
    if (historical_data_.empty()) {
        return false;
    }
    
    is_running_ = true;
    is_paused_ = false;
    should_stop_ = false;
    
    // Start replay thread
    replay_thread_ = std::thread(&ReplayEngine::replay_loop, this);
    
    emit_event("replay_started", nlohmann::json{{"symbol", config_.symbol}});
    return true;
}

void ReplayEngine::pause() {
    is_paused_ = true;
    emit_event("replay_paused", nlohmann::json{});
}

void ReplayEngine::resume() {
    is_paused_ = false;
    emit_event("replay_resumed", nlohmann::json{});
}

void ReplayEngine::stop() {
    should_stop_ = true;
    is_running_ = false;
    is_paused_ = false;
    
    if (replay_thread_.joinable()) {
        replay_thread_.join();
    }
    
    emit_event("replay_stopped", nlohmann::json{});
}

void ReplayEngine::seek_to_date(const std::string& date) {
    // Find the bar index for the given date
    for (size_t i = 0; i < timestamps_.size(); ++i) {
        std::string bar_date = format_date(timestamps_[i]);
        if (bar_date >= date) {
            current_bar_index_ = i;
            emit_event("replay_seeked", nlohmann::json{{"date", date}, {"bar_index", i}});
            return;
        }
    }
}

void ReplayEngine::set_speed(double multiplier) {
    speed_multiplier_ = multiplier;
    config_.speed_multiplier = multiplier;
    emit_event("speed_changed", nlohmann::json{{"multiplier", multiplier}});
}

ReplayState ReplayEngine::get_state() const {
    ReplayState state;
    if (!historical_data_.empty() && current_bar_index_ < historical_data_.size()) {
        state.current_date = format_date(timestamps_[current_bar_index_]);
    }
    state.current_bar_index = current_bar_index_;
    state.total_bars = historical_data_.size();
    state.is_playing = is_running_ && !is_paused_;
    state.is_paused = is_paused_;
    state.progress_percent = historical_data_.empty() ? 0.0 :
        (static_cast<double>(current_bar_index_) / historical_data_.size()) * 100.0;
    return state;
}

bool ReplayEngine::place_order(const std::string& symbol, OrderSide side, Quantity quantity, Price price) {
    if (!is_running_ || historical_data_.empty() || current_bar_index_ >= historical_data_.size()) {
        return false;
    }
    
    const OHLC& current_data = historical_data_[current_bar_index_];
    if (current_data.symbol != symbol) {
        return false;
    }
    
    // Create order
    Order order;
    order.id = portfolio_manager_->generate_order_id();
    order.symbol = symbol;
    order.side = side;
    order.quantity = quantity;
    order.price = price > 0.0 ? price : current_data.close; // Use market price if not specified
    order.type = OrderType::MARKET;
    order.timestamp = current_data.timestamp;
    
    // Execute order
    auto fill = execution_handler_->execute_order(order, current_data);
    fill.timestamp = current_data.timestamp;  // Use bar/simulation time, not wall-clock
    if (fill.quantity > 0) {
        portfolio_manager_->update_fill(fill);
        
        emit_event("order_filled", nlohmann::json{
            {"symbol", symbol},
            {"side", side == OrderSide::BUY ? "BUY" : "SELL"},
            {"quantity", fill.quantity},
            {"price", fill.price},
            {"timestamp", current_data.timestamp}
        });
        
        return true;
    }
    
    return false;
}

OHLC ReplayEngine::get_current_market_data() const {
    if (historical_data_.empty() || current_bar_index_ >= historical_data_.size()) {
        return OHLC{};
    }
    return historical_data_[current_bar_index_];
}

std::vector<OHLC> ReplayEngine::get_historical_data() const {
    return historical_data_;
}

Price ReplayEngine::get_portfolio_value() const {
    if (!portfolio_manager_) {
        return 0.0;
    }
    
    OHLC current_data = get_current_market_data();
    return portfolio_manager_->get_total_value(current_data);
}

std::vector<Position> ReplayEngine::get_positions() const {
    if (!portfolio_manager_) {
        return {};
    }
    return portfolio_manager_->get_positions();
}

void ReplayEngine::set_event_callback(ReplayEventCallback callback) {
    event_callback_ = callback;
}

bool ReplayEngine::step_forward() {
    if (historical_data_.empty() || current_bar_index_ >= historical_data_.size() - 1) {
        return false;
    }
    
    current_bar_index_++;
    process_bar(current_bar_index_);
    
    if (current_bar_index_ >= historical_data_.size() - 1) {
        // Reached end
        is_running_ = false;
        emit_event("replay_completed", nlohmann::json{});
    }
    
    return true;
}

void ReplayEngine::replay_loop() {
    while (!should_stop_ && current_bar_index_ < historical_data_.size()) {
        // Wait if paused
        while (is_paused_ && !should_stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (should_stop_) {
            break;
        }
        
        process_bar(current_bar_index_);
        
        // Move to next bar
        current_bar_index_++;
        
        // Check if we've reached the end
        if (current_bar_index_ >= historical_data_.size()) {
            is_running_ = false;
            emit_event("replay_completed", nlohmann::json{});
            break;
        }
        
        // Calculate delay based on data interval and speed multiplier
        // For daily data, we might want to advance faster
        // For intraday data, we need to respect the time intervals
        int delay_ms = 1000; // Default 1 second per bar
        if (config_.data_interval == "1d") {
            delay_ms = static_cast<int>(1000 / speed_multiplier_); // Faster for daily
        } else if (config_.data_interval == "1h") {
            delay_ms = static_cast<int>(100 / speed_multiplier_); // 100ms per hour
        } else {
            delay_ms = static_cast<int>(50 / speed_multiplier_); // 50ms for shorter intervals
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

void ReplayEngine::process_bar(size_t bar_index) {
    if (bar_index >= historical_data_.size()) {
        return;
    }
    
    const OHLC& bar = historical_data_[bar_index];
    
    // Update portfolio with current market data
    portfolio_manager_->update_market_data(bar.symbol, bar);
    
    // Emit bar update event
    emit_event("bar_update", nlohmann::json{
        {"symbol", bar.symbol},
        {"timestamp", bar.timestamp},
        {"open", bar.open},
        {"high", bar.high},
        {"low", bar.low},
        {"close", bar.close},
        {"volume", bar.volume},
        {"date", format_date(bar.timestamp)},
        {"bar_index", bar_index},
        {"total_bars", historical_data_.size()}
    });
}

void ReplayEngine::emit_event(const std::string& type, const nlohmann::json& data) {
    if (event_callback_) {
        nlohmann::json event;
        event["type"] = type;
        event["data"] = data;
        event["timestamp"] = std::time(nullptr);
        event_callback_(event);
    }
}

std::string ReplayEngine::format_date(const Timestamp& ts) const {
    std::time_t time = static_cast<std::time_t>(ts);
    std::tm* tm = std::localtime(&time);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d");
    return oss.str();
}

// ReplayConfig methods
nlohmann::json ReplayConfig::to_json() const {
    nlohmann::json j;
    j["symbol"] = symbol;
    j["start_date"] = start_date;
    j["end_date"] = end_date;
    j["initial_capital"] = initial_capital;
    j["commission_rate"] = commission_rate;
    j["slippage_rate"] = slippage_rate;
    j["data_source"] = data_source;
    j["data_path"] = data_path;
    j["data_interval"] = data_interval;
    j["speed_multiplier"] = speed_multiplier;
    return j;
}

bool ReplayConfig::validate() const {
    if (symbol.empty() || start_date.empty() || end_date.empty()) {
        return false;
    }
    if (initial_capital <= 0) {
        return false;
    }
    if (speed_multiplier <= 0) {
        return false;
    }
    return true;
}

// ReplayState methods
nlohmann::json ReplayState::to_json() const {
    nlohmann::json j;
    j["current_date"] = current_date;
    j["current_bar_index"] = current_bar_index;
    j["total_bars"] = total_bars;
    j["is_playing"] = is_playing;
    j["is_paused"] = is_paused;
    j["progress_percent"] = progress_percent;
    return j;
}

} // namespace backtesting

