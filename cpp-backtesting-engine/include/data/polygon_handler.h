#pragma once

#include "data/data_handler.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace backtesting {

// Options data structure
struct OptionsData {
    std::string symbol;
    std::string expiry;
    double strike;
    std::string option_type; // "call" or "put"
    double bid;
    double ask;
    double last;
    double volume;
    double open_interest;
    double implied_volatility;
    double delta;
    double gamma;
    double theta;
    double vega;
};

// Polygon.io data handler (primarily for options)
class PolygonHandler : public DataHandler {
private:
    std::string api_key_;
    std::string base_url_;
    std::string cache_directory_;
    bool enable_disk_cache_;
    int cache_expiry_hours_;
    
    // Data storage
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, std::vector<OHLC>> symbol_data_;
    std::vector<OHLC> current_data_;
    size_t current_index_;
    
    // Options data storage
    std::unordered_map<std::string, std::vector<OptionsData>> options_data_;
    
    // Caching
    std::unordered_map<std::string, std::vector<OHLC>> data_cache_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> cache_timestamps_;
    mutable std::mutex cache_mutex_;
    
    // Statistics
    int successful_requests_ = 0;
    int failed_requests_ = 0;
    int cache_hits_ = 0;
    int cache_misses_ = 0;
    
    // Logging
    std::shared_ptr<spdlog::logger> logger_;
    
    // Rate limiting (Polygon free tier: 5 requests/minute)
    std::chrono::steady_clock::time_point last_request_time_;
    static constexpr int MIN_REQUEST_INTERVAL_MS = 12000; // 5 req/min = 12s between requests
    
    // Retry configuration
    int max_retry_attempts_ = 3;
    int retry_delay_seconds_ = 2;

public:
    PolygonHandler(
        const std::string& api_key = "",
        const std::string& cache_dir = "./polygon_cache",
        bool enable_disk_cache = true,
        int cache_expiry_hours = 24
    );
    
    ~PolygonHandler();
    
    // DataHandler interface implementation
    bool load_symbol_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) override;
    
    bool has_next() const override;
    OHLC get_next() override;
    void reset() override;
    std::vector<std::string> get_symbols() const override;
    std::vector<OHLC> get_historical_data(const std::string& symbol) const override;
    std::string get_source_name() const override { return "Polygon"; }
    
    // Options-specific methods
    bool load_options_chain(const std::string& symbol, const std::string& expiry_date = "");
    std::vector<OptionsData> get_options_chain(const std::string& symbol, const std::string& expiry_date = "") const;
    bool is_available() const { return !api_key_.empty(); }
    nlohmann::json get_api_usage_stats() const;
    
private:
    void initialize_logging();
    void apply_rate_limit();
    
    // HTTP request handling
    struct CurlResponse {
        std::string data;
        long response_code = 0;
        std::string error_message;
    };
    
    CurlResponse make_http_request(const std::string& url);
    CurlResponse make_http_request_with_retry(const std::string& url);
    static size_t write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response);
    
    // Polygon API methods
    std::string build_polygon_stocks_url(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) const;
    
    std::string build_polygon_options_url(
        const std::string& symbol,
        const std::string& expiry_date
    ) const;
    
    std::vector<OHLC> parse_polygon_stocks_response(const std::string& response, const std::string& symbol) const;
    std::vector<OptionsData> parse_polygon_options_response(const std::string& response) const;
    
    // Cache methods
    bool load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date);
    void save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                      const std::string& start_date, const std::string& end_date);
    bool is_cache_valid(const std::string& symbol) const;
    
    // Utility methods
    Timestamp parse_polygon_timestamp(const std::string& timestamp_str) const;
    std::string get_cache_filename(const std::string& symbol) const;
};

} // namespace backtesting

