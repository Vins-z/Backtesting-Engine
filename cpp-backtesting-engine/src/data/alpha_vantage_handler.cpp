#include "data/alpha_vantage_handler.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace backtesting {

AlphaVantageHandler::AlphaVantageHandler(
    const std::string& api_key,
    const std::string& cache_dir,
    bool enable_disk_cache,
    int cache_expiry_hours
) : api_key_(api_key), 
    current_index_(0),
    cache_directory_(cache_dir),
    cache_expiry_hours_(cache_expiry_hours),
    enable_disk_cache_(enable_disk_cache),
    last_request_time_(std::chrono::steady_clock::now() - std::chrono::milliseconds(MIN_REQUEST_INTERVAL_MS)) {
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Setup logging
    try {
        logger_ = spdlog::get("alpha_vantage");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("alpha_vantage");
        }
    } catch (...) {
        // Fallback to basic stdout logger if colored logger fails
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("alpha_vantage", console_sink);
        spdlog::register_logger(logger_);
    }
    
    // Create cache directory if it doesn't exist
    if (enable_disk_cache_) {
        try {
            std::filesystem::create_directories(cache_directory_);
            logger_->info("Cache directory initialized: {}", cache_directory_);
        } catch (const std::exception& e) {
            logger_->warn("Failed to create cache directory: {}", e.what());
            enable_disk_cache_ = false;
        }
    }
    
    // Clean up expired cache entries on startup
    if (enable_disk_cache_) {
        cleanup_expired_cache();
    }
    
    logger_->info("AlphaVantage handler initialized with API key: {}...", 
                  api_key_.substr(0, 8));
}

AlphaVantageHandler::~AlphaVantageHandler() {
    curl_global_cleanup();
}

bool AlphaVantageHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) {
    try {
        logger_->info("Loading data for symbol: {} from {} to {}", symbol, start_date, end_date);
        
        // Check cache first
        if (load_from_cache(symbol, start_date, end_date)) {
            logger_->info("Data loaded from cache for {}", symbol);
            cache_hits_++;
            return true;
        }
        
        cache_misses_++;
        
        // Apply rate limiting before making API request
        apply_rate_limit();
        
        // Fetch data from Alpha Vantage API (use adjusted daily per docs)
        std::string url = build_adjusted_url(symbol);
        logger_->info("Fetching data from Alpha Vantage for {}: {}", symbol, url);
        auto response = make_http_request_with_retry(url);
        
        if (response.data.empty() || response.response_code != 200) {
            logger_->error("Failed to fetch data for symbol: {} (HTTP {}) - Response: {}", symbol, response.response_code, response.data.substr(0, 200));
            failed_requests_++;
            return false;
        }
        
        logger_->info("Successfully received {} bytes from Alpha Vantage for {}", response.data.size(), symbol);
        
        // Parse the JSON response (adjusted daily includes adjusted close, split/dividend)
        std::vector<OHLC> raw_data = parse_adjusted_response(response.data, symbol);
        
        if (raw_data.empty()) {
            // Check for rate limit note and backoff
            try {
                nlohmann::json j = nlohmann::json::parse(response.data);
                if (j.contains("Note")) {
                    std::string note = j["Note"].get<std::string>();
                    logger_->warn("Alpha Vantage rate limit note received: {}. Backing off and retrying...", note);
                    std::this_thread::sleep_for(std::chrono::seconds(30)); // Longer wait
                    apply_rate_limit();
                    logger_->info("Retrying Alpha Vantage request for {} after rate limit backoff", symbol);
                    auto retry_resp = make_http_request_with_retry(build_adjusted_url(symbol));
                    if (retry_resp.response_code == 200 && !retry_resp.data.empty()) {
                        logger_->info("Retry successful, parsing {} bytes for {}", retry_resp.data.size(), symbol);
                        raw_data = parse_adjusted_response(retry_resp.data, symbol);
                    } else {
                        logger_->error("Retry failed for {}: HTTP {}", symbol, retry_resp.response_code);
                    }
                } else if (j.contains("Error Message")) {
                    std::string error = j["Error Message"].get<std::string>();
                    logger_->error("Alpha Vantage API error for {}: {}", symbol, error);
                } else if (j.contains("Information")) {
                    std::string info = j["Information"].get<std::string>();
                    logger_->warn("Alpha Vantage info for {}: {}", symbol, info);
                }
            } catch (const std::exception& e) {
                logger_->error("Failed to parse Alpha Vantage response for {}: {}", symbol, e.what());
            }
        }

        if (raw_data.empty()) {
            logger_->error("No data parsed for symbol: {} after all retries", symbol);
            failed_requests_++;
            return false;
        }
        
        logger_->info("Successfully parsed {} data points for {}", raw_data.size(), symbol);
        
        // Clean and validate the data
        std::vector<OHLC> cleaned_data = clean_and_validate_data(raw_data);

        // Keep a copy before filtering so we can fallback if needed
        std::vector<OHLC> pre_filter_data = cleaned_data;
        
        // Normalize date range: if start > end, swap
        if (!start_date.empty() && !end_date.empty()) {
            auto start_tp = parse_alpha_vantage_timestamp(start_date);
            auto end_tp = parse_alpha_vantage_timestamp(end_date);
            if (start_tp > end_tp) {
                logger_->warn("Start date {} is after end date {}. Swapping.", start_date, end_date);
                filter_data_by_date_range(cleaned_data, end_date, start_date);
            } else {
                filter_data_by_date_range(cleaned_data, start_date, end_date);
            }
        } else {
            filter_data_by_date_range(cleaned_data, start_date, end_date);
        }
        
        if (cleaned_data.empty()) {
            // Fallback: use available data if requested window has no overlap with available range
            if (!pre_filter_data.empty()) {
                auto first_ts = pre_filter_data.front().timestamp;
                auto last_ts = pre_filter_data.back().timestamp;
                logger_->warn(
                    "No data in requested range [{}..{}] for {}. Using available range [{}..{}] instead.",
                    start_date, end_date, symbol,
                    std::chrono::system_clock::to_time_t(first_ts),
                    std::chrono::system_clock::to_time_t(last_ts)
                );
                cleaned_data = pre_filter_data;
            } else {
                logger_->warn("No data available for {} after cleaning", symbol);
                return false;
            }
        }
        
        // Validate data quality
        DataQualityMetrics quality = validate_data_quality(cleaned_data);
        logger_->info("Data quality for {}: {} (completeness: {:.1f}%, validity: {:.1f}%)", 
                     symbol, quality.quality_grade, quality.completeness_ratio * 100, quality.validity_ratio * 100);
        
        // Store the data
        symbol_data_[symbol] = cleaned_data;
        if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
            symbols_.push_back(symbol);
        }
        
        // Add to current data for iteration
        current_data_.insert(current_data_.end(), cleaned_data.begin(), cleaned_data.end());
        
        // Sort by timestamp
        std::sort(current_data_.begin(), current_data_.end(), 
                  [](const OHLC& a, const OHLC& b) {
                      return a.timestamp < b.timestamp;
                  });
        
        // Save to cache
        // Save full series to cache (ignore requested dates)
        save_to_cache(symbol, cleaned_data, "", "");
        
        successful_requests_++;
        logger_->info("Successfully loaded {} data points for {} (filtered from {} total)", cleaned_data.size(), symbol, pre_filter_data.size());
        return true;
        
    } catch (const std::exception& e) {
        logger_->error("Error loading data for {}: {}", symbol, e.what());
        failed_requests_++;
        return false;
    }
}

bool AlphaVantageHandler::has_next() const {
    return current_index_ < current_data_.size();
}

OHLC AlphaVantageHandler::get_next() {
    if (!has_next()) {
        return OHLC{};
    }
    
    OHLC data = current_data_[current_index_];
    current_index_++;
    return data;
}

void AlphaVantageHandler::reset() {
    current_index_ = 0;
}

std::vector<std::string> AlphaVantageHandler::get_symbols() const {
    return symbols_;
}

std::vector<OHLC> AlphaVantageHandler::get_historical_data(const std::string& symbol) const {
    auto it = symbol_data_.find(symbol);
    if (it != symbol_data_.end()) {
        return it->second;
    }
    return {};
}

bool AlphaVantageHandler::fetch_daily_data(const std::string& symbol, [[maybe_unused]] bool force_refresh) {
    apply_rate_limit();
    
    std::string url = build_daily_url(symbol);
    auto response = make_http_request(url);
    
    if (response.response_code != 200 || response.data.empty()) {
        return false;
    }
    
    std::vector<OHLC> data = parse_daily_response(response.data, symbol);
    if (!data.empty()) {
        symbol_data_[symbol] = data;
        return true;
    }
    
    return false;
}

bool AlphaVantageHandler::fetch_intraday_data(const std::string& symbol, const std::string& interval, [[maybe_unused]] bool force_refresh) {
    apply_rate_limit();
    
    std::string url = build_intraday_url(symbol, interval);
    auto response = make_http_request(url);
    
    if (response.response_code != 200 || response.data.empty()) {
        return false;
    }
    
    std::vector<OHLC> data = parse_intraday_response(response.data, symbol);
    if (!data.empty()) {
        symbol_data_[symbol] = data;
        return true;
    }
    
    return false;
}

AlphaVantageHandler::CurlResponse AlphaVantageHandler::make_http_request_with_retry(const std::string& url) {
    CurlResponse final_response;
    
    for (int attempt = 1; attempt <= max_retry_attempts_; ++attempt) {
        auto response = make_http_request(url);
        
        if (response.response_code == 200 && !response.data.empty()) {
            return response;
        }
        
        logger_->warn("HTTP request attempt {} failed (code: {}). Error: {}", 
                     attempt, response.response_code, response.error_message);
        
        final_response = response; // Keep the last response for error reporting
        
        if (attempt < max_retry_attempts_) {
            logger_->info("Retrying in {} seconds...", retry_delay_seconds_);
            std::this_thread::sleep_for(std::chrono::seconds(retry_delay_seconds_));
        }
    }
    
    logger_->error("All retry attempts failed for URL: {}", url);
    return final_response;
}

AlphaVantageHandler::CurlResponse AlphaVantageHandler::make_http_request(const std::string& url) {
    CURL* curl = curl_easy_init();
    CurlResponse response;
    
    if (!curl) {
        response.error_message = "Failed to initialize CURL";
        logger_->error(response.error_message);
        return response;
    }
    
    // Configure CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(request_timeout_seconds_));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BacktestPro/1.0");
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
        logger_->error("CURL request failed: {}", response.error_message);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.response_code);
        
        if (detailed_logging_) {
            logger_->debug("HTTP request completed: {} bytes, code: {}", 
                          response.data.size(), response.response_code);
        }
    }
    
    curl_easy_cleanup(curl);
    return response;
}

size_t AlphaVantageHandler::write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
    size_t total_size = size * nmemb;
    response->data.append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Caching methods
bool AlphaVantageHandler::load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Check memory cache first
    auto cache_it = data_cache_.find(symbol);
    if (cache_it != data_cache_.end() && is_cache_valid(cache_it->second, start_date, end_date)) {
        // Filter cached data by date range
        std::vector<OHLC> filtered_data = cache_it->second.data;
        filter_data_by_date_range(filtered_data, start_date, end_date);
        
        if (!filtered_data.empty()) {
            symbol_data_[symbol] = filtered_data;
            if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
                symbols_.push_back(symbol);
            }
            
            // Add to current data for iteration
            current_data_.insert(current_data_.end(), filtered_data.begin(), filtered_data.end());
            std::sort(current_data_.begin(), current_data_.end(), 
                      [](const OHLC& a, const OHLC& b) { return a.timestamp < b.timestamp; });
            
            logger_->debug("Data loaded from memory cache for {}", symbol);
            return true;
        }
    }
    
    // Check disk cache if enabled
    if (!enable_disk_cache_) {
        return false;
    }
    
    std::string cache_filename = get_cache_filename(symbol, "", "");
    std::ifstream cache_file(cache_filename, std::ios::binary);
    
    if (!cache_file.is_open()) {
        return false;
    }
    
    try {
        // Read cache metadata
        nlohmann::json cache_json;
        cache_file >> cache_json;
        
        // Check if cache is still valid
        auto cached_at = std::chrono::system_clock::from_time_t(cache_json["cached_at"].get<std::time_t>());
        auto now = std::chrono::system_clock::now();
        auto cache_age = std::chrono::duration_cast<std::chrono::hours>(now - cached_at);
        
        if (cache_age > cache_expiry_hours_) {
            logger_->debug("Cache file expired for {}", symbol);
            cache_file.close();
            std::filesystem::remove(cache_filename);
            return false;
        }
        
        // Load data from cache
        std::vector<OHLC> cached_data;
        for (const auto& item : cache_json["data"]) {
            OHLC ohlc;
            ohlc.symbol = item["symbol"];
            ohlc.timestamp = std::chrono::system_clock::from_time_t(item["timestamp"].get<std::time_t>());
            ohlc.open = item["open"];
            ohlc.high = item["high"];
            ohlc.low = item["low"];
            ohlc.close = item["close"];
            ohlc.volume = item["volume"];
            cached_data.push_back(ohlc);
        }
        
        // Filter by date range
        filter_data_by_date_range(cached_data, start_date, end_date);
        
        if (!cached_data.empty()) {
            // Store in memory cache
            CacheEntry entry;
            entry.data = cached_data;
            entry.cached_at = cached_at;
            entry.start_date = start_date;
            entry.end_date = end_date;
            entry.quality = validate_data_quality(cached_data);
            data_cache_[symbol] = entry;
            
            // Store in symbol data
            symbol_data_[symbol] = cached_data;
            if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
                symbols_.push_back(symbol);
            }
            
            // Add to current data for iteration
            current_data_.insert(current_data_.end(), cached_data.begin(), cached_data.end());
            std::sort(current_data_.begin(), current_data_.end(), 
                      [](const OHLC& a, const OHLC& b) { return a.timestamp < b.timestamp; });
            
            logger_->debug("Data loaded from disk cache for {}", symbol);
            return true;
        }
        
    } catch (const std::exception& e) {
        logger_->warn("Failed to load cache file for {}: {}", symbol, e.what());
        cache_file.close();
        std::filesystem::remove(cache_filename);
    }
    
    return false;
}

void AlphaVantageHandler::save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                                       const std::string& start_date, const std::string& end_date) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Save to memory cache
    CacheEntry entry;
    entry.data = data;
    entry.cached_at = std::chrono::system_clock::now();
    entry.start_date = start_date;
    entry.end_date = end_date;
    entry.quality = validate_data_quality(data);
    data_cache_[symbol] = entry;
    
    // Save to disk cache if enabled
    if (!enable_disk_cache_) {
        return;
    }
    
    try {
        std::string cache_filename = get_cache_filename(symbol, start_date, end_date);
        std::ofstream cache_file(cache_filename, std::ios::binary);
        
        if (!cache_file.is_open()) {
            logger_->warn("Failed to create cache file for {}", symbol);
            return;
        }
        
        // Create cache JSON
        nlohmann::json cache_json;
        cache_json["symbol"] = symbol;
        cache_json["start_date"] = "";
        cache_json["end_date"] = "";
        cache_json["cached_at"] = std::chrono::system_clock::to_time_t(entry.cached_at);
        cache_json["data_count"] = data.size();
        
        // Store data
        cache_json["data"] = nlohmann::json::array();
        for (const auto& ohlc : data) {
            nlohmann::json item;
            item["symbol"] = ohlc.symbol;
            item["timestamp"] = std::chrono::system_clock::to_time_t(ohlc.timestamp);
            item["open"] = ohlc.open;
            item["high"] = ohlc.high;
            item["low"] = ohlc.low;
            item["close"] = ohlc.close;
            item["volume"] = ohlc.volume;
            cache_json["data"].push_back(item);
        }
        
        // Store quality metrics
        cache_json["quality"] = {
            {"completeness_ratio", entry.quality.completeness_ratio},
            {"validity_ratio", entry.quality.validity_ratio},
            {"missing_values", entry.quality.missing_values},
            {"outliers", entry.quality.outliers},
            {"has_gaps", entry.quality.has_gaps},
            {"quality_grade", entry.quality.quality_grade}
        };
        
        cache_file << std::setw(2) << cache_json << std::endl;
        logger_->debug("Data saved to cache for {}", symbol);
        
    } catch (const std::exception& e) {
        logger_->warn("Failed to save cache for {}: {}", symbol, e.what());
    }
}

// Data validation and quality methods
DataQualityMetrics AlphaVantageHandler::validate_data_quality(const std::vector<OHLC>& data) const {
    DataQualityMetrics metrics;
    
    if (data.empty()) {
        metrics.quality_grade = "F - No Data";
        return metrics;
    }
    
    int valid_points = 0;
    int total_points = data.size();
    int missing_values = 0;
    int outliers = 0;
    
    // Calculate mean and standard deviation for outlier detection
    double sum_close = 0.0;
    for (const auto& ohlc : data) {
        sum_close += ohlc.close;
    }
    double mean_close = sum_close / total_points;
    
    double sum_squared_diff = 0.0;
    for (const auto& ohlc : data) {
        double diff = ohlc.close - mean_close;
        sum_squared_diff += diff * diff;
    }
    double std_dev = std::sqrt(sum_squared_diff / total_points);
    
    // Validate each data point
    for (const auto& ohlc : data) {
        bool is_valid = validate_ohlc_data(ohlc);
        
        if (is_valid) {
            valid_points++;
        } else {
            missing_values++;
        }
        
        // Check for outliers (values beyond 3 standard deviations)
        if (std::abs(ohlc.close - mean_close) > 3 * std_dev) {
            outliers++;
        }
    }
    
    // Check for data gaps
    metrics.has_gaps = detect_data_gaps(data);
    
    // Calculate ratios
    metrics.completeness_ratio = static_cast<double>(valid_points) / total_points;
    metrics.validity_ratio = static_cast<double>(valid_points) / total_points;
    metrics.missing_values = missing_values;
    metrics.outliers = outliers;
    
    // Assign quality grade
    if (metrics.completeness_ratio >= 0.95 && metrics.validity_ratio >= 0.95 && !metrics.has_gaps) {
        metrics.quality_grade = "A - Excellent";
    } else if (metrics.completeness_ratio >= 0.90 && metrics.validity_ratio >= 0.90) {
        metrics.quality_grade = "B - Good";
    } else if (metrics.completeness_ratio >= 0.80 && metrics.validity_ratio >= 0.80) {
        metrics.quality_grade = "C - Fair";
    } else if (metrics.completeness_ratio >= 0.70 && metrics.validity_ratio >= 0.70) {
        metrics.quality_grade = "D - Poor";
    } else {
        metrics.quality_grade = "F - Unacceptable";
    }
    
    return metrics;
}

std::vector<OHLC> AlphaVantageHandler::clean_and_validate_data(const std::vector<OHLC>& raw_data) const {
    std::vector<OHLC> cleaned_data;
    cleaned_data.reserve(raw_data.size());
    
    for (const auto& ohlc : raw_data) {
        if (validate_ohlc_data(ohlc)) {
            cleaned_data.push_back(ohlc);
        } else {
            logger_->debug("Invalid OHLC data point filtered out for {} at timestamp {}", 
                          ohlc.symbol, std::chrono::system_clock::to_time_t(ohlc.timestamp));
        }
    }
    
    // Remove outliers
    std::vector<OHLC> no_outliers = remove_outliers(cleaned_data);
    
    // Fill missing data if needed
    std::vector<OHLC> final_data = no_outliers;
    fill_missing_data(final_data);
    
    logger_->debug("Data cleaning: {} -> {} -> {} -> {} points", 
                  raw_data.size(), cleaned_data.size(), no_outliers.size(), final_data.size());
    
    return final_data;
}

bool AlphaVantageHandler::validate_ohlc_data(const OHLC& data) const {
    // Check for valid numeric values
    if (std::isnan(data.open) || std::isnan(data.high) || std::isnan(data.low) || std::isnan(data.close)) {
        return false;
    }
    
    if (std::isinf(data.open) || std::isinf(data.high) || std::isinf(data.low) || std::isinf(data.close)) {
        return false;
    }
    
    // Check for negative values (except for some edge cases)
    if (data.open <= 0 || data.high <= 0 || data.low <= 0 || data.close <= 0) {
        return false;
    }
    
    // Check OHLC relationships
    if (data.high < data.low) {
        return false;
    }
    
    if (data.high < data.open || data.high < data.close) {
        return false;
    }
    
    if (data.low > data.open || data.low > data.close) {
        return false;
    }
    
    // Check for reasonable volume (negative volume is invalid)
    if (data.volume < 0) {
        return false;
    }
    
    // Check for extreme price ratios (likely data errors)
    double price_range = data.high - data.low;
    double avg_price = (data.high + data.low) / 2.0;
    if (price_range / avg_price > 0.5) { // More than 50% daily range is suspicious
        return false;
    }
    
    return true;
}

std::vector<OHLC> AlphaVantageHandler::remove_outliers(const std::vector<OHLC>& data, double z_threshold) const {
    if (data.size() < 10) {
        return data; // Not enough data for outlier detection
    }
    
    // Calculate mean and standard deviation for close prices
    double sum = 0.0;
    for (const auto& ohlc : data) {
        sum += ohlc.close;
    }
    double mean = sum / data.size();
    
    double sum_squared_diff = 0.0;
    for (const auto& ohlc : data) {
        double diff = ohlc.close - mean;
        sum_squared_diff += diff * diff;
    }
    double std_dev = std::sqrt(sum_squared_diff / data.size());
    
    // Remove outliers
    std::vector<OHLC> filtered_data;
    filtered_data.reserve(data.size());
    
    int outliers_removed = 0;
    for (const auto& ohlc : data) {
        double z_score = std::abs(ohlc.close - mean) / std_dev;
        if (z_score <= z_threshold) {
            filtered_data.push_back(ohlc);
        } else {
            outliers_removed++;
            logger_->debug("Outlier removed: {} close={:.2f} (z-score={:.2f})", 
                          ohlc.symbol, ohlc.close, z_score);
        }
    }
    
    if (outliers_removed > 0) {
        logger_->info("Removed {} outliers from {} data points", outliers_removed, data.size());
    }
    
    return filtered_data;
}

void AlphaVantageHandler::fill_missing_data(std::vector<OHLC>& data) const {
    if (data.size() < 2) {
        return;
    }
    
    // Sort data by timestamp
    std::sort(data.begin(), data.end(), [](const OHLC& a, const OHLC& b) {
        return a.timestamp < b.timestamp;
    });
    
    // Gap filling is not implemented in this build. Missing days are left unchanged.
    logger_->debug("Data gap filling step completed (no-op)");
}

bool AlphaVantageHandler::detect_data_gaps(const std::vector<OHLC>& data, int max_gap_days) const {
    if (data.size() < 2) {
        return false;
    }
    
    // Sort data by timestamp
    std::vector<OHLC> sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end(), [](const OHLC& a, const OHLC& b) {
        return a.timestamp < b.timestamp;
    });
    
    for (size_t i = 1; i < sorted_data.size(); ++i) {
        auto time_diff = std::chrono::duration_cast<std::chrono::hours>(
            sorted_data[i].timestamp - sorted_data[i-1].timestamp);
        
        int gap_days = time_diff.count() / 24;
        
        // Skip weekends (2-3 day gaps are normal)
        if (gap_days > max_gap_days) {
            logger_->debug("Data gap detected: {} days between {} and {}", 
                          gap_days, 
                          std::chrono::system_clock::to_time_t(sorted_data[i-1].timestamp),
                          std::chrono::system_clock::to_time_t(sorted_data[i].timestamp));
            return true;
        }
    }
    
    return false;
}

// URL building methods
std::string AlphaVantageHandler::build_daily_url(const std::string& symbol) const {
    std::ostringstream url;
    url << "https://www.alphavantage.co/query?"
        << "function=TIME_SERIES_DAILY"
        << "&symbol=" << symbol
        << "&outputsize=full"
        << "&apikey=" << api_key_;
    return url.str();
}

std::string AlphaVantageHandler::build_intraday_url(const std::string& symbol, const std::string& interval) const {
    std::ostringstream url;
    url << "https://www.alphavantage.co/query?"
        << "function=TIME_SERIES_INTRADAY"
        << "&symbol=" << symbol
        << "&interval=" << interval
        << "&outputsize=full"
        << "&apikey=" << api_key_;
    return url.str();
}

std::string AlphaVantageHandler::build_adjusted_url(const std::string& symbol) const {
    std::ostringstream url;
    url << "https://www.alphavantage.co/query?"
        << "function=TIME_SERIES_DAILY_ADJUSTED"
        << "&symbol=" << symbol
        << "&outputsize=full"
        << "&apikey=" << api_key_;
    return url.str();
}

std::string AlphaVantageHandler::build_quote_url(const std::string& symbol) const {
    std::ostringstream url;
    url << "https://www.alphavantage.co/query?"
        << "function=GLOBAL_QUOTE"
        << "&symbol=" << symbol
        << "&apikey=" << api_key_;
    return url.str();
}

std::string AlphaVantageHandler::build_intraday_extended_url(const std::string& symbol,
                                                            const std::string& interval,
                                                            int year, int month) const {
    // Alpha Vantage intraday extended: series=year{1..2}month{1..6} for premium keys
    // Map year and month into series label: year1month1 .. year2month6 (24 slices)
    int series_index = (year - 1) * 6 + month; // 1..24
    int series_year = (series_index - 1) / 6 + 1;
    int series_month = (series_index - 1) % 6 + 1;
    std::ostringstream series;
    series << "year" << series_year << "month" << series_month;
    std::ostringstream url;
    url << "https://www.alphavantage.co/query?"
        << "function=TIME_SERIES_INTRADAY_EXTENDED"
        << "&symbol=" << symbol
        << "&interval=" << interval
        << "&slice=" << series.str()
        << "&adjusted=false"
        << "&apikey=" << api_key_;
    return url.str();
}

// Cache utility methods
std::string AlphaVantageHandler::get_cache_filename(const std::string& symbol, const std::string& start_date, const std::string& end_date) const {
    (void)start_date; (void)end_date; // Date-agnostic cache: one file per symbol
    std::ostringstream filename;
    filename << cache_directory_ << "/" << symbol << ".json";
    return filename.str();
}

bool AlphaVantageHandler::is_cache_valid(const CacheEntry& entry, const std::string& start_date, const std::string& end_date) const {
    (void)start_date; (void)end_date; // Cache validity is time-based; date range is applied when reading.
    auto now = std::chrono::system_clock::now();
    auto cache_age = std::chrono::duration_cast<std::chrono::hours>(now - entry.cached_at);
    
    // Check if cache has expired
    if (cache_age > cache_expiry_hours_) {
        return false;
    }

    // Date-agnostic cache: we store full series and filter at read time
    return entry.is_valid;
}

void AlphaVantageHandler::cleanup_expired_cache() {
    if (!enable_disk_cache_) {
        return;
    }
    
    try {
        [[maybe_unused]] auto now = std::chrono::system_clock::now();
        int files_removed = 0;
        
        for (const auto& entry : std::filesystem::directory_iterator(cache_directory_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                auto file_time = std::filesystem::last_write_time(entry);
                auto file_age = std::chrono::duration_cast<std::chrono::hours>(
                    std::chrono::file_clock::now() - file_time);
                
                if (file_age > cache_expiry_hours_) {
                    std::filesystem::remove(entry.path());
                    files_removed++;
                }
            }
        }
        
        if (files_removed > 0) {
            logger_->info("Cleaned up {} expired cache files", files_removed);
        }
        
    } catch (const std::exception& e) {
        logger_->warn("Failed to cleanup cache: {}", e.what());
    }
}

// Statistics and monitoring
nlohmann::json AlphaVantageHandler::get_api_usage_stats() const {
    nlohmann::json stats;
    stats["successful_requests"] = successful_requests_;
    stats["failed_requests"] = failed_requests_;
    stats["total_requests"] = successful_requests_ + failed_requests_;
    stats["success_rate"] = (successful_requests_ + failed_requests_ > 0) ? 
        static_cast<double>(successful_requests_) / (successful_requests_ + failed_requests_) : 0.0;
    stats["cache_hits"] = cache_hits_;
    stats["cache_misses"] = cache_misses_;
    stats["cache_hit_rate"] = (cache_hits_ + cache_misses_ > 0) ? 
        static_cast<double>(cache_hits_) / (cache_hits_ + cache_misses_) : 0.0;
    
    // Rate limiting info
    auto now = std::chrono::steady_clock::now();
    auto one_hour_ago = now - std::chrono::hours(1);
    int recent_requests = 0;
    for (const auto& timestamp : request_history_) {
        if (timestamp > one_hour_ago) {
            recent_requests++;
        }
    }
    
    stats["requests_last_hour"] = recent_requests;
    stats["requests_remaining_hour"] = std::max(0, MAX_REQUESTS_PER_HOUR - recent_requests);
    
    return stats;
}

nlohmann::json AlphaVantageHandler::get_cache_statistics() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    nlohmann::json stats;
    stats["memory_cache_size"] = data_cache_.size();
    stats["cache_enabled"] = enable_disk_cache_;
    stats["cache_directory"] = cache_directory_;
    stats["cache_expiry_hours"] = cache_expiry_hours_.count();
    
    if (enable_disk_cache_) {
        try {
            int disk_cache_files = 0;
            size_t total_cache_size = 0;
            
            for (const auto& entry : std::filesystem::directory_iterator(cache_directory_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    disk_cache_files++;
                    total_cache_size += std::filesystem::file_size(entry);
                }
            }
            
            stats["disk_cache_files"] = disk_cache_files;
            stats["total_cache_size_bytes"] = total_cache_size;
            
        } catch (const std::exception& e) {
            stats["cache_error"] = e.what();
        }
    }
    
    return stats;
}

void AlphaVantageHandler::enable_detailed_logging(bool enable) {
    detailed_logging_ = enable;
    if (enable) {
        logger_->set_level(spdlog::level::debug);
        logger_->info("Detailed logging enabled for Alpha Vantage handler");
    } else {
        logger_->set_level(spdlog::level::info);
    }
}

std::vector<OHLC> AlphaVantageHandler::parse_daily_response(const std::string& json_response, const std::string& symbol) {
    std::vector<OHLC> data;
    
    try {
        nlohmann::json json = nlohmann::json::parse(json_response);
        
        // Check for API errors
        if (json.contains("Error Message")) {
            std::cerr << "Alpha Vantage API Error: " << json["Error Message"] << std::endl;
            return data;
        }
        
        if (json.contains("Note")) {
            std::cerr << "Alpha Vantage API Note: " << json["Note"] << std::endl;
            return data;
        }
        
        // Parse time series data
        if (!json.contains("Time Series (Daily)")) {
            std::cerr << "No daily time series data found in response" << std::endl;
            return data;
        }
        
        auto time_series = json["Time Series (Daily)"];
        
        for (auto& [date_str, day_data] : time_series.items()) {
            OHLC ohlc;
            ohlc.symbol = symbol;
            ohlc.timestamp = parse_alpha_vantage_timestamp(date_str);
            ohlc.open = std::stod(day_data["1. open"].get<std::string>());
            ohlc.high = std::stod(day_data["2. high"].get<std::string>());
            ohlc.low = std::stod(day_data["3. low"].get<std::string>());
            ohlc.close = std::stod(day_data["4. close"].get<std::string>());
            ohlc.volume = std::stoll(day_data["5. volume"].get<std::string>());
            
            data.push_back(ohlc);
        }
        
        // Sort by timestamp (oldest first)
        std::sort(data.begin(), data.end(), [](const OHLC& a, const OHLC& b) {
            return a.timestamp < b.timestamp;
        });
        
    } catch (const std::exception& e) {
        std::cerr << "Error parsing Alpha Vantage response: " << e.what() << std::endl;
    }
    
    return data;
}

std::vector<OHLC> AlphaVantageHandler::parse_adjusted_response(const std::string& json_response, const std::string& symbol) {
    std::vector<OHLC> data;
    try {
        nlohmann::json json = nlohmann::json::parse(json_response);

        std::string error_message;
        if (!validate_json_response(json, error_message)) {
            std::cerr << "Alpha Vantage API error: " << error_message << std::endl;
            return data;
        }

        if (!json.contains("Time Series (Daily)")) {
            std::cerr << "No adjusted daily time series data found in response" << std::endl;
            return data;
        }

        auto time_series = json["Time Series (Daily)"];

        for (auto& [date_str, day_data] : time_series.items()) {
            OHLC ohlc;
            ohlc.symbol = symbol;
            ohlc.timestamp = parse_alpha_vantage_timestamp(date_str);
            // TIME_SERIES_DAILY_ADJUSTED keys per docs
            // 1. open, 2. high, 3. low, 4. close, 5. adjusted close, 6. volume, 7. dividend amount, 8. split coefficient
            ohlc.open = std::stod(day_data["1. open"].get<std::string>());
            ohlc.high = std::stod(day_data["2. high"].get<std::string>());
            ohlc.low = std::stod(day_data["3. low"].get<std::string>());
            // Use adjusted close as close to account for splits/dividends
            if (day_data.contains("5. adjusted close")) {
                ohlc.close = std::stod(day_data["5. adjusted close"].get<std::string>());
            } else {
                ohlc.close = std::stod(day_data["4. close"].get<std::string>());
            }
            // Volume key can be "6. volume"
            if (day_data.contains("6. volume")) {
                ohlc.volume = std::stoll(day_data["6. volume"].get<std::string>());
            } else if (day_data.contains("5. volume")) {
                ohlc.volume = std::stoll(day_data["5. volume"].get<std::string>());
            } else {
                ohlc.volume = 0;
            }

            data.push_back(ohlc);
        }

        std::sort(data.begin(), data.end(), [](const OHLC& a, const OHLC& b) {
            return a.timestamp < b.timestamp;
        });

    } catch (const std::exception& e) {
        std::cerr << "Error parsing Alpha Vantage adjusted response: " << e.what() << std::endl;
    }
    return data;
}

std::vector<OHLC> AlphaVantageHandler::parse_intraday_response(const std::string& json_response, const std::string& symbol) {
    std::vector<OHLC> data;
    
    try {
        nlohmann::json json = nlohmann::json::parse(json_response);
        
        // Check for API errors
        if (json.contains("Error Message")) {
            std::cerr << "Alpha Vantage API Error: " << json["Error Message"] << std::endl;
            return data;
        }
        
        if (json.contains("Note")) {
            std::cerr << "Alpha Vantage API Note: " << json["Note"] << std::endl;
            return data;
        }
        
        // Find the time series key (varies by interval)
        std::string time_series_key;
        for (auto& [key, value] : json.items()) {
            if (key.find("Time Series") != std::string::npos) {
                time_series_key = key;
                break;
            }
        }
        
        if (time_series_key.empty()) {
            std::cerr << "No intraday time series data found in response" << std::endl;
            return data;
        }
        
        auto time_series = json[time_series_key];
        
        for (auto& [datetime_str, interval_data] : time_series.items()) {
            OHLC ohlc;
            ohlc.symbol = symbol;
            ohlc.timestamp = parse_alpha_vantage_timestamp(datetime_str);
            ohlc.open = std::stod(interval_data["1. open"].get<std::string>());
            ohlc.high = std::stod(interval_data["2. high"].get<std::string>());
            ohlc.low = std::stod(interval_data["3. low"].get<std::string>());
            ohlc.close = std::stod(interval_data["4. close"].get<std::string>());
            ohlc.volume = std::stoll(interval_data["5. volume"].get<std::string>());
            
            data.push_back(ohlc);
        }
        
        // Sort by timestamp (oldest first)
        std::sort(data.begin(), data.end(), [](const OHLC& a, const OHLC& b) {
            return a.timestamp < b.timestamp;
        });
        
    } catch (const std::exception& e) {
        std::cerr << "Error parsing Alpha Vantage intraday response: " << e.what() << std::endl;
    }
    
    return data;
}

std::vector<OHLC> AlphaVantageHandler::parse_intraday_extended_csv(const std::string& csv,
                                                                   const std::string& symbol) const {
    std::vector<OHLC> data;
    std::istringstream ss(csv);
    std::string line;
    // Skip header
    if (!std::getline(ss, line)) return data;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string ts, open, high, low, close, volume;
        // timestamp,open,high,low,close,volume
        if (!std::getline(ls, ts, ',')) continue;
        if (!std::getline(ls, open, ',')) continue;
        if (!std::getline(ls, high, ',')) continue;
        if (!std::getline(ls, low, ',')) continue;
        if (!std::getline(ls, close, ',')) continue;
        if (!std::getline(ls, volume, ',')) volume = "0";
        OHLC o;
        o.symbol = symbol;
        o.timestamp = parse_alpha_vantage_timestamp(ts);
        try {
            o.open = std::stod(open);
            o.high = std::stod(high);
            o.low = std::stod(low);
            o.close = std::stod(close);
            o.volume = std::stoll(volume);
        } catch (...) { continue; }
        data.push_back(o);
    }
    std::sort(data.begin(), data.end(), [](const OHLC& a, const OHLC& b){ return a.timestamp < b.timestamp; });
    return data;
}

std::vector<OHLC> AlphaVantageHandler::resample_to_daily(const std::vector<OHLC>& intraday) const {
    if (intraday.empty()) return {};
    std::vector<OHLC> daily;
    // Group by date
    std::map<std::string, std::vector<const OHLC*>> by_date;
    for (const auto& bar : intraday) {
        auto t = std::chrono::system_clock::to_time_t(bar.timestamp);
        std::tm* g = std::gmtime(&t);
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", g);
        by_date[buf].push_back(&bar);
    }
    for (auto& [date, bars] : by_date) {
        if (bars.empty()) continue;
        // Ensure bars are sorted by timestamp
        std::sort(bars.begin(), bars.end(), [](const OHLC* a, const OHLC* b){ return a->timestamp < b->timestamp; });
        OHLC d;
        d.symbol = bars.front()->symbol;
        d.timestamp = bars.front()->timestamp; // date start
        d.open = bars.front()->open;
        d.close = bars.back()->close;
        d.high = bars.front()->high;
        d.low = bars.front()->low;
        long long vol = 0;
        for (const auto* b : bars) {
            d.high = std::max(d.high, b->high);
            d.low = std::min(d.low, b->low);
            vol += b->volume;
        }
        d.volume = vol;
        daily.push_back(d);
    }
    std::sort(daily.begin(), daily.end(), [](const OHLC& a, const OHLC& b){ return a.timestamp < b.timestamp; });
    return daily;
}

bool AlphaVantageHandler::fetch_intraday_extended_and_resample_daily(const std::string& symbol,
                                                                     const std::string& interval,
                                                                     const std::string& start_date,
                                                                     const std::string& end_date) {
    // Requires premium key; if server returns JSON with error/note, we'll bail out gracefully
    std::vector<OHLC> all_intraday;
    // Determine needed slices roughly based on date range (up to 24 months per AV)
    // We will iterate all 24 slices conservatively, then filter by date.
    for (int year = 1; year <= 2; ++year) {
        for (int month = 1; month <= 6; ++month) {
            apply_rate_limit();
            auto url = build_intraday_extended_url(symbol, interval, year, month);
            auto resp = make_http_request(url);
            if (resp.response_code != 200 || resp.data.empty()) {
                // If JSON error, stop early as extended may not be available
                continue;
            }
            // Heuristic: extended returns CSV; if it looks like JSON, skip
            if (resp.data.size() > 0 && resp.data[0] == '{') {
                // Not available on this key or rate limited; skip chunk
                continue;
            }
            auto bars = parse_intraday_extended_csv(resp.data, symbol);
            all_intraday.insert(all_intraday.end(), bars.begin(), bars.end());
        }
    }
    if (all_intraday.empty()) return false;
    auto daily = resample_to_daily(all_intraday);
    if (daily.empty()) return false;
    // Filter to requested date range
    std::vector<OHLC> filtered = daily;
    filter_data_by_date_range(filtered, start_date, end_date);
    if (filtered.empty()) return false;
    // Store
    symbol_data_[symbol] = filtered;
    if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
        symbols_.push_back(symbol);
    }
    current_data_.insert(current_data_.end(), filtered.begin(), filtered.end());
    std::sort(current_data_.begin(), current_data_.end(), [](const OHLC& a, const OHLC& b){ return a.timestamp < b.timestamp; });
    save_to_cache(symbol, filtered, start_date, end_date);
    return true;
}

Timestamp AlphaVantageHandler::parse_alpha_vantage_timestamp(const std::string& timestamp_str) const {
    std::tm tm = {};
    std::istringstream ss(timestamp_str);
    
    // Try parsing date-time format first (YYYY-MM-DD HH:MM:SS)
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) {
        // Try parsing date-only format (YYYY-MM-DD)
        ss.clear();
        ss.str(timestamp_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

bool AlphaVantageHandler::validate_json_response(const nlohmann::json& json, std::string& error_message) const {
    if (json.contains("Error Message")) {
        error_message = json["Error Message"].get<std::string>();
        return false;
    }
    if (json.contains("Note")) {
        error_message = json["Note"].get<std::string>();
        return false;
    }
    if (json.contains("Information")) {
        error_message = json["Information"].get<std::string>();
        return false;
    }
    return true;
}

void AlphaVantageHandler::filter_data_by_date_range(
    std::vector<OHLC>& data,
    const std::string& start_date,
    const std::string& end_date
) {
    if (start_date.empty() && end_date.empty()) {
        return; // No filtering needed
    }
    
    Timestamp start_time = std::chrono::system_clock::time_point::min();
    Timestamp end_time = std::chrono::system_clock::time_point::max();
    
    if (!start_date.empty()) {
        start_time = parse_alpha_vantage_timestamp(start_date);
    }
    
    if (!end_date.empty()) {
        end_time = parse_alpha_vantage_timestamp(end_date);
    }
    
    data.erase(
        std::remove_if(data.begin(), data.end(),
            [start_time, end_time](const OHLC& ohlc) {
                return ohlc.timestamp < start_time || ohlc.timestamp > end_time;
            }),
        data.end()
    );
}

void AlphaVantageHandler::apply_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto one_hour_ago = now - std::chrono::hours(1);
    
    // Remove requests older than 1 hour
    request_history_.erase(
        std::remove_if(request_history_.begin(), request_history_.end(),
            [one_hour_ago](const std::chrono::steady_clock::time_point& time) {
                return time < one_hour_ago;
            }),
        request_history_.end()
    );
    
    // Check if we've exceeded the hourly limit
    if (request_history_.size() >= MAX_REQUESTS_PER_HOUR) {
        auto oldest_request = *std::min_element(request_history_.begin(), request_history_.end());
        auto wait_until = oldest_request + std::chrono::hours(1);
        auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(wait_until - now);
        
        if (wait_time.count() > 0) {
            std::cout << "Rate limit exceeded. Waiting " << wait_time.count() / 1000 
                      << " seconds until next request is allowed..." << std::endl;
            std::this_thread::sleep_for(wait_time);
        }
    }
    
    // Also enforce minimum interval between requests (3 minutes)
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time_);
    if (time_since_last.count() < MIN_REQUEST_INTERVAL_MS) {
        auto sleep_time = MIN_REQUEST_INTERVAL_MS - time_since_last.count();
        std::cout << "Minimum interval rate limiting: sleeping for " << sleep_time / 1000 
                  << " seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    }
    
    // Record this request
    auto request_time = std::chrono::steady_clock::now();
    request_history_.push_back(request_time);
    last_request_time_ = request_time;
    
    std::cout << "API request " << request_history_.size() << "/" << MAX_REQUESTS_PER_HOUR 
              << " in current hour" << std::endl;
}

} // namespace backtesting 