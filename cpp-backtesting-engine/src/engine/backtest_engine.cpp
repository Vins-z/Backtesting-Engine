#include "engine/backtest_engine.h"
#include "strategy/strategy_factory.h"
#include "data/data_handler_factory.h"
#include "risk/risk_manager.h"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace backtesting {

namespace {
double compute_atr(const std::deque<OHLC>& data, int period) {
    if (data.size() < static_cast<size_t>(period + 1)) return 0.0;
    std::vector<double> true_ranges;
    for (size_t i = 1; i < data.size(); ++i) {
        double tr1 = data[i].high - data[i].low;
        double tr2 = std::abs(data[i].high - data[i - 1].close);
        double tr3 = std::abs(data[i].low - data[i - 1].close);
        true_ranges.push_back(std::max({tr1, tr2, tr3}));
    }
    if (true_ranges.size() < static_cast<size_t>(period)) return 0.0;
    double sum = 0.0;
    for (size_t i = true_ranges.size() - period; i < true_ranges.size(); ++i)
        sum += true_ranges[i];
    return sum / period;
}
}  // namespace

BacktestEngine::BacktestEngine()
    : is_running_(false), is_paused_(false), current_bar_(0), order_id_counter_(1),
      regime_classifier_(50, 14, 50) {
    logger_ = spdlog::get("backtest");
    if (!logger_) {
        try {
            logger_ = spdlog::stdout_color_mt("backtest");
        } catch (...) {
            // Fallback to basic stdout logger if colored logger fails
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            logger_ = std::make_shared<spdlog::logger>("backtest", console_sink);
            spdlog::register_logger(logger_);
        }
    }
}

BacktestEngine::~BacktestEngine() {
    if (async_backtest_thread_.joinable()) {
        // Avoid self-join deadlocks if the destructor runs on the async worker thread.
        if (std::this_thread::get_id() == async_backtest_thread_.get_id()) {
            async_backtest_thread_.detach();
        } else {
            async_backtest_thread_.join();
        }
    }
}

bool BacktestEngine::configure(const BacktestConfig& config) {
    config_ = config;
    return initialize_components();
}

bool BacktestEngine::configure_from_yaml(const std::string& yaml_file) {
    try {
        config_ = BacktestConfig::load_from_yaml(yaml_file);
        return initialize_components();
    } catch (const std::exception& e) {
        logger_->error("Failed to load config from YAML: {}", e.what());
        return false;
    }
}

bool BacktestEngine::initialize_components() {
    try {
        logger_->info("Initializing backtest engine components");
        logger_->info("Creating data handler for source: {}, path: {}, interval: {}", 
                     config_.data_source, config_.data_path, config_.data_interval);
        
        // Initialize data handler with interval support
        if (config_.data_interval.empty()) {
            data_handler_ = DataHandlerFactory::create(config_.data_source, config_.data_path, config_.api_key);
        } else {
            data_handler_ = DataHandlerFactory::create(config_.data_source, config_.data_path, config_.api_key, config_.data_interval);
        }
        if (!data_handler_) {
            logger_->error("Failed to create data handler for source: {}", config_.data_source);
            return false;
        }
        logger_->info("Data handler created successfully");

        // Initialize strategy (supports both numeric and string parameters, including multi-leg)
        StrategyConfig strat_cfg;
        strat_cfg.name = config_.strategy_name;
        strat_cfg.parameters = config_.strategy_params;
        strat_cfg.string_parameters = config_.strategy_string_params;
        
        // Parse indicators and context filters from strategy_definition_json if available
        if (!config_.strategy_definition_json.is_null() && !config_.strategy_definition_json.empty()) {
            if (config_.strategy_definition_json.contains("indicators")) {
                // Convert indicators array to JSON string for string_parameters
                strat_cfg.string_parameters["indicators"] = config_.strategy_definition_json["indicators"].dump();
            }
            if (config_.strategy_definition_json.contains("legs")) {
                // Ensure legs are in string_parameters
                if (strat_cfg.string_parameters.find("legs") == strat_cfg.string_parameters.end()) {
                    strat_cfg.string_parameters["legs"] = config_.strategy_definition_json["legs"].dump();
                }
            }
            // Parse context filters (volatility, regime) for "when not to trade"
            if (config_.strategy_definition_json.contains("filters")) {
                const auto& filters = config_.strategy_definition_json["filters"];
                if (filters.contains("volatility") && filters["volatility"].is_object()) {
                    const auto& vol = filters["volatility"];
                    use_volatility_filter_ = vol.value("enabled", false);
                    if (vol.contains("maxATRPercent") && vol["maxATRPercent"].is_number()) {
                        max_volatility_pct_ = vol["maxATRPercent"].get<double>();
                    }
                }
                if (filters.contains("regime") && filters["regime"].is_object()) {
                    const auto& reg = filters["regime"];
                    use_regime_filter_ = reg.value("enabled", false);
                    if (reg.contains("allowedRegimes") && reg["allowedRegimes"].is_array()) {
                        for (const auto& r : reg["allowedRegimes"]) {
                            if (r.is_string()) allowed_regimes_.push_back(r.get<std::string>());
                        }
                    }
                }
            }
            // Parse adaptive sizing (streak, regime) for position size multipliers
            if (config_.strategy_definition_json.contains("sizing")) {
                const auto& sizing = config_.strategy_definition_json["sizing"];
                if (sizing.contains("streak_aware") && sizing["streak_aware"].is_object()) {
                    const auto& sa = sizing["streak_aware"];
                    use_streak_sizing_ = sa.value("enabled", false);
                    streak_after_losses_ = sa.value("after_losses", 3);
                    streak_size_multiplier_ = sa.value("size_multiplier", 0.5);
                }
                if (sizing.contains("regime_based") && sizing["regime_based"].is_object()) {
                    const auto& rb = sizing["regime_based"];
                    use_regime_sizing_ = rb.value("enabled", false);
                    for (auto it = rb.begin(); it != rb.end(); ++it) {
                        if (it.key() != "enabled" && it.value().is_number()) {
                            regime_size_multipliers_[it.key()] = it.value().get<double>();
                        }
                    }
                }
            }
        }
        // Also use StrategyConfig values if filters not in JSON
        if (!use_volatility_filter_ && strat_cfg.use_volatility_filter) {
            use_volatility_filter_ = true;
            max_volatility_pct_ = strat_cfg.max_volatility * 100.0;  // StrategyConfig uses 0.05 = 5%
        }
        
        strategy_ = StrategyFactory::create_from_config(strat_cfg);
        if (!strategy_) {
            logger_->error("Failed to create strategy: {}", config_.strategy_name);
            return false;
        }

        // Initialize portfolio manager with risk config from API (max_position_size 1.0 = 100% per position)
        RiskConfig risk_config;
        risk_config.max_position_size_pct = (config_.max_position_size > 0.0) ? config_.max_position_size : 1.0;
        portfolio_manager_ = std::make_unique<PortfolioManager>(config_.initial_capital, risk_config);
        
        // Initialize execution handler - use RealisticExecutionHandler for industry-standard execution
        // This provides proper market impact, realistic slippage, and better price execution
        execution_handler_ = std::make_unique<RealisticExecutionHandler>(
            config_.commission_rate,  // commission_rate
            1.0,                       // min_commission (minimum $1 per trade)
            0.005,                     // max_commission (0.5% max)
            config_.slippage_rate,     // slippage_rate
            0.001                      // market_impact_factor (0.1% impact factor)
        );
        
        // Initialize risk manager with breakeven/trailing stop support from strategy definition
        bool enable_breakeven = false;
        double breakeven_trigger_R = 1.0;
        bool enable_trailing = false;
        std::string trailing_type = "PERCENT";
        double trailing_value = 2.0;
        
        // Extract risk params from strategy definition if available
        if (!config_.strategy_definition_json.is_null() && !config_.strategy_definition_json.empty() && config_.strategy_definition_json.contains("risk")) {
            auto risk_json = config_.strategy_definition_json["risk"];
            enable_breakeven = risk_json.value("enable_breakeven", false);
            breakeven_trigger_R = risk_json.value("breakeven_trigger_R", 1.0);
            enable_trailing = risk_json.value("enable_trailing_stop", false);
            trailing_type = risk_json.value("trailing_type", std::string("PERCENT"));
            trailing_value = risk_json.value("trailing_value", 2.0);
        }
        
        auto basic_risk_manager = std::make_unique<BasicRiskManager>(
            config_.max_position_size, 0.02, config_.stop_loss_percentage, 0.05, 0.1,
            enable_breakeven, breakeven_trigger_R, enable_trailing, trailing_type, trailing_value
        );
        risk_manager_ = std::move(basic_risk_manager);
        
        // Initialize performance analyzer
        performance_analyzer_ = std::make_unique<BasicPerformanceAnalyzer>();

        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "engine.init";
            evt["message"] = "Components initialized";
            evt["strategy_name"] = config_.strategy_name;
            evt["symbols"] = config_.symbols;
            progress_callback_(evt);
        }
        return true;
    } catch (const std::exception& e) {
        logger_->error("Failed to initialize components: {}", e.what());
        return false;
    }
}

BacktestResult BacktestEngine::run_backtest() {
    logger_->info("Starting backtest: {}", config_.name);
    start_time_ = std::chrono::high_resolution_clock::now();
    
    BacktestResult result;
    result.config = config_;
    result.start_time = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(start_time_.time_since_epoch()).count());
    
    try {
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "backtest.start";
            evt["name"] = config_.name;
            evt["start_time_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(start_time_.time_since_epoch()).count();
            evt["config"] = config_.to_json();
            // Include strategy params explicitly for richer logs
            evt["strategy_name"] = config_.strategy_name;
            evt["strategy_params"] = config_.strategy_params;
            progress_callback_(evt);
        }
        if (!load_data()) {
            throw std::runtime_error("Failed to load market data");
        }
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "data.loaded";
            evt["symbols"] = config_.symbols;
            progress_callback_(evt);
        }
        
        is_running_ = true;
        current_bar_ = 0;
        ohlc_history_.clear();

        // Main backtest loop
        while (is_running_ && data_handler_->has_next()) {
            // Get next market data
            auto market_data = data_handler_->get_next();
            current_time_ = market_data.timestamp;
            
            // Create market event
            auto market_event = std::make_unique<MarketEvent>(market_data.symbol, market_data);
            process_market_event(*market_event);
            
            // Process all events in queue
            while (!event_queue_.empty()) {
                auto event = std::move(event_queue_.front());
                event_queue_.pop();
                
                switch (event->type) {
                    case EventType::SIGNAL:
                        process_signal_event(static_cast<const SignalEvent&>(*event));
                        break;
                    case EventType::ORDER:
                        process_order_event(static_cast<const OrderEvent&>(*event));
                        break;
                    case EventType::FILL:
                        process_fill_event(static_cast<const FillEvent&>(*event));
                        break;
                    default:
                        break;
                }
            }
            // Also allow delayed execution handlers to release fills (if used)
            // by pushing any generated fills back into the event queue. This is a no-op for SimpleExecutionHandler.
            
            // Update portfolio and check stops
            update_portfolio();
            check_stop_losses();
            
            current_bar_++;
            
            if (config_.verbose_logging && current_bar_ % 100 == 0) {
                logger_->info("Processed {} bars", current_bar_);
            }
            if (progress_callback_ && (current_bar_ < 50 || current_bar_ % 10 == 0)) {
                nlohmann::json evt;
                evt["type"] = "progress.bar";
                evt["current_bar"] = current_bar_;
                evt["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(current_time_.time_since_epoch()).count();
                evt["strategy_name"] = config_.strategy_name;
                // Optional: equity so far
                const auto* pm = portfolio_manager_.get();
                if (pm) {
                    auto equity_curve = pm->get_equity_curve();
                    if (!equity_curve.empty()) {
                        evt["equity"] = equity_curve.back().second;
                    }
                }
                // Throughput: bars/sec
                auto now = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double>(now - start_time_).count();
                if (elapsed > 0) {
                    evt["bars_per_sec"] = current_bar_ / elapsed;
                }
                progress_callback_(evt);
            }
        }
        
        // Finalize results
        end_time_ = std::chrono::high_resolution_clock::now();
        result.duration_seconds = std::chrono::duration<double>(end_time_ - start_time_).count();
        result.end_time = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(end_time_.time_since_epoch()).count());
        
        // Calculate performance metrics
        result.metrics = performance_analyzer_->calculate_metrics(
            portfolio_manager_->get_equity_curve(),
            portfolio_manager_->get_trade_history(),
            config_.initial_capital
        );
        
        // Get trade history and equity curve
        result.trade_history = portfolio_manager_->get_trade_history();
        result.equity_curve = portfolio_manager_->get_equity_curve();
        // Derive final balance for frontend convenience
        if (!result.equity_curve.empty()) {
            result.execution_stats["final_balance"] = result.equity_curve.back().second;
        } else {
            result.execution_stats["final_balance"] = config_.initial_capital;
        }
        
        logger_->info("Backtest completed. Total return: {:.2f}%, Total trades: {}", 
                     result.metrics.total_return * 100, result.metrics.total_trades);
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "backtest.complete";
            evt["duration_seconds"] = result.duration_seconds;
            evt["metrics"] = {
                {"total_return", result.metrics.total_return},
                {"total_trades", result.metrics.total_trades},
                {"win_rate", result.metrics.win_rate},
                {"max_drawdown", result.metrics.max_drawdown},
                {"sharpe_ratio", result.metrics.sharpe_ratio},
                {"profit_factor", result.metrics.profit_factor}
            };
            progress_callback_(evt);
        }
        
        // Send final summary via SSE
        if (progress_callback_) {
            nlohmann::json summary_evt;
            summary_evt["type"] = "backtest.summary";
            summary_evt["metrics"] = result.to_json();
            
            // Map common metrics to top-level for easier frontend access
            summary_evt["metrics"]["final_balance"] = result.execution_stats["final_balance"];
            summary_evt["metrics"]["duration_seconds"] = result.duration_seconds;
            
            progress_callback_(summary_evt);
        }

        
    } catch (const std::exception& e) {
        logger_->error("Backtest failed: {}", e.what());
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "backtest.error";
            evt["error"] = e.what();
            progress_callback_(evt);
        }
        throw;
    }
    
    is_running_ = false;
    return result;
}

void BacktestEngine::run_backtest_async(std::function<void(const BacktestResult&)> callback) {
    if (async_backtest_thread_.joinable()) {
        // Avoid joining the currently executing thread (can deadlock).
        if (std::this_thread::get_id() == async_backtest_thread_.get_id()) {
            async_backtest_thread_.detach();
        } else {
            async_backtest_thread_.join();
        }
    }
    async_backtest_thread_ = std::thread([this, cb = std::move(callback)]() {
        try {
            BacktestResult result = this->run_backtest();
            if (cb) cb(result);
        } catch (const std::exception& e) {
            logger_->error("Async backtest failed: {}", e.what());
            // The progress_callback_ will have already sent the backtest.error event
        }
    });
}

double BacktestEngine::get_position_size_multiplier() const {
    double mult = 1.0;
    if (use_streak_sizing_ && consecutive_losses_ >= streak_after_losses_) {
        mult = std::min(mult, streak_size_multiplier_);
    }
    if (use_regime_sizing_ && !current_bar_context_.regime.empty()) {
        auto it = regime_size_multipliers_.find(current_bar_context_.regime);
        if (it != regime_size_multipliers_.end() && it->second < mult) {
            mult = it->second;
        }
    }
    return mult;
}

void BacktestEngine::process_market_event(const MarketEvent& event) {
    // Update portfolio with current market data
    portfolio_manager_->update_market_data(event.symbol, event.data);

    // Build OHLC history for context (volatility, regime)
    ohlc_history_[event.symbol].push_back(event.data);
    constexpr size_t max_history = 500;
    if (ohlc_history_[event.symbol].size() > max_history) {
        ohlc_history_[event.symbol].pop_front();
    }

    // Compute decision context at this bar (regime + volatility)
    current_bar_context_ = BarContext{};
    const auto& history = ohlc_history_[event.symbol];
    if (history.size() >= 15) {
        double atr = compute_atr(history, 14);
        double atr_pct = (event.data.close > 0.0 && atr > 0.0)
            ? (atr / event.data.close * 100.0) : 0.0;
        current_bar_context_.atr_at_bar = atr;
        current_bar_context_.volatility_pct = atr_pct;
        std::vector<OHLC> history_vec(history.begin(), history.end());
        RegimeState regime_state = regime_classifier_.classify(history_vec, history_vec.size() - 1);
        current_bar_context_.regime = regime_state.regime;
    }

    // Regime filter: suppress signals when regime not in allowed list
    if (use_regime_filter_ && !allowed_regimes_.empty()) {
        bool regime_ok = false;
        for (const auto& r : allowed_regimes_) {
            if (r == current_bar_context_.regime) { regime_ok = true; break; }
        }
        if (!regime_ok) {
            current_bar_context_.filter_reason = "regime_filter";
            if (strategy_) strategy_->update(event.data);
            return;
        }
    }

    // Volatility filter: suppress signals when ATR% exceeds max
    if (use_volatility_filter_ && current_bar_context_.volatility_pct > 0.0) {
        if (current_bar_context_.volatility_pct > max_volatility_pct_) {
            current_bar_context_.filter_reason = "vol_filter";
            if (strategy_) strategy_->update(event.data);
            return;  // Do not generate or push any signal
        }
    }

    // Feed strategy with the latest bar before generating a signal
    if (strategy_) {
        strategy_->update(event.data);
    }
    // Generate signals from strategy (uses internal history)
    auto signal = strategy_->generate_signal(event.symbol, event.data, *portfolio_manager_);

    if (signal != Signal::NONE) {
        auto signal_event = std::make_unique<SignalEvent>(event.symbol, signal);
        event_queue_.push(std::move(signal_event));
    }
}

void BacktestEngine::process_signal_event(const SignalEvent& event) {
    // Check risk management
    if (!risk_manager_->check_signal(event, *portfolio_manager_)) {
        logger_->debug("Signal rejected by risk manager: {} {}", 
                      event.symbol, static_cast<int>(event.signal));
        return;
    }
    
    // Generate order
    auto order = portfolio_manager_->generate_order(event);
    // Apply adaptive position size multiplier only for BUY (entry) orders, not SELL (exit)
    if (order.quantity > 0 && order.side == OrderSide::BUY) {
        double mult = get_position_size_multiplier();
        if (mult < 1.0 && mult > 0.0) {
            order.quantity = static_cast<Quantity>(std::max(Quantity(1), static_cast<Quantity>(order.quantity * mult)));
        }
    }
    if (order.quantity > 0) {
        auto order_event = std::make_unique<OrderEvent>(order);
        event_queue_.push(std::move(order_event));
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "event.order";
            evt["symbol"] = order.symbol;
            evt["quantity"] = order.quantity;
            evt["side"] = (order.side == OrderSide::BUY) ? "BUY" : "SELL";
            evt["price"] = order.price;
            progress_callback_(evt);
        }
    }
}

void BacktestEngine::process_order_event(const OrderEvent& event) {
    // Start from the generated order and normalize quantity for markets that require whole shares
    Order order = event.order;
    
    // For Indian cash equities, enforce whole-share quantities before any validation or execution.
    if (config_.market_type == "IN_EQUITY" && order.quantity > 0) {
        Quantity int_qty = static_cast<Quantity>(std::floor(order.quantity));
        if (int_qty <= 0) {
            logger_->info("Rejecting order {} for {} due to sub-1 share quantity ({}); whole shares required for IN_EQUITY",
                          order.id, order.symbol, order.quantity);
            if (progress_callback_) {
                nlohmann::json evt;
                evt["type"] = "event.order_rejected";
                evt["order_id"] = order.id;
                evt["symbol"] = order.symbol;
                evt["reason"] = "Quantity below 1 share for IN_EQUITY";
                progress_callback_(evt);
            }
            return;
        }
        if (int_qty != order.quantity) {
            logger_->debug("Rounding order {} quantity for {} from {} to {} (IN_EQUITY whole-share constraint)",
                           order.id, order.symbol, order.quantity, int_qty);
            order.quantity = int_qty;
        }
    }

    // Execute order - need to get current market data for execution
    auto current_data = portfolio_manager_->get_current_market_data(order.symbol);
    
    // Validate market data is available
    if (current_data.close <= 0.0 || current_data.volume < 0) {
        logger_->warn("Invalid market data for {} when executing order {}: close={}, volume={}", 
                     order.symbol, order.id, current_data.close, current_data.volume);
        logger_->debug("Order {} rejected due to invalid market data", order.id);
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "event.order_rejected";
            evt["order_id"] = order.id;
            evt["symbol"] = order.symbol;
            evt["reason"] = "Invalid market data";
            progress_callback_(evt);
        }
        return;
    }
    
    // Pre-validate order before execution (comprehensive checks)
    if (!portfolio_manager_->pre_validate_order(order, current_data)) {
        logger_->warn("Order {} pre-validation failed: {} {} shares @ ${:.2f}", 
                     order.id,
                     order.side == OrderSide::BUY ? "BUY" : "SELL",
                     order.quantity, order.price);
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "event.order_rejected";
            evt["order_id"] = order.id;
            evt["symbol"] = order.symbol;
            evt["reason"] = "Pre-validation failed";
            progress_callback_(evt);
        }
        return;
    }
    
    // Additional risk manager validation
    if (!risk_manager_->is_order_allowed(order, *portfolio_manager_)) {
        logger_->warn("Order {} rejected by risk manager: {} {} shares @ ${:.2f}", 
                     order.id,
                     order.side == OrderSide::BUY ? "BUY" : "SELL",
                     order.quantity, order.price);
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "event.order_rejected";
            evt["order_id"] = order.id;
            evt["symbol"] = order.symbol;
            evt["reason"] = "Risk manager rejection";
            progress_callback_(evt);
        }
        return;
    }
    
    // Execute order through execution handler
    auto fill = execution_handler_->execute_order(order, current_data);
    fill.timestamp = current_data.timestamp;  // Use bar/simulation time, not wall-clock

    // Attach decision context to fill (regime, volatility at entry)
    fill.regime = current_bar_context_.regime;
    fill.volatility_pct = current_bar_context_.volatility_pct;
    fill.atr_at_entry = current_bar_context_.atr_at_bar;
    fill.filter_reason = current_bar_context_.filter_reason;

    // Process fill if quantity > 0
    if (fill.quantity > 0) {
        auto fill_event = std::make_unique<FillEvent>(fill);
        event_queue_.push(std::move(fill_event));
        logger_->debug("Order {} executed: {} {} @ ${:.2f}, commission=${:.2f}, slippage=${:.4f}", 
                      order.id, fill.quantity, 
                      fill.side == OrderSide::BUY ? "BUY" : "SELL",
                      fill.price, fill.commission, fill.slippage);
    } else {
        // Log failed fills for debugging - this should be rare but indicates an issue
        logger_->warn("Order {} failed to fill: {} {} shares requested @ ${:.2f}", 
                     event.order.id, event.order.quantity,
                     event.order.side == OrderSide::BUY ? "BUY" : "SELL",
                     event.order.price);
        if (progress_callback_) {
            nlohmann::json evt;
            evt["type"] = "event.order_rejected";
            evt["order_id"] = event.order.id;
            evt["symbol"] = event.order.symbol;
            evt["reason"] = "Execution handler returned zero quantity";
            progress_callback_(evt);
        }
    }
}

void BacktestEngine::process_fill_event(const FillEvent& event) {
    // Validate fill before processing
    if (event.fill.quantity <= 0 || event.fill.price <= 0.0) {
        logger_->warn("Invalid fill received: symbol={}, quantity={}, price={}", 
                     event.fill.symbol, event.fill.quantity, event.fill.price);
        return;
    }
    
    // Validate that the fill price is within the current bar's OHLC range.
    auto current_data = portfolio_manager_->get_current_market_data(event.fill.symbol);
    if (current_data.close > 0.0) {
        if (event.fill.price < current_data.low || event.fill.price > current_data.high) {
            logger_->warn("Fill price outside OHLC range: price={:.2f}, low={:.2f}, high={:.2f} - clamping",
                         event.fill.price, current_data.low, current_data.high);
            // Price validation is handled in ExecutionHandler::validate_execution_price
            // No need to clamp here as the fill price should already be validated
        }
    }
    
    // Get position before update to check if this is a new position
    auto position_before = portfolio_manager_->get_position(event.fill.symbol);
    bool was_new_position = (position_before.quantity == 0);

    // Update streak for adaptive sizing (when SELL closes a position)
    if (event.fill.side == OrderSide::SELL && position_before.quantity > 0) {
        Quantity sold_qty = std::min(position_before.quantity, event.fill.quantity);
        Price cost = position_before.avg_price * sold_qty;
        Price proceeds = event.fill.price * event.fill.quantity - event.fill.commission;
        Price pnl = proceeds - cost;
        if (pnl > 0) {
            consecutive_wins_++;
            consecutive_losses_ = 0;
        } else if (pnl < 0) {
            consecutive_losses_++;
            consecutive_wins_ = 0;
        }
    }
    
    // Update portfolio with fill
    portfolio_manager_->update_fill(event.fill);
    
    // Get position after update
    auto position_after = portfolio_manager_->get_position(event.fill.symbol);
    
    // Register stop-loss/take-profit tracking for newly opened positions.
    if (was_new_position && event.fill.side == OrderSide::BUY && position_after.quantity > 0) {
        // New long position opened - register entry price with risk manager for stop loss tracking
        // Calculate stop loss price from risk config
        Price entry_price = event.fill.price;
        Price stop_loss_price = entry_price * (1.0 - config_.stop_loss_percentage);
        
        // Update risk manager with entry price for stop loss and take profit tracking
        // Note: We need to cast to BasicRiskManager to access update_entry_price
        auto* basic_risk = dynamic_cast<BasicRiskManager*>(risk_manager_.get());
        if (basic_risk) {
            // Get take profit percentage from config or use default (15%)
            Price take_profit_pct = 0.15; // Default 15%
            // Try to get from risk config if available
            auto risk_config = portfolio_manager_->get_risk_config();
            if (risk_config.default_take_profit_pct > 0.0) {
                take_profit_pct = risk_config.default_take_profit_pct;
            }
            
            basic_risk->update_entry_price(event.fill.symbol, entry_price, take_profit_pct);
            Price take_profit_price = entry_price * (1.0 + take_profit_pct);
            logger_->debug("Risk management initialized for {}: entry=${:.2f}, stop=${:.2f}, take_profit=${:.2f}", 
                          event.fill.symbol, entry_price, stop_loss_price, take_profit_price);
        }
    }
    
    // Record trade for performance analysis
    performance_analyzer_->record_trade(event.fill);
    
    // Log executed fills for traceability during debugging/analysis.
    logger_->info("Fill executed: {} {} {} shares @ ${:.2f}, commission=${:.2f}, slippage=${:.4f}, total_cost=${:.2f}", 
                  event.fill.symbol, 
                  event.fill.side == OrderSide::BUY ? "BUY" : "SELL",
                  event.fill.quantity, event.fill.price, event.fill.commission, 
                  event.fill.slippage, event.fill.quantity * event.fill.price + event.fill.commission);
    
    if (progress_callback_) {
        nlohmann::json evt;
        evt["type"] = "event.fill";
        evt["symbol"] = event.fill.symbol;
        evt["quantity"] = event.fill.quantity;
        evt["side"] = (event.fill.side == OrderSide::BUY) ? "BUY" : "SELL";
        evt["price"] = event.fill.price;
        evt["commission"] = event.fill.commission;
        evt["slippage"] = event.fill.slippage;
        evt["total_cost"] = event.fill.quantity * event.fill.price + event.fill.commission;
        if (!event.fill.regime.empty()) evt["regime"] = event.fill.regime;
        if (event.fill.volatility_pct > 0.0) evt["volatility_pct"] = event.fill.volatility_pct;
        if (event.fill.atr_at_entry > 0.0) evt["atr_at_entry"] = event.fill.atr_at_entry;
        if (!event.fill.filter_reason.empty()) evt["filter_reason"] = event.fill.filter_reason;
        progress_callback_(evt);
    }
}

bool BacktestEngine::load_data() {
    try {
        // data_handler_ was created in initialize_components() with config_.data_interval,
        // so load_symbol_data() uses the handler's configured interval (e.g. 1d vs 1h).
        logger_->info("Starting data loading for {} symbol(s)", config_.symbols.size());
        for (const auto& symbol : config_.symbols) {
            logger_->info("Loading data for symbol: {} from {} to {}", symbol, config_.start_date, config_.end_date);
            
            if (progress_callback_) {
                nlohmann::json evt;
                evt["type"] = "data.loading";
                evt["symbol"] = symbol;
                progress_callback_(evt);
            }
            
            bool load_success = data_handler_->load_symbol_data(symbol, config_.start_date, config_.end_date);
            if (!load_success) {
                logger_->error("Failed to load data for symbol: {} (load_symbol_data returned false)", symbol);
                return false;
            }
            
            auto series = data_handler_->get_historical_data(symbol);
            logger_->info("Successfully loaded {} data points for symbol: {}", series.size(), symbol);
            
            if (progress_callback_) {
                nlohmann::json evt;
                evt["type"] = "data.symbol_loaded";
                evt["symbol"] = symbol;
                evt["points"] = static_cast<int>(series.size());
                progress_callback_(evt);
            }
            
            // Initialize strategy with the symbol's historical series before the run
            if (strategy_) {
                logger_->info("Initializing strategy with {} data points for symbol: {}", series.size(), symbol);
                strategy_->initialize(series);
            }
        }
        logger_->info("Data loading completed successfully for all symbols");
        return true;
    } catch (const std::exception& e) {
        logger_->error("Exception while loading data: {}", e.what());
        return false;
    }
}

void BacktestEngine::update_portfolio() {
    portfolio_manager_->update_portfolio(current_time_);
}

void BacktestEngine::check_stop_losses() {
    // Evaluate stop-loss and take-profit rules on each bar.
    // Use check_risk_orders which includes both stop losses and take profits
    auto* basic_risk = dynamic_cast<BasicRiskManager*>(risk_manager_.get());
    std::vector<Order> risk_orders;
    
    if (basic_risk) {
        risk_orders = basic_risk->check_risk_orders(*portfolio_manager_, current_time_);
    } else {
        // Fallback to just stop losses if not BasicRiskManager
        risk_orders = risk_manager_->check_stop_losses(*portfolio_manager_, current_time_);
    }
    
    if (!risk_orders.empty()) {
        logger_->info("Risk orders triggered: {} order(s) generated (stop loss/take profit)", risk_orders.size());
    }
    
    for (const auto& order : risk_orders) {
        logger_->info("Risk order: {} {} {} shares @ ${:.2f}", 
                     order.symbol, order.side == OrderSide::BUY ? "BUY" : "SELL",
                     order.quantity, order.price);
        auto order_event = std::make_unique<OrderEvent>(order);
        event_queue_.push(std::move(order_event));
    }
}

// BacktestConfig implementation
BacktestConfig BacktestConfig::load_from_yaml(const std::string& yaml_file) {
    YAML::Node config = YAML::LoadFile(yaml_file);
    
    BacktestConfig cfg;
    cfg.name = config["backtest"]["name"].as<std::string>();
    cfg.description = config["backtest"]["description"].as<std::string>("");
    cfg.symbols = config["backtest"]["symbols"].as<std::vector<std::string>>();
    cfg.start_date = config["backtest"]["start_date"].as<std::string>();
    cfg.end_date = config["backtest"]["end_date"].as<std::string>();
    cfg.initial_capital = config["portfolio"]["initial_cash"].as<double>();
    cfg.commission_rate = config["portfolio"]["commission"].as<double>();
    cfg.slippage_rate = config["execution"]["slippage_rate"].as<double>();
    cfg.data_source = config["data"]["source"].as<std::string>();
    cfg.data_path = config["data"]["path"].as<std::string>("");
    cfg.api_key = config["data"]["api_key"].as<std::string>("");
    cfg.output_path = config["output"]["path"].as<std::string>();
    cfg.verbose_logging = config["output"]["verbose_logging"].as<bool>(false);
    cfg.account_type = "CASH";
    cfg.market_type = "OTHER";
    cfg.strategy_name = config["strategy"]["name"].as<std::string>();
    
    // Load strategy parameters
    if (config["strategy"]["params"]) {
        for (const auto& param : config["strategy"]["params"]) {
            cfg.strategy_params[param.first.as<std::string>()] = param.second.as<double>();
        }
    }
    
    // Load risk parameters
    cfg.max_position_size = config["risk"]["max_position_size"].as<double>(1.0);
    cfg.stop_loss_percentage = config["risk"]["stop_loss"].as<double>(0.05);
    
    return cfg;
}

nlohmann::json BacktestConfig::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["description"] = description;
    j["symbols"] = symbols;
    j["start_date"] = start_date;
    j["end_date"] = end_date;
    j["initial_capital"] = initial_capital;
    j["commission_rate"] = commission_rate;
    j["slippage_rate"] = slippage_rate;
    j["data_source"] = data_source;
    if (!data_interval.empty()) {
        j["data_interval"] = data_interval;
    }
    if (!account_type.empty()) {
        j["account_type"] = account_type;
    }
    if (!market_type.empty()) {
        j["market_type"] = market_type;
    }
    j["strategy_name"] = strategy_name;
    j["strategy_params"] = strategy_params;
    if (!strategy_string_params.empty()) {
        j["strategy_string_params"] = strategy_string_params;
    }
    if (!strategy_definition_json.is_null() && !strategy_definition_json.empty()) {
        j["strategy_definition_json"] = strategy_definition_json;
    }
    return j;
}

// BacktestResult implementation
nlohmann::json BacktestResult::to_json() const {
    nlohmann::json j;
    
    // Metrics
    j["total_return"] = metrics.total_return;
    j["total_trades"] = metrics.total_trades;
    j["win_rate"] = metrics.win_rate;
    j["max_drawdown"] = metrics.max_drawdown;
    j["sharpe_ratio"] = metrics.sharpe_ratio;
    j["profit_factor"] = metrics.profit_factor;
    
    // ISO-style datetime formatter for trade timestamps (UTC)
    auto fmt_ts_datetime = [](Timestamp ts) {
        auto t = std::chrono::system_clock::to_time_t(ts);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    };

    // Trades (raw fills for compatibility), with decision context
    j["trades"] = nlohmann::json::array();
    for (const auto& trade : trade_history) {
        nlohmann::json trade_json;
        trade_json["date"] = fmt_ts_datetime(trade.timestamp);
        trade_json["symbol"] = trade.symbol;
        trade_json["side"] = (trade.side == OrderSide::BUY) ? "BUY" : "SELL";
        trade_json["quantity"] = trade.quantity;
        trade_json["price"] = trade.price;
        trade_json["commission"] = trade.commission;
        if (!trade.regime.empty()) trade_json["regime"] = trade.regime;
        if (trade.volatility_pct > 0.0) trade_json["volatility_pct"] = trade.volatility_pct;
        if (trade.atr_at_entry > 0.0) trade_json["atr_at_entry"] = trade.atr_at_entry;
        if (!trade.filter_reason.empty()) trade_json["filter_reason"] = trade.filter_reason;
        j["trades"].push_back(trade_json);
    }

    // Round-trip trades (pair BUY then SELL per symbol for entry/exit and P&L)
    j["round_trips"] = nlohmann::json::array();
    std::unordered_map<std::string, std::vector<Fill>> symbol_fills;
    for (const auto& f : trade_history) {
        symbol_fills[f.symbol].push_back(f);
    }
    for (const auto& [symbol, list] : symbol_fills) {
        // Sort fills by timestamp so round-trips are paired chronologically
        std::vector<Fill> sorted_list = list;
        std::sort(sorted_list.begin(), sorted_list.end(),
                  [](const Fill& a, const Fill& b) { return a.timestamp < b.timestamp; });
        std::vector<Fill> buys, sells;
        for (const auto& f : sorted_list) {
            if (f.side == OrderSide::BUY) buys.push_back(f);
            else sells.push_back(f);
        }
        size_t n = std::min(buys.size(), sells.size());
        for (size_t i = 0; i < n; ++i) {
            const Fill& buy = buys[i];
            const Fill& sell = sells[i];
            Quantity qty = std::min(buy.quantity, sell.quantity);
            Price entry_price = buy.price;
            Price exit_price = sell.price;
            Price pnl = (exit_price - entry_price) * qty - buy.commission - sell.commission;
            Price cost_basis = entry_price * qty;
            double pnl_pct = (cost_basis > 0.0) ? (pnl / cost_basis * 100.0) : 0.0;
            nlohmann::json rt;
            rt["id"] = std::to_string(static_cast<int>(j["round_trips"].size())) + "_" + symbol;
            rt["entryDate"] = fmt_ts_datetime(buy.timestamp);
            rt["exitDate"] = fmt_ts_datetime(sell.timestamp);
            rt["entryPrice"] = entry_price;
            rt["exitPrice"] = exit_price;
            rt["quantity"] = qty;
            rt["pnl"] = pnl;
            rt["pnlPercent"] = pnl_pct;
            rt["direction"] = "LONG";
            rt["symbol"] = symbol;
            if (!buy.regime.empty()) rt["regime"] = buy.regime;
            if (buy.volatility_pct > 0.0) rt["volatility_pct"] = buy.volatility_pct;
            if (buy.atr_at_entry > 0.0) rt["atr_at_entry"] = buy.atr_at_entry;
            if (!buy.filter_reason.empty()) rt["filter_reason"] = buy.filter_reason;
            j["round_trips"].push_back(rt);
        }
    }
    
    // Equity curve
    j["equity_curve"] = nlohmann::json::array();
    for (const auto& point : equity_curve) {
        nlohmann::json point_json;
        auto time_t = std::chrono::system_clock::to_time_t(point.first);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");
        point_json["timestamp"] = oss.str();
        point_json["value"] = point.second;
        j["equity_curve"].push_back(point_json);
    }
    
    return j;
}

bool BacktestResult::save_to_file(const std::string& filename) const {
    try {
        std::ofstream file(filename);
        file << std::setw(4) << to_json() << std::endl;
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void BacktestEngine::enable_logging(const std::string& log_file, spdlog::level::level_enum level) {
    if (!logger_) {
        try {
            logger_ = spdlog::stdout_color_mt("backtest");
        } catch (...) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            logger_ = std::make_shared<spdlog::logger>("backtest", console_sink);
            spdlog::register_logger(logger_);
        }
    }
    
    logger_->set_level(level);
    
    if (!log_file.empty()) {
        try {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
            logger_->sinks().push_back(file_sink);
            logger_->info("Logging enabled to file: {}", log_file);
        } catch (const std::exception& e) {
            logger_->warn("Failed to enable file logging: {}", e.what());
        }
    }
}

std::unique_ptr<BacktestEngine> BacktestEngine::create_from_config(const BacktestConfig& config) {
    auto engine = std::make_unique<BacktestEngine>();
    
    if (!config.validate()) {
        throw std::invalid_argument("Invalid backtest configuration");
    }
    
    if (!engine->configure(config)) {
        throw std::runtime_error("Failed to configure backtest engine");
    }
    
    return engine;
}

bool BacktestConfig::validate() const {
    if (name.empty()) {
        return false;
    }
    
    if (symbols.empty()) {
        return false;
    }
    
    if (start_date.empty() || end_date.empty()) {
        return false;
    }
    
    if (initial_capital <= 0) {
        return false;
    }
    
    if (commission_rate < 0 || slippage_rate < 0) {
        return false;
    }
    
    if (strategy_name.empty()) {
        return false;
    }

    // Default account_type and market_type if not provided so older callers remain valid
    // and downstream components can rely on non-empty strings.
    // CASH means fully-funded (no margin); MARGIN is reserved for future use.
    if (account_type.empty()) {
        const_cast<BacktestConfig*>(this)->account_type = "CASH";
    }
    if (market_type.empty()) {
        const_cast<BacktestConfig*>(this)->market_type = "OTHER";
    }

    return true;
}

} // namespace backtesting 