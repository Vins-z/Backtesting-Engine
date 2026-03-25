#pragma once

#include "data/data_handler.h"
#include "data/yfinance_handler.h"
#include "data/iex_handler.h"
#include "data/polygon_handler.h"
#include "data/alpha_vantage_handler.h"
#include "data/data_handler.h" // For CSVDataHandler
#include "common/types.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

// OptionsData is defined in polygon_handler.h

namespace backtesting {

// Data type enumeration
enum class DataType {
    STOCK,
    OPTIONS,
    FUTURES,
    FOREX,
    CRYPTO
};

// Data source router - intelligently routes requests to appropriate data sources
class DataSourceRouter {
private:
    // Available handlers
    std::unique_ptr<YFinanceHandler> yfinance_handler_;
    std::unique_ptr<IEXHandler> iex_handler_;
    std::unique_ptr<PolygonHandler> polygon_handler_;
    std::unique_ptr<AlphaVantageHandler> alpha_vantage_handler_;
    
    // Configuration
    std::string yfinance_cache_dir_;
    std::string iex_api_key_;
    std::string iex_cache_dir_;
    std::string polygon_api_key_;
    std::string polygon_cache_dir_;
    std::string alpha_vantage_api_key_;
    std::string csv_data_path_;
    
    // Cache for handlers
    std::unordered_map<std::string, std::unique_ptr<DataHandler>> handler_cache_;
    
    // Statistics
    std::unordered_map<std::string, int> source_usage_count_;
    
    // Logging
    std::shared_ptr<spdlog::logger> logger_;
    
    // Initialize handlers
    void initialize_handlers();
    void initialize_logging();

public:
    DataSourceRouter(
        const std::string& yfinance_cache = "./yfinance_cache",
        const std::string& iex_api_key = "",
        const std::string& iex_cache = "./iex_cache",
        const std::string& polygon_api_key = "",
        const std::string& polygon_cache = "./polygon_cache",
        const std::string& alpha_vantage_api_key = "",
        const std::string& csv_data_path = ""
    );
    
    ~DataSourceRouter() = default;
    
    // Main routing method
    std::unique_ptr<DataHandler> get_data_handler(
        const std::string& symbol,
        DataType data_type = DataType::STOCK,
        const std::string& preferred_source = ""
    );
    
    // Load data with automatic source selection
    bool load_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date,
        DataType data_type = DataType::STOCK
    );
    
    // Get options chain (uses Polygon if available, otherwise YFinance)
    // Note: OptionsData is defined in polygon_handler.h
    std::vector<struct OptionsData> get_options_chain(
        const std::string& symbol,
        const std::string& expiry_date = ""
    );
    
    // Check source availability
    bool is_source_available(const std::string& source_name) const;
    
    // Get usage statistics
    nlohmann::json get_usage_stats() const;
    
    // Get recommended source for data type
    std::string get_recommended_source(DataType data_type) const;
    
private:
    // Source selection logic
    std::string select_source(
        const std::string& symbol,
        DataType data_type,
        const std::string& preferred_source
    ) const;
    
    // Create handler for specific source
    std::unique_ptr<DataHandler> create_handler(const std::string& source_name);
    
    // Check cache first
    std::unique_ptr<DataHandler> check_cache(const std::string& symbol, DataType data_type);
};

} // namespace backtesting

