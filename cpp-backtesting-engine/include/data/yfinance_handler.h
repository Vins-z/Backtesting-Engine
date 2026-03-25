#pragma once

#include "data/data_handler.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace backtesting {

// Data quality metrics for validation
struct YFinanceDataQualityMetrics {
    double completeness_ratio = 0.0;
    double validity_ratio = 0.0;
    int missing_values = 0;
    int outliers = 0;
    bool has_gaps = false;
    std::string quality_grade = "Unknown";
};

// Supported data intervals
enum class DataInterval {
    ONE_MINUTE = 1,      // 1m
    FIVE_MINUTES = 5,    // 5m
    FIFTEEN_MINUTES = 15, // 15m
    THIRTY_MINUTES = 30,  // 30m
    ONE_HOUR = 60,       // 1h
    ONE_DAY = 1440       // 1d
};

// Cache entry for storing data with metadata
struct YFinanceCacheEntry {
    std::vector<OHLC> data;
    std::chrono::system_clock::time_point cached_at;
    std::string start_date;
    std::string end_date;
    DataInterval interval;
    YFinanceDataQualityMetrics quality;
    bool is_valid = true;
};

// yfinance-based data handler with comprehensive features
class YFinanceHandler : public DataHandler {
private:
    std::string cache_directory_;
    std::string base_url_;
    bool enable_disk_cache_;
    int cache_expiry_hours_;
    bool detailed_logging_;
    
    // Data storage
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, std::vector<OHLC>> symbol_data_;
    std::vector<OHLC> current_data_;
    size_t current_index_;
    DataInterval current_interval_;
    
    // Caching
    std::unordered_map<std::string, YFinanceCacheEntry> data_cache_;
    mutable std::mutex cache_mutex_;
    
    // Statistics
    int successful_requests_ = 0;
    int failed_requests_ = 0;
    int cache_hits_ = 0;
    int cache_misses_ = 0;
    
    // Logging
    std::shared_ptr<spdlog::logger> logger_;
    
    // Rate limiting
    std::vector<std::chrono::steady_clock::time_point> request_history_;
    std::chrono::steady_clock::time_point last_request_time_;
    static constexpr int MAX_REQUESTS_PER_HOUR = 2000; // yfinance is more generous
    static constexpr int MIN_REQUEST_INTERVAL_MS = 100; // 100ms between requests
    
    // Retry configuration
    int max_retry_attempts_ = 3;
    int retry_delay_seconds_ = 2;

public:
    YFinanceHandler(
        const std::string& cache_dir = "./cache",
        bool enable_disk_cache = true,
        int cache_expiry_hours = 24,
        DataInterval interval = DataInterval::ONE_DAY
    );
    
    ~YFinanceHandler();
    
    // DataHandler interface implementation
    bool load_symbol_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) override;
    
    // Enhanced data loading with interval support
    bool load_symbol_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date,
        DataInterval interval
    );
    
    bool has_next() const override;
    OHLC get_next() override;
    void reset() override;
    std::vector<std::string> get_symbols() const override;
    std::vector<OHLC> get_historical_data(const std::string& symbol) const override;
    std::string get_source_name() const override { return "YFinance"; }
    
    // Enhanced features
    bool load_multiple_symbols(
        const std::vector<std::string>& symbols,
        const std::string& start_date,
        const std::string& end_date
    );
    
    // Data quality and validation
    YFinanceDataQualityMetrics validate_data_quality(const std::vector<OHLC>& data) const;
    std::vector<OHLC> clean_and_validate_data(const std::vector<OHLC>& raw_data) const;
    bool validate_ohlc_data(const OHLC& data) const;
    
    // Caching methods
    bool load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date);
    bool load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date, DataInterval interval);
    void save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                      const std::string& start_date, const std::string& end_date);
    void save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                      const std::string& start_date, const std::string& end_date, DataInterval interval);
    void cleanup_expired_cache();
    
    // Statistics and monitoring
    nlohmann::json get_api_usage_stats() const;
    nlohmann::json get_cache_statistics() const;
    void enable_detailed_logging(bool enable);
    
    // Data processing utilities
    std::vector<OHLC> remove_outliers(const std::vector<OHLC>& data, double z_threshold = 3.0) const;
    void fill_missing_data(std::vector<OHLC>& data) const;
    bool detect_data_gaps(const std::vector<OHLC>& data, int max_gap_days = 5) const;
    
    // Date filtering
    void filter_data_by_date_range(
        std::vector<OHLC>& data,
        const std::string& start_date,
        const std::string& end_date
    );
    
    // Rate limiting
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
    
    // yfinance API methods
    std::string build_yfinance_url(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) const;
    
    std::string build_yfinance_url(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date,
        DataInterval interval
    ) const;
    
    std::vector<OHLC> parse_yfinance_response(const std::string& response, const std::string& symbol) const;
    
    // Utility methods
    std::string get_cache_filename(const std::string& symbol, const std::string& start_date, const std::string& end_date) const;
    std::string get_cache_filename(const std::string& symbol, const std::string& start_date, const std::string& end_date, DataInterval interval) const;
    bool is_cache_valid(const YFinanceCacheEntry& entry, const std::string& start_date, const std::string& end_date) const;
    bool is_cache_valid(const YFinanceCacheEntry& entry, const std::string& start_date, const std::string& end_date, DataInterval interval) const;
    Timestamp parse_yfinance_timestamp(const std::string& timestamp_str) const;
    
    // Interval support methods
    std::string interval_to_yfinance_string(DataInterval interval) const;
    DataInterval string_to_interval(const std::string& interval_str) const;
    std::string get_interval_name(DataInterval interval) const;
    
    // Data validation helpers
    bool validate_json_response(const nlohmann::json& json, std::string& error_message) const;
    
    // Symbol normalization (aligned with yahoo-finance2 API)
    // Normalizes symbols for US and Indian markets (NSE/BSE)
    std::string normalize_symbol(const std::string& symbol) const;
    bool is_valid_symbol(const std::string& symbol) const;
    bool is_indian_market_symbol(const std::string& symbol) const;
    bool is_us_market_symbol(const std::string& symbol) const;
    
    // Quote fetching (real-time/delayed quotes)
    struct QuoteData {
        std::string symbol;
        double price = 0.0;
        double change = 0.0;
        double change_percent = 0.0;
        long long volume = 0;
        double high = 0.0;
        double low = 0.0;
        double open = 0.0;
        double previous_close = 0.0;
        std::string currency;
        std::string exchange;
        std::string name;
        std::string sector;
    };
    
    bool fetch_quote(const std::string& symbol, QuoteData& quote);
    bool fetch_quotes(const std::vector<std::string>& symbols, std::vector<QuoteData>& quotes);
    
    // Improved data validation
    double safe_to_double(const nlohmann::json& value, double fallback = 0.0) const;
    long long safe_to_int64(const nlohmann::json& value, long long fallback = 0) const;
    
private:
    void initialize_logging();
    void create_cache_directory();
};

} // namespace backtesting
