#pragma once

#include "data_handler.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>

namespace backtesting {

// Data quality metrics structure
struct DataQualityMetrics {
    double completeness_ratio = 0.0;
    double validity_ratio = 0.0;
    int missing_values = 0;
    int outliers = 0;
    bool has_gaps = false;
    std::string quality_grade = "Unknown";
};

// Enhanced cache entry with metadata
struct CacheEntry {
    std::vector<OHLC> data;
    std::chrono::system_clock::time_point cached_at;
    std::string start_date;
    std::string end_date;
    DataQualityMetrics quality;
    bool is_valid = true;
};

class AlphaVantageHandler : public DataHandler {
private:
    std::string api_key_;
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, std::vector<OHLC>> symbol_data_;
    std::vector<OHLC> current_data_;
    size_t current_index_;
    
    // Enhanced caching system
    std::unordered_map<std::string, CacheEntry> data_cache_;
    std::string cache_directory_;
    std::chrono::hours cache_expiry_hours_;
    bool enable_disk_cache_;
    mutable std::mutex cache_mutex_;
    
    // Logging
    std::shared_ptr<spdlog::logger> logger_;
    
    // CURL response structure
    struct CurlResponse {
        std::string data;
        long response_code = 0;
        std::string error_message;
    };
    
public:
    explicit AlphaVantageHandler(const std::string& api_key, 
                                 const std::string& cache_dir = "./cache",
                                 bool enable_disk_cache = true,
                                 int cache_expiry_hours = 24);
    ~AlphaVantageHandler();
    
    // Base DataHandler interface
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
    std::string get_source_name() const override { return "Alpha Vantage Enhanced"; }
    
    // Enhanced Alpha Vantage specific methods
    bool fetch_daily_data(const std::string& symbol, bool force_refresh = false);
    bool fetch_intraday_data(const std::string& symbol, const std::string& interval = "5min", bool force_refresh = false);
    bool fetch_adjusted_data(const std::string& symbol, bool force_refresh = false);
    // Premium-only: Intraday extended slices (month chunks), then resample to daily
    bool fetch_intraday_extended_and_resample_daily(const std::string& symbol,
                                                    const std::string& interval,
                                                    const std::string& start_date,
                                                    const std::string& end_date);
    
    // Batch operations
    bool load_multiple_symbols(const std::vector<std::string>& symbols,
                              const std::string& start_date,
                              const std::string& end_date);
    
    // Cache management
    void clear_cache();
    void clear_symbol_cache(const std::string& symbol);
    bool is_data_cached(const std::string& symbol, const std::string& start_date, const std::string& end_date) const;
    void set_cache_expiry(int hours) { cache_expiry_hours_ = std::chrono::hours(hours); }
    
    // Data validation and quality
    DataQualityMetrics validate_data_quality(const std::vector<OHLC>& data) const;
    std::vector<OHLC> clean_and_validate_data(const std::vector<OHLC>& raw_data) const;
    bool detect_data_gaps(const std::vector<OHLC>& data, int max_gap_days = 7) const;
    
    // Statistics and monitoring
    nlohmann::json get_api_usage_stats() const;
    nlohmann::json get_cache_statistics() const;
    void enable_detailed_logging(bool enable = true);
    
    // Configuration
    void set_api_key(const std::string& api_key) { api_key_ = api_key; }
    void set_timeout(int seconds) { request_timeout_seconds_ = seconds; }
    void set_retry_attempts(int attempts) { max_retry_attempts_ = attempts; }
    
private:
    // HTTP request methods with retry logic
    CurlResponse make_http_request_with_retry(const std::string& url);
    CurlResponse make_http_request(const std::string& url);
    static size_t write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response);
    
    // URL builders
    std::string build_daily_url(const std::string& symbol) const;
    std::string build_intraday_url(const std::string& symbol, const std::string& interval) const;
    std::string build_adjusted_url(const std::string& symbol) const;
    std::string build_quote_url(const std::string& symbol) const;
    std::string build_intraday_extended_url(const std::string& symbol,
                                            const std::string& interval,
                                            int year, int month) const;
    
    // JSON parsing with validation
    std::vector<OHLC> parse_daily_response(const std::string& json_response, const std::string& symbol);
    std::vector<OHLC> parse_intraday_response(const std::string& json_response, const std::string& symbol);
    std::vector<OHLC> parse_adjusted_response(const std::string& json_response, const std::string& symbol);
    bool validate_json_response(const nlohmann::json& json, std::string& error_message) const;
    // CSV parser for intraday extended slices
    std::vector<OHLC> parse_intraday_extended_csv(const std::string& csv,
                                                  const std::string& symbol) const;
    // Resample intraday bars to daily OHLC
    std::vector<OHLC> resample_to_daily(const std::vector<OHLC>& intraday) const;
    
    // Caching system
    bool load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date);
    void save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                      const std::string& start_date, const std::string& end_date);
    std::string get_cache_filename(const std::string& symbol, const std::string& start_date, const std::string& end_date) const;
    bool is_cache_valid(const CacheEntry& entry, const std::string& start_date, const std::string& end_date) const;
    void cleanup_expired_cache();
    
    // Data validation and cleaning
    bool validate_ohlc_data(const OHLC& data) const;
    std::vector<OHLC> remove_outliers(const std::vector<OHLC>& data, double z_threshold = 3.0) const;
    void fill_missing_data(std::vector<OHLC>& data) const;
    
    // Utility methods
    Timestamp parse_alpha_vantage_timestamp(const std::string& timestamp_str) const;
    void filter_data_by_date_range(
        std::vector<OHLC>& data,
        const std::string& start_date,
        const std::string& end_date
    );
    
    // Enhanced rate limiting
    void apply_rate_limit();
    bool check_rate_limit_status() const;
    void reset_rate_limit_counters();
    
    // Request tracking and statistics
    mutable std::chrono::steady_clock::time_point last_request_time_;
    mutable std::vector<std::chrono::steady_clock::time_point> request_history_;
    mutable int successful_requests_ = 0;
    mutable int failed_requests_ = 0;
    mutable int cache_hits_ = 0;
    mutable int cache_misses_ = 0;
    
    // Configuration parameters
    int request_timeout_seconds_ = 120; // Increased for Alpha Vantage
    int max_retry_attempts_ = 5; // More retries
    int retry_delay_seconds_ = 10; // Longer delay between retries
    bool detailed_logging_ = false;
    
    // Rate limiting constants
    static constexpr int MAX_REQUESTS_PER_HOUR = 300; // 5 per minute
    static constexpr int MIN_REQUEST_INTERVAL_MS = 12000; // ~12 seconds per docs
    static constexpr int MAX_REQUESTS_PER_DAY = 500; // Daily limit per docs
};

} // namespace backtesting 