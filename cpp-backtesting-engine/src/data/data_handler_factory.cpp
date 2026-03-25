#include "data/data_handler_factory.h"
#include "data/data_handler.h"
#include "data/alpha_vantage_handler.h"
#include "data/yfinance_handler.h"
#include "data/iex_handler.h"
#include "data/polygon_handler.h"
#include <stdexcept>
#include <spdlog/spdlog.h>

// DataInterval is defined in yfinance_handler.h
using backtesting::DataInterval;

namespace backtesting {

std::unique_ptr<DataHandler> DataHandlerFactory::create(
    const std::string& source_type,
    const std::string& data_path,
    const std::string& api_key
) {
    // Log parameters for observability; some may be unused today
    spdlog::info("DataHandlerFactory::create source_type={}, data_path={} (len={}), api_key_set={}",
                 source_type,
                 data_path,
                 data_path.size(),
                 !api_key.empty());
    if (source_type == "alpha_vantage" || source_type == "alphavantage") {
        if (api_key.empty()) {
            throw std::invalid_argument("Alpha Vantage API key is required");
        }
        return create_alpha_vantage_handler(api_key);
    } else if (source_type == "csv") {
        if (data_path.empty()) {
            throw std::invalid_argument("CSV data_path is required when using data_source=csv");
        }
        return std::make_unique<CSVDataHandler>(data_path);
    } else if (source_type == "yfinance" || source_type == "yahoo") {
        // Default to 1d interval if not specified
        DataInterval interval = DataInterval::ONE_DAY;
        
        // Default cache directory if not provided
        std::string cache_dir = data_path.empty() ? "./yfinance_cache" : data_path;
        // yfinance doesn't require API key, uses cache directory as data_path
        return std::make_unique<YFinanceHandler>(cache_dir, true, 24, interval);
    } else if (source_type == "iex") {
        // IEX Cloud - API key optional for free tier
        return std::make_unique<IEXHandler>(api_key, data_path.empty() ? "./iex_cache" : data_path, true, 24);
    } else if (source_type == "polygon") {
        // Polygon.io - requires API key
        if (api_key.empty()) {
            throw std::invalid_argument("Polygon API key is required");
        }
        return std::make_unique<PolygonHandler>(api_key, data_path.empty() ? "./polygon_cache" : data_path, true, 24);
    } else {
        throw std::invalid_argument("Unknown data source type: " + source_type);
    }
}

std::unique_ptr<DataHandler> DataHandlerFactory::create(
    const std::string& source_type,
    const std::string& data_path,
    const std::string& api_key,
    const std::string& data_interval
) {
    // Log parameters for observability
    spdlog::info("DataHandlerFactory::create source_type={}, data_path={} (len={}), api_key_set={}, interval={}",
                 source_type,
                 data_path,
                 data_path.size(),
                 !api_key.empty(),
                 data_interval);
    
    if (source_type == "alpha_vantage" || source_type == "alphavantage") {
        if (api_key.empty()) {
            throw std::invalid_argument("Alpha Vantage API key is required");
        }
        return create_alpha_vantage_handler(api_key);
    } else if (source_type == "csv") {
        if (data_path.empty()) {
            throw std::invalid_argument("CSV data_path is required when using data_source=csv");
        }
        return std::make_unique<CSVDataHandler>(data_path);
    } else if (source_type == "yfinance" || source_type == "yahoo") {
        // Convert string interval to DataInterval enum
        DataInterval interval = DataInterval::ONE_DAY; // default
        if (data_interval == "1m") interval = DataInterval::ONE_MINUTE;
        else if (data_interval == "5m") interval = DataInterval::FIVE_MINUTES;
        else if (data_interval == "15m") interval = DataInterval::FIFTEEN_MINUTES;
        else if (data_interval == "30m") interval = DataInterval::THIRTY_MINUTES;
        else if (data_interval == "1h") interval = DataInterval::ONE_HOUR;
        else if (data_interval == "1d") interval = DataInterval::ONE_DAY;
        
        // Default cache directory if not provided
        std::string cache_dir = data_path.empty() ? "./yfinance_cache" : data_path;
        return std::make_unique<YFinanceHandler>(cache_dir, true, 24, interval);
    } else if (source_type == "iex") {
        // IEX Cloud - API key optional for free tier
        return std::make_unique<IEXHandler>(api_key, data_path.empty() ? "./iex_cache" : data_path, true, 24);
    } else if (source_type == "polygon") {
        // Polygon.io - requires API key
        if (api_key.empty()) {
            throw std::invalid_argument("Polygon API key is required");
        }
        return std::make_unique<PolygonHandler>(api_key, data_path.empty() ? "./polygon_cache" : data_path, true, 24);
    } else {
        throw std::invalid_argument("Unknown data source type: " + source_type);
    }
}

std::vector<std::string> DataHandlerFactory::get_available_sources() {
    return {"csv", "alpha_vantage", "alphavantage", "yfinance", "yahoo", "iex", "polygon"};
}

std::unique_ptr<DataHandler> DataHandlerFactory::create_alpha_vantage_handler(const std::string& api_key) {
    return std::make_unique<AlphaVantageHandler>(api_key);
}

std::unique_ptr<DataHandler> DataHandlerFactory::create_database_handler(const std::string& connection_string) {
    // For future implementation
    (void)connection_string; // Suppress unused parameter warning
    throw std::runtime_error("Database handler not implemented yet");
}

} // namespace backtesting 