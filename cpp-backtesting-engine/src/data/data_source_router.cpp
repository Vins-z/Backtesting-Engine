#include "data/data_source_router.h"
#include "data/data_handler.h" // For CSVDataHandler
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>

namespace backtesting {

DataSourceRouter::DataSourceRouter(
    const std::string& yfinance_cache,
    const std::string& iex_api_key,
    const std::string& iex_cache,
    const std::string& polygon_api_key,
    const std::string& polygon_cache,
    const std::string& alpha_vantage_api_key,
    const std::string& csv_data_path
) : yfinance_cache_dir_(yfinance_cache),
    iex_api_key_(iex_api_key),
    iex_cache_dir_(iex_cache),
    polygon_api_key_(polygon_api_key),
    polygon_cache_dir_(polygon_cache),
    alpha_vantage_api_key_(alpha_vantage_api_key),
    csv_data_path_(csv_data_path) {
    
    initialize_logging();
    initialize_handlers();
}

void DataSourceRouter::initialize_logging() {
    try {
        logger_ = spdlog::get("data_router");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("data_router");
        }
    } catch (...) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("data_router", console_sink);
        spdlog::register_logger(logger_);
    }
}

void DataSourceRouter::initialize_handlers() {
    // Initialize YFinance (always available, no API key needed)
    try {
        yfinance_handler_ = std::make_unique<YFinanceHandler>(yfinance_cache_dir_, true, 24);
        logger_->info("YFinance handler initialized");
    } catch (const std::exception& e) {
        logger_->warn("Failed to initialize YFinance handler: {}", e.what());
    }
    
    // Initialize IEX (free tier doesn't require API key for some endpoints)
    try {
        iex_handler_ = std::make_unique<IEXHandler>(iex_api_key_, iex_cache_dir_, true, 24);
        logger_->info("IEX handler initialized (API key: {})", iex_api_key_.empty() ? "not set" : "set");
    } catch (const std::exception& e) {
        logger_->warn("Failed to initialize IEX handler: {}", e.what());
    }
    
    // Initialize Polygon (requires API key)
    if (!polygon_api_key_.empty()) {
        try {
            polygon_handler_ = std::make_unique<PolygonHandler>(polygon_api_key_, polygon_cache_dir_, true, 24);
            logger_->info("Polygon handler initialized");
        } catch (const std::exception& e) {
            logger_->warn("Failed to initialize Polygon handler: {}", e.what());
        }
    } else {
        logger_->info("Polygon handler not initialized (no API key)");
    }
    
    // Initialize Alpha Vantage (requires API key)
    if (!alpha_vantage_api_key_.empty()) {
        try {
            alpha_vantage_handler_ = std::make_unique<AlphaVantageHandler>(alpha_vantage_api_key_);
            logger_->info("Alpha Vantage handler initialized");
        } catch (const std::exception& e) {
            logger_->warn("Failed to initialize Alpha Vantage handler: {}", e.what());
        }
    } else {
        logger_->info("Alpha Vantage handler not initialized (no API key)");
    }
}

std::unique_ptr<DataHandler> DataSourceRouter::get_data_handler(
    const std::string& symbol,
    DataType data_type,
    const std::string& preferred_source
) {
    // Check cache first
    auto cached = check_cache(symbol, data_type);
    if (cached) {
        return cached;
    }
    
    // Select appropriate source
    std::string source = select_source(symbol, data_type, preferred_source);
    
    // Create handler for selected source
    auto handler = create_handler(source);
    
    if (handler) {
        source_usage_count_[source]++;
        logger_->debug("Selected data source '{}' for symbol '{}' (type: {})", 
                      source, symbol, static_cast<int>(data_type));
    } else {
        logger_->warn("Failed to create handler for source '{}'", source);
    }
    
    return handler;
}

std::string DataSourceRouter::select_source(
    const std::string& /* symbol */,
    DataType data_type,
    const std::string& preferred_source
) const {
    // If preferred source is specified and available, use it
    if (!preferred_source.empty() && is_source_available(preferred_source)) {
        return preferred_source;
    }
    
    // Route based on data type
    switch (data_type) {
        case DataType::OPTIONS:
            // Options: Polygon (if available) > YFinance
            if (polygon_handler_ && polygon_handler_->is_available()) {
                return "polygon";
            }
            if (yfinance_handler_) {
                return "yfinance";
            }
            break;
            
        case DataType::STOCK:
            // Stocks: YFinance (free) > IEX (free) > Alpha Vantage
            if (yfinance_handler_) {
                return "yfinance";
            }
            if (iex_handler_ && iex_handler_->is_available()) {
                return "iex";
            }
            if (alpha_vantage_handler_) {
                return "alpha_vantage";
            }
            if (!csv_data_path_.empty()) {
                return "csv";
            }
            break;
            
        case DataType::FUTURES:
        case DataType::FOREX:
        case DataType::CRYPTO:
            // For now, use YFinance as fallback
            if (yfinance_handler_) {
                return "yfinance";
            }
            break;
    }
    
    // Default fallback
    return "yfinance";
}

std::unique_ptr<DataHandler> DataSourceRouter::create_handler(const std::string& source_name) {
    // Check if we have a cached handler
    auto it = handler_cache_.find(source_name);
    if (it != handler_cache_.end() && it->second) {
        // Return a copy/clone if needed, or reuse
        // For now, we'll create new instances
    }
    
    if (source_name == "yfinance" || source_name == "yahoo") {
        if (yfinance_handler_) {
            // Return a new instance or clone
            return std::make_unique<YFinanceHandler>(yfinance_cache_dir_, true, 24);
        }
    } else if (source_name == "iex") {
        if (iex_handler_ && iex_handler_->is_available()) {
            return std::make_unique<IEXHandler>(iex_api_key_, iex_cache_dir_, true, 24);
        }
    } else if (source_name == "polygon") {
        if (polygon_handler_ && polygon_handler_->is_available()) {
            return std::make_unique<PolygonHandler>(polygon_api_key_, polygon_cache_dir_, true, 24);
        }
    } else if (source_name == "alpha_vantage" || source_name == "alphavantage") {
        if (alpha_vantage_handler_) {
            return std::make_unique<AlphaVantageHandler>(alpha_vantage_api_key_);
        }
    } else if (source_name == "csv") {
        if (!csv_data_path_.empty()) {
            return std::make_unique<CSVDataHandler>(csv_data_path_);
        }
    }
    
    return nullptr;
}

std::unique_ptr<DataHandler> DataSourceRouter::check_cache(
    const std::string& /* symbol */,
    DataType /* data_type */
) {
    // For now, we don't maintain a cross-source cache
    // Each handler manages its own cache
    // This could be enhanced to check all handler caches
    return nullptr;
}

bool DataSourceRouter::load_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date,
    DataType data_type
) {
    auto handler = get_data_handler(symbol, data_type);
    if (!handler) {
        logger_->error("No available data handler for symbol: {}", symbol);
        return false;
    }
    
    return handler->load_symbol_data(symbol, start_date, end_date);
}

std::vector<struct OptionsData> DataSourceRouter::get_options_chain(
    const std::string& symbol,
    const std::string& expiry_date
) {
    // Try Polygon first (best for options)
    if (polygon_handler_ && polygon_handler_->is_available()) {
        if (polygon_handler_->load_options_chain(symbol, expiry_date)) {
            auto options = polygon_handler_->get_options_chain(symbol, expiry_date);
            std::vector<struct OptionsData> result;
            for (const auto& opt : options) {
                result.push_back(opt);
            }
            return result;
        }
    }
    
    // Fallback: return empty (YFinance options support would go here)
    logger_->warn("Options chain not available for symbol: {}", symbol);
    return {};
}

bool DataSourceRouter::is_source_available(const std::string& source_name) const {
    if (source_name == "yfinance" || source_name == "yahoo") {
        return yfinance_handler_ != nullptr;
    } else if (source_name == "iex") {
        return iex_handler_ != nullptr && iex_handler_->is_available();
    } else if (source_name == "polygon") {
        return polygon_handler_ != nullptr && polygon_handler_->is_available();
    } else if (source_name == "alpha_vantage" || source_name == "alphavantage") {
        return alpha_vantage_handler_ != nullptr;
    } else if (source_name == "csv") {
        return !csv_data_path_.empty();
    }
    
    return false;
}

std::string DataSourceRouter::get_recommended_source(DataType data_type) const {
    switch (data_type) {
        case DataType::OPTIONS:
            if (polygon_handler_ && polygon_handler_->is_available()) {
                return "polygon";
            }
            return "yfinance";
            
        case DataType::STOCK:
            if (yfinance_handler_) {
                return "yfinance";
            }
            if (iex_handler_ && iex_handler_->is_available()) {
                return "iex";
            }
            return "alpha_vantage";
            
        default:
            return "yfinance";
    }
}

nlohmann::json DataSourceRouter::get_usage_stats() const {
    nlohmann::json stats;
    stats["source_usage"] = source_usage_count_;
    
    // Add handler-specific stats
    if (yfinance_handler_) {
        stats["yfinance"] = yfinance_handler_->get_api_usage_stats();
    }
    if (iex_handler_) {
        stats["iex"] = iex_handler_->get_api_usage_stats();
    }
    if (polygon_handler_) {
        stats["polygon"] = polygon_handler_->get_api_usage_stats();
    }
    
    return stats;
}

} // namespace backtesting

