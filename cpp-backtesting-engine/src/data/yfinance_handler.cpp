#include "data/yfinance_handler.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cctype>
#include <cstring>
#include <curl/curl.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace backtesting {

YFinanceHandler::YFinanceHandler(
    const std::string& cache_dir,
    bool enable_disk_cache,
    int cache_expiry_hours,
    DataInterval interval
) : cache_directory_(cache_dir.empty() ? "./yfinance_cache" : cache_dir), 
    enable_disk_cache_(enable_disk_cache),
    cache_expiry_hours_(cache_expiry_hours),
    detailed_logging_(false),
    current_index_(0),
    current_interval_(interval),
    last_request_time_(std::chrono::steady_clock::now() - std::chrono::milliseconds(MIN_REQUEST_INTERVAL_MS)) {
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Setup logging
    initialize_logging();
    
    // Create cache directory if it doesn't exist
    create_cache_directory();
    
    // Clean up expired cache entries on startup
    if (enable_disk_cache_) {
        cleanup_expired_cache();
    }
    
    logger_->info("YFinance handler initialized with cache directory: {} and interval: {}", 
                  cache_directory_, get_interval_name(interval));
}

YFinanceHandler::~YFinanceHandler() {
    curl_global_cleanup();
}

void YFinanceHandler::initialize_logging() {
    try {
        logger_ = spdlog::get("yfinance");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("yfinance");
        }
    } catch (...) {
        // Fallback to basic stdout logger if colored logger fails
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("yfinance", console_sink);
        spdlog::register_logger(logger_);
    }
}

void YFinanceHandler::create_cache_directory() {
    if (enable_disk_cache_) {
        try {
            std::filesystem::create_directories(cache_directory_);
            logger_->info("Cache directory initialized: {}", cache_directory_);
        } catch (const std::exception& e) {
            logger_->warn("Failed to create cache directory: {}", e.what());
            enable_disk_cache_ = false;
        }
    }
}

bool YFinanceHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) {
    try {
        // Normalize symbol first (aligned with yahoo-finance2 API)
        std::string normalized_symbol = normalize_symbol(symbol);
        
        if (normalized_symbol != symbol) {
            logger_->info("Normalized symbol: {} -> {}", symbol, normalized_symbol);
        }
        
        logger_->info("Loading data for symbol: {} from {} to {}", normalized_symbol, start_date, end_date);
        
        // Check cache first (using normalized symbol and current interval)
        if (load_from_cache(normalized_symbol, start_date, end_date, current_interval_)) {
            logger_->info("Data loaded from cache for {} (interval: {})", normalized_symbol, get_interval_name(current_interval_));
            cache_hits_++;
            return true;
        }
        
        cache_misses_++;
        
        // Apply rate limiting before making API request
        apply_rate_limit();
        
        // Fetch data from yfinance API (using normalized symbol)
        std::string url = build_yfinance_url(normalized_symbol, start_date, end_date);
        logger_->info("Fetching data from yfinance for {}: {}", normalized_symbol, url);
        auto response = make_http_request_with_retry(url);
        
        if (response.data.empty() || response.response_code != 200) {
            logger_->error("Failed to fetch data for symbol: {} (HTTP {}) - Response: {}", 
                          normalized_symbol, response.response_code, response.data.substr(0, 200));
            failed_requests_++;
            return false;
        }
        
        // Log successful response for debugging
        logger_->info("YFinance API response for {}: {} bytes, HTTP {}", normalized_symbol, response.data.size(), response.response_code);
        
        logger_->info("Successfully received {} bytes from yfinance for {}", response.data.size(), normalized_symbol);
        
        // Parse the JSON response (using normalized symbol)
        std::vector<OHLC> raw_data = parse_yfinance_response(response.data, normalized_symbol);
        
        if (raw_data.empty()) {
            logger_->error("No data parsed for symbol: {} after all retries", normalized_symbol);
            failed_requests_++;
            return false;
        }
        
        logger_->info("Successfully parsed {} data points for {}", raw_data.size(), normalized_symbol);
        
        // Clean and validate the data
        std::vector<OHLC> cleaned_data = clean_and_validate_data(raw_data);

        // Keep a copy before filtering so we can fallback if needed
        std::vector<OHLC> pre_filter_data = cleaned_data;
        
        // Normalize date range: if start > end, swap
        if (!start_date.empty() && !end_date.empty()) {
            auto start_tp = parse_yfinance_timestamp(start_date);
            auto end_tp = parse_yfinance_timestamp(end_date);
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
                    start_date, end_date, normalized_symbol,
                    std::chrono::system_clock::to_time_t(first_ts),
                    std::chrono::system_clock::to_time_t(last_ts)
                );
                cleaned_data = pre_filter_data;
            } else {
                logger_->warn("No data available for {} after cleaning", normalized_symbol);
                return false;
            }
        }
        
        // Validate data quality
        YFinanceDataQualityMetrics quality = validate_data_quality(cleaned_data);
        logger_->info("Data quality for {}: {} (completeness: {:.1f}%, validity: {:.1f}%)", 
                     normalized_symbol, quality.quality_grade, quality.completeness_ratio * 100, quality.validity_ratio * 100);
        
        // Store the data (using normalized symbol)
        symbol_data_[normalized_symbol] = cleaned_data;
        if (std::find(symbols_.begin(), symbols_.end(), normalized_symbol) == symbols_.end()) {
            symbols_.push_back(normalized_symbol);
        }
        
        // Add to current data for iteration
        current_data_.insert(current_data_.end(), cleaned_data.begin(), cleaned_data.end());
        
        // Sort by timestamp
        std::sort(current_data_.begin(), current_data_.end(), 
                  [](const OHLC& a, const OHLC& b) {
                      return a.timestamp < b.timestamp;
                  });
        
        // Save to cache (using normalized symbol)
        save_to_cache(normalized_symbol, cleaned_data, start_date, end_date);
        
        successful_requests_++;
        logger_->info("Successfully loaded {} data points for {} (filtered from {} total)", 
                     cleaned_data.size(), normalized_symbol, pre_filter_data.size());
        return true;
        
    } catch (const std::exception& e) {
        logger_->error("Error loading data for {}: {}", symbol, e.what());
        failed_requests_++;
        return false;
    }
}

bool YFinanceHandler::load_multiple_symbols(
    const std::vector<std::string>& symbols,
    const std::string& start_date,
    const std::string& end_date
) {
    bool all_success = true;
    for (const auto& symbol : symbols) {
        if (!load_symbol_data(symbol, start_date, end_date)) {
            logger_->error("Failed to load data for symbol: {}", symbol);
            all_success = false;
        }
    }
    return all_success;
}

bool YFinanceHandler::has_next() const {
    return current_index_ < current_data_.size();
}

OHLC YFinanceHandler::get_next() {
    if (!has_next()) {
        return OHLC{};
    }
    
    OHLC data = current_data_[current_index_];
    current_index_++;
    return data;
}

void YFinanceHandler::reset() {
    current_index_ = 0;
}

std::vector<std::string> YFinanceHandler::get_symbols() const {
    return symbols_;
}

std::vector<OHLC> YFinanceHandler::get_historical_data(const std::string& symbol) const {
    auto it = symbol_data_.find(symbol);
    if (it != symbol_data_.end()) {
        return it->second;
    }
    return {};
}

std::string YFinanceHandler::build_yfinance_url(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) const {
    return build_yfinance_url(symbol, start_date, end_date, current_interval_);
}

std::string YFinanceHandler::build_yfinance_url(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date,
    DataInterval interval
) const {
    // Resolve optional proxy base URL from environment.
    // If unset, query Yahoo Finance directly so backend works standalone.
    auto get_market_data_base_url = []() -> std::string {
        const char* env_val = std::getenv("MARKET_DATA_API_BASE_URL");
        std::string base = (env_val && std::strlen(env_val) > 0) ? std::string(env_val) : "";

        // Trim whitespace
        auto is_space = [](unsigned char c) { return std::isspace(c); };
        base.erase(base.begin(), std::find_if(base.begin(), base.end(), [&](char c){ return !is_space(static_cast<unsigned char>(c)); }));
        base.erase(std::find_if(base.rbegin(), base.rend(), [&](char c){ return !is_space(static_cast<unsigned char>(c)); }).base(), base.end());

        // Remove trailing slash for consistent URL join
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }

        return base;
    };

    // Normalize symbol first
    std::string normalized_symbol = normalize_symbol(symbol);
    
    std::ostringstream url;
    
    // Calculate date ranges properly
    time_t start_epoch = 0;
    time_t end_epoch = std::time(nullptr); // Current time as default end
    
    if (!start_date.empty()) {
        try {
            auto start_tp = parse_yfinance_timestamp(start_date);
            start_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                start_tp.time_since_epoch()).count();
            // Validate epoch is reasonable (not just year - should be > 946684800 = 2000-01-01)
            if (start_epoch < 946684800) {
                logger_->warn("Start date '{}' parsed to suspicious epoch {} (likely only year extracted), using 1 year ago", 
                             start_date, start_epoch);
                start_epoch = end_epoch - (365 * 24 * 60 * 60);
            }
        } catch (const std::exception& e) {
            logger_->warn("Failed to parse start date '{}', using default: {}", start_date, e.what());
            // Use 1 year ago as default
            start_epoch = end_epoch - (365 * 24 * 60 * 60);
        }
    } else {
        // Default to 1 year ago if no start date
        start_epoch = end_epoch - (365 * 24 * 60 * 60);
    }
    
    if (!end_date.empty()) {
        try {
            auto end_tp = parse_yfinance_timestamp(end_date);
            end_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                end_tp.time_since_epoch()).count();
            // Validate epoch is reasonable
            if (end_epoch < 946684800) {
                logger_->warn("End date '{}' parsed to suspicious epoch {} (likely only year extracted), using current time", 
                             end_date, end_epoch);
                end_epoch = std::time(nullptr);
            }
        } catch (const std::exception& e) {
            logger_->warn("Failed to parse end date '{}', using current time: {}", end_date, e.what());
            end_epoch = std::time(nullptr);
        }
    }
    
    // Ensure end_epoch is after start_epoch
    if (end_epoch <= start_epoch) {
        logger_->warn("End date is before or equal to start date, adjusting end date");
        end_epoch = start_epoch + (365 * 24 * 60 * 60); // Add 1 year to start
    }
    
    const std::string proxy_base = get_market_data_base_url();
    if (!proxy_base.empty()) {
        // Proxy mode (expects query params from/to and yfinance-compatible JSON).
        url << proxy_base << "/api/market-data?symbol=" << normalized_symbol;
        url << "&interval=" << interval_to_yfinance_string(interval);
        url << "&from=" << start_epoch;
        url << "&to=" << end_epoch;
    } else {
        // Standalone mode using Yahoo Finance chart API.
        url << "https://query1.finance.yahoo.com/v8/finance/chart/" << normalized_symbol;
        url << "?period1=" << start_epoch;
        url << "&period2=" << end_epoch;
        url << "&interval=" << interval_to_yfinance_string(interval);
        url << "&includePrePost=false&events=div,splits";
    }
    
    logger_->debug("Built YFinance URL: {}", url.str());
    return url.str();
}

std::vector<OHLC> YFinanceHandler::parse_yfinance_response(const std::string& response, const std::string& symbol) const {
    std::vector<OHLC> data;
    
    try {
        nlohmann::json json = nlohmann::json::parse(response);
        
        // Check for API errors
        if (json.contains("error")) {
            logger_->error("YFinance API Error: {}", json["error"].get<std::string>());
            return data;
        }
        
        if (!json.contains("chart") || !json["chart"].contains("result")) {
            logger_->error("No chart data found in yfinance response");
            return data;
        }
        
        auto result = json["chart"]["result"];
        if (result.empty() || !result[0].contains("timestamp") || !result[0].contains("indicators")) {
            logger_->error("Invalid chart result structure");
            return data;
        }
        
        auto timestamps = result[0]["timestamp"];
        auto indicators = result[0]["indicators"];
        
        if (!indicators.contains("quote") || indicators["quote"].empty()) {
            logger_->error("No quote data found in indicators");
            return data;
        }
        
        auto quote = indicators["quote"][0];
        auto opens = quote["open"];
        auto highs = quote["high"];
        auto lows = quote["low"];
        auto closes = quote["close"];
        auto volumes = quote["volume"];
        
        if (timestamps.size() != opens.size() || timestamps.size() != highs.size() ||
            timestamps.size() != lows.size() || timestamps.size() != closes.size()) {
            logger_->error("Mismatched data array sizes in yfinance response");
            return data;
        }
        
        for (size_t i = 0; i < timestamps.size(); ++i) {
            // Skip if timestamp is null
            if (timestamps[i].is_null()) {
                continue;
            }
            
            // Use safe conversion for all numeric values (aligned with yahoo-finance2 API)
            double open_val = safe_to_double(opens[i]);
            double high_val = safe_to_double(highs[i]);
            double low_val = safe_to_double(lows[i]);
            double close_val = safe_to_double(closes[i]);
            
            // Skip if close price is invalid (required field)
            if (close_val <= 0.0) {
                continue;
            }
            
            // Validate OHLC relationships
            if (high_val < low_val || high_val < open_val || high_val < close_val ||
                low_val > open_val || low_val > close_val) {
                logger_->warn("Invalid OHLC relationships at index {}, skipping", i);
                continue;
            }
            
            OHLC ohlc;
            ohlc.symbol = symbol;
            
            // Handle timestamp conversion safely
            try {
                if (timestamps[i].is_number()) {
                    time_t ts = timestamps[i].get<time_t>();
                    ohlc.timestamp = std::chrono::system_clock::from_time_t(ts);
                } else if (timestamps[i].is_string()) {
                    // Try parsing as string timestamp
                    std::string ts_str = timestamps[i].get<std::string>();
                    ohlc.timestamp = parse_yfinance_timestamp(ts_str);
                } else {
                    continue; // Skip invalid timestamp
                }
            } catch (...) {
                logger_->warn("Failed to parse timestamp at index {}, skipping", i);
                continue;
            }
            
            ohlc.open = open_val;
            ohlc.high = high_val;
            ohlc.low = low_val;
            ohlc.close = close_val;
            ohlc.volume = safe_to_int64(volumes[i], 0);
            
            data.push_back(ohlc);
        }
        
        // Sort by timestamp (oldest first)
        std::sort(data.begin(), data.end(), [](const OHLC& a, const OHLC& b) {
            return a.timestamp < b.timestamp;
        });
        
    } catch (const std::exception& e) {
        logger_->error("Error parsing yfinance response: {}", e.what());
    }
    
    return data;
}

Timestamp YFinanceHandler::parse_yfinance_timestamp(const std::string& timestamp_str) const {
    // First try parsing as Unix timestamp (number)
    try {
        // Check if it's a pure number (Unix timestamp)
        if (!timestamp_str.empty() && std::all_of(timestamp_str.begin(), timestamp_str.end(), 
                                                   [](char c) { return std::isdigit(c); })) {
            time_t epoch = std::stoll(timestamp_str);
            return std::chrono::system_clock::from_time_t(epoch);
        }
    } catch (const std::exception&) {
        // If not a number, try parsing as date string
    }
    
    std::tm tm = {};
    std::memset(&tm, 0, sizeof(tm)); // Initialize to zero
    std::istringstream ss(timestamp_str);
    
    // Try parsing date-time format first (YYYY-MM-DD HH:MM:SS)
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) {
        // Try parsing date-only format (YYYY-MM-DD)
        ss.clear();
        ss.str(timestamp_str);
        ss.seekg(0);
        ss >> std::get_time(&tm, "%Y-%m-%d");
    }
    
    if (ss.fail()) {
        // Try alternative format (YYYY/MM/DD)
        ss.clear();
        ss.str(timestamp_str);
        ss.seekg(0);
        ss >> std::get_time(&tm, "%Y/%m/%d");
    }
    
    if (ss.fail()) {
        // If all parsing fails, log error and use current time
        logger_->warn("Failed to parse timestamp '{}', using current time", timestamp_str);
        return std::chrono::system_clock::now();
    }
    
    // Set time to midnight if not specified
    if (tm.tm_hour == 0 && tm.tm_min == 0 && tm.tm_sec == 0) {
        // Already at midnight, good
    }
    
    // Convert to time_t (handles timezone)
    time_t time_val = std::mktime(&tm);
    if (time_val == -1) {
        logger_->warn("mktime failed for timestamp '{}', using current time", timestamp_str);
        return std::chrono::system_clock::now();
    }
    
    return std::chrono::system_clock::from_time_t(time_val);
}

YFinanceHandler::CurlResponse YFinanceHandler::make_http_request_with_retry(const std::string& url) {
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
            // For 429 (Too Many Requests), use longer backoff
            int backoff_seconds;
            if (response.response_code == 429) {
                // Wait longer for rate limit errors: 60, 120, 180 seconds
                backoff_seconds = 60 * attempt;
                logger_->warn("Rate limited (429). Waiting {} seconds before retry (attempt {} of {})...", 
                             backoff_seconds, attempt, max_retry_attempts_);
            } else {
                // Exponential backoff with jitter for other errors
                backoff_seconds = retry_delay_seconds_ * (1 << (attempt - 1));
                int jitter_ms = 100 * attempt; // simple linear jitter
                logger_->info("Retrying in {} seconds (attempt {} of {})...", backoff_seconds, attempt, max_retry_attempts_);
                std::this_thread::sleep_for(std::chrono::milliseconds(jitter_ms));
            }
            std::this_thread::sleep_for(std::chrono::seconds(backoff_seconds));
        }
    }
    
    logger_->error("All retry attempts failed for URL: {}", url);
    return final_response;
}

YFinanceHandler::CurlResponse YFinanceHandler::make_http_request(const std::string& url) {
    CURL* curl = curl_easy_init();
    CurlResponse response;
    
    if (!curl) {
        response.error_message = "Failed to initialize CURL";
        logger_->error(response.error_message);
        return response;
    }
    
    // Use a realistic browser user agent to avoid blocking
    const char* user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    
    // Set up HTTP headers to mimic a real browser request
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json, text/plain, */*");
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate, br");
    headers = curl_slist_append(headers, "Referer: https://finance.yahoo.com/");
    headers = curl_slist_append(headers, "Origin: https://finance.yahoo.com");
    headers = curl_slist_append(headers, "Sec-Fetch-Dest: empty");
    headers = curl_slist_append(headers, "Sec-Fetch-Mode: cors");
    headers = curl_slist_append(headers, "Sec-Fetch-Site: same-site");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    
    // Configure CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_ENCODING, ""); // Enable automatic decompression
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
        logger_->error("CURL request failed for URL {}: {}", url, response.error_message);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.response_code);
        
        if (response.response_code != 200) {
            logger_->warn("HTTP request returned non-200 status: {} for URL: {}", 
                         response.response_code, url);
            if (response.data.size() < 500) {
                logger_->debug("Response body: {}", response.data);
            }
        }
        
        if (detailed_logging_) {
            logger_->debug("HTTP request completed: {} bytes, code: {} for URL: {}", 
                          response.data.size(), response.response_code, url);
        }
    }
    
    // Clean up headers
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

size_t YFinanceHandler::write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
    size_t total_size = size * nmemb;
    response->data.append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Caching methods
bool YFinanceHandler::load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date) {
    return load_from_cache(symbol, start_date, end_date, current_interval_);
}

bool YFinanceHandler::load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date, DataInterval interval) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Create a cache key that includes interval
    std::string cache_key = symbol + "_" + interval_to_yfinance_string(interval);
    
    // Check memory cache first
    auto cache_it = data_cache_.find(cache_key);
    if (cache_it != data_cache_.end() && is_cache_valid(cache_it->second, start_date, end_date, interval)) {
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
            
            logger_->debug("Data loaded from memory cache for {} (interval: {})", symbol, interval_to_yfinance_string(interval));
            return true;
        }
    }
    
    // Check disk cache if enabled
    if (!enable_disk_cache_) {
        return false;
    }
    
    std::string cache_filename = get_cache_filename(symbol, "", "", interval);
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
        
        if (cache_age.count() > cache_expiry_hours_) {
            logger_->debug("Cache file expired for {} (interval: {})", symbol, interval_to_yfinance_string(interval));
            cache_file.close();
            std::filesystem::remove(cache_filename);
            return false;
        }
        
        // Verify interval matches
        if (cache_json.contains("interval")) {
            std::string cached_interval_str = cache_json["interval"].get<std::string>();
            if (cached_interval_str != interval_to_yfinance_string(interval)) {
                logger_->debug("Cache interval mismatch for {}: cached={}, requested={}", 
                              symbol, cached_interval_str, interval_to_yfinance_string(interval));
                cache_file.close();
                return false;
            }
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
            // Store in memory cache with interval-aware key
            YFinanceCacheEntry entry;
            entry.data = cached_data;
            entry.cached_at = cached_at;
            entry.start_date = start_date;
            entry.end_date = end_date;
            entry.interval = interval;
            entry.quality = validate_data_quality(cached_data);
            entry.is_valid = true;
            data_cache_[cache_key] = entry;
            
            // Store in symbol data
            symbol_data_[symbol] = cached_data;
            if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
                symbols_.push_back(symbol);
            }
            
            // Add to current data for iteration
            current_data_.insert(current_data_.end(), cached_data.begin(), cached_data.end());
            std::sort(current_data_.begin(), current_data_.end(), 
                      [](const OHLC& a, const OHLC& b) { return a.timestamp < b.timestamp; });
            
            logger_->debug("Data loaded from disk cache for {} (interval: {})", symbol, interval_to_yfinance_string(interval));
            return true;
        }
        
    } catch (const std::exception& e) {
        logger_->warn("Failed to load cache file for {} (interval: {}): {}", symbol, interval_to_yfinance_string(interval), e.what());
        cache_file.close();
        // Don't remove cache file on parse error, might be valid for other intervals
    }
    
    return false;
}

void YFinanceHandler::save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                                   const std::string& start_date, const std::string& end_date) {
    save_to_cache(symbol, data, start_date, end_date, current_interval_);
}

void YFinanceHandler::save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                                   const std::string& start_date, const std::string& end_date, DataInterval interval) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Create interval-aware cache key
    std::string cache_key = symbol + "_" + interval_to_yfinance_string(interval);
    
    // Save to memory cache
    YFinanceCacheEntry entry;
    entry.data = data;
    entry.cached_at = std::chrono::system_clock::now();
    entry.start_date = start_date;
    entry.end_date = end_date;
    entry.interval = interval;
    entry.quality = validate_data_quality(data);
    entry.is_valid = true;
    data_cache_[cache_key] = entry;
    
    // Save to disk cache if enabled
    if (!enable_disk_cache_) {
        return;
    }
    
    try {
        std::string cache_filename = get_cache_filename(symbol, start_date, end_date, interval);
        std::ofstream cache_file(cache_filename, std::ios::binary);
        
        if (!cache_file.is_open()) {
            logger_->warn("Failed to create cache file for {} (interval: {})", symbol, interval_to_yfinance_string(interval));
            return;
        }
        
        // Create cache JSON
        nlohmann::json cache_json;
        cache_json["symbol"] = symbol;
        cache_json["start_date"] = "";
        cache_json["end_date"] = "";
        cache_json["interval"] = interval_to_yfinance_string(interval);
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
YFinanceDataQualityMetrics YFinanceHandler::validate_data_quality(const std::vector<OHLC>& data) const {
    YFinanceDataQualityMetrics metrics;
    
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

std::vector<OHLC> YFinanceHandler::clean_and_validate_data(const std::vector<OHLC>& raw_data) const {
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

bool YFinanceHandler::validate_ohlc_data(const OHLC& data) const {
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

std::vector<OHLC> YFinanceHandler::remove_outliers(const std::vector<OHLC>& data, double z_threshold) const {
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

void YFinanceHandler::fill_missing_data(std::vector<OHLC>& data) const {
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

bool YFinanceHandler::detect_data_gaps(const std::vector<OHLC>& data, int max_gap_days) const {
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

void YFinanceHandler::filter_data_by_date_range(
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
        start_time = parse_yfinance_timestamp(start_date);
    }
    
    if (!end_date.empty()) {
        end_time = parse_yfinance_timestamp(end_date);
    }
    
    data.erase(
        std::remove_if(data.begin(), data.end(),
            [start_time, end_time](const OHLC& ohlc) {
                return ohlc.timestamp < start_time || ohlc.timestamp > end_time;
            }),
        data.end()
    );
}

void YFinanceHandler::apply_rate_limit() {
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
    
    // Also enforce minimum interval between requests (100ms)
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time_);
    if (time_since_last.count() < MIN_REQUEST_INTERVAL_MS) {
        auto sleep_time = MIN_REQUEST_INTERVAL_MS - time_since_last.count();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    }
    
    // Record this request
    auto request_time = std::chrono::steady_clock::now();
    request_history_.push_back(request_time);
    last_request_time_ = request_time;
    
    if (detailed_logging_) {
        std::cout << "API request " << request_history_.size() << "/" << MAX_REQUESTS_PER_HOUR 
                  << " in current hour" << std::endl;
    }
}

void YFinanceHandler::cleanup_expired_cache() {
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
                
                if (file_age.count() > cache_expiry_hours_) {
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
nlohmann::json YFinanceHandler::get_api_usage_stats() const {
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

nlohmann::json YFinanceHandler::get_cache_statistics() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    nlohmann::json stats;
    stats["memory_cache_size"] = data_cache_.size();
    stats["cache_enabled"] = enable_disk_cache_;
    stats["cache_directory"] = cache_directory_;
    stats["cache_expiry_hours"] = cache_expiry_hours_;
    
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

void YFinanceHandler::enable_detailed_logging(bool enable) {
    detailed_logging_ = enable;
    if (enable) {
        logger_->set_level(spdlog::level::debug);
        logger_->info("Detailed logging enabled for YFinance handler");
    } else {
        logger_->set_level(spdlog::level::info);
    }
}

std::string YFinanceHandler::get_cache_filename(const std::string& symbol, const std::string& start_date, const std::string& end_date) const {
    return get_cache_filename(symbol, start_date, end_date, current_interval_);
}

std::string YFinanceHandler::get_cache_filename(const std::string& symbol, const std::string& start_date, const std::string& end_date, DataInterval interval) const {
    (void)start_date; (void)end_date; // Date-agnostic cache: one file per symbol
    std::ostringstream filename;
    filename << cache_directory_ << "/" << symbol << "_" << interval_to_yfinance_string(interval) << ".json";
    return filename.str();
}

bool YFinanceHandler::is_cache_valid(const YFinanceCacheEntry& entry, const std::string& start_date, const std::string& end_date) const {
    return is_cache_valid(entry, start_date, end_date, current_interval_);
}

bool YFinanceHandler::is_cache_valid(const YFinanceCacheEntry& entry, const std::string& /* start_date */, const std::string& /* end_date */, DataInterval interval) const {
    auto now = std::chrono::system_clock::now();
    auto cache_age = std::chrono::duration_cast<std::chrono::hours>(now - entry.cached_at);
    
    // Check if cache has expired
    if (cache_age.count() > cache_expiry_hours_) {
        return false;
    }

    // Ensure interval matches requested interval
    if (entry.interval != interval) {
        return false;
    }

    // Date-agnostic cache: we store full series and filter at read time
    return entry.is_valid;
}

bool YFinanceHandler::validate_json_response(const nlohmann::json& json, std::string& error_message) const {
    if (json.contains("error")) {
        error_message = json["error"].get<std::string>();
        return false;
    }
    return true;
}

// Interval support methods
std::string YFinanceHandler::interval_to_yfinance_string(DataInterval interval) const {
    switch (interval) {
        case DataInterval::ONE_MINUTE: return "1m";
        case DataInterval::FIVE_MINUTES: return "5m";
        case DataInterval::FIFTEEN_MINUTES: return "15m";
        case DataInterval::THIRTY_MINUTES: return "30m";
        case DataInterval::ONE_HOUR: return "1h";
        case DataInterval::ONE_DAY: return "1d";
        default: return "1d";
    }
}

DataInterval YFinanceHandler::string_to_interval(const std::string& interval_str) const {
    if (interval_str == "1m") return DataInterval::ONE_MINUTE;
    if (interval_str == "5m") return DataInterval::FIVE_MINUTES;
    if (interval_str == "15m") return DataInterval::FIFTEEN_MINUTES;
    if (interval_str == "30m") return DataInterval::THIRTY_MINUTES;
    if (interval_str == "1h") return DataInterval::ONE_HOUR;
    if (interval_str == "1d") return DataInterval::ONE_DAY;
    return DataInterval::ONE_DAY; // default
}

std::string YFinanceHandler::get_interval_name(DataInterval interval) const {
    switch (interval) {
        case DataInterval::ONE_MINUTE: return "1 minute";
        case DataInterval::FIVE_MINUTES: return "5 minutes";
        case DataInterval::FIFTEEN_MINUTES: return "15 minutes";
        case DataInterval::THIRTY_MINUTES: return "30 minutes";
        case DataInterval::ONE_HOUR: return "1 hour";
        case DataInterval::ONE_DAY: return "1 day";
        default: return "1 day";
    }
}

// Enhanced data loading with interval support
bool YFinanceHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date,
    DataInterval interval
) {
    // Normalize symbol first (aligned with yahoo-finance2 API)
    std::string normalized_symbol = normalize_symbol(symbol);
    
    std::string interval_name = get_interval_name(interval);
    logger_->info("Loading {} data for symbol: {} from {} to {}", 
                  interval_name, normalized_symbol, start_date, end_date);
    
    // Check cache first (using normalized symbol and specified interval)
    if (load_from_cache(normalized_symbol, start_date, end_date, interval)) {
        logger_->info("Data loaded from cache for {} (interval: {})", normalized_symbol, get_interval_name(interval));
        return true;
    }
    
    // Apply rate limiting
    apply_rate_limit();
    
    // Build URL with specific interval (using normalized symbol)
    std::string url = build_yfinance_url(normalized_symbol, start_date, end_date, interval);
    logger_->info("Fetching data from yfinance for {}: {}", normalized_symbol, url);
    
    // Make HTTP request with retry logic
    CurlResponse response = make_http_request_with_retry(url);
    
    if (response.response_code != 200) {
        logger_->error("Failed to fetch data for {}: HTTP {}", normalized_symbol, response.response_code);
        return false;
    }
    
    if (response.data.empty()) {
        logger_->error("Empty response received for {}", normalized_symbol);
        return false;
    }
    
    logger_->info("Successfully received {} bytes from yfinance for {}", response.data.size(), normalized_symbol);
    
    // Parse response (using normalized symbol)
    std::vector<OHLC> data = parse_yfinance_response(response.data, normalized_symbol);
    
    if (data.empty()) {
        logger_->error("No data parsed for {}", normalized_symbol);
        return false;
    }
    
    logger_->info("Successfully parsed {} data points for {}", data.size(), normalized_symbol);
    
    // Clean and validate data
    data = clean_and_validate_data(data);
    
    // Validate data quality
    YFinanceDataQualityMetrics quality = validate_data_quality(data);
    logger_->info("Data quality for {}: {} - {} (completeness: {:.1f}%, validity: {:.1f}%)", 
                  normalized_symbol, quality.quality_grade, 
                  quality.quality_grade == "A" ? "Excellent" : 
                  quality.quality_grade == "B" ? "Good" : "Fair",
                  quality.completeness_ratio * 100, quality.validity_ratio * 100);
    
    // Filter data by date range
    filter_data_by_date_range(data, start_date, end_date);
    
    if (data.empty()) {
        logger_->warn("No data remaining after date filtering for {}", normalized_symbol);
        return false;
    }
    
    logger_->info("Successfully loaded {} data points for {} (filtered from {} total)", 
                  data.size(), normalized_symbol, data.size());
    
    // Store data (using normalized symbol)
    symbol_data_[normalized_symbol] = data;
    if (std::find(symbols_.begin(), symbols_.end(), normalized_symbol) == symbols_.end()) {
        symbols_.push_back(normalized_symbol);
    }
    
    // Add to current data for iteration
    current_data_.insert(current_data_.end(), data.begin(), data.end());
    std::sort(current_data_.begin(), current_data_.end(),
              [](const OHLC& a, const OHLC& b) { return a.timestamp < b.timestamp; });
    
    // Save to cache (using normalized symbol and interval)
    save_to_cache(normalized_symbol, data, start_date, end_date, interval);
    
    return true;
}

// Symbol normalization (aligned with yahoo-finance2 API)
std::string YFinanceHandler::normalize_symbol(const std::string& symbol) const {
    if (symbol.empty()) {
        return symbol;
    }
    
    std::string normalized = symbol;
    
    // Trim whitespace and convert to uppercase
    size_t first = normalized.find_first_not_of(" \t\n\r");
    if (first != std::string::npos) {
        normalized.erase(0, first);
    } else {
        normalized.clear();
        return normalized;
    }
    
    size_t last = normalized.find_last_not_of(" \t\n\r");
    if (last != std::string::npos) {
        normalized.erase(last + 1);
    }
    
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
    
    // If already has .NS or .BO suffix, return as is
    if (normalized.length() >= 3) {
        std::string suffix = normalized.substr(normalized.length() - 3);
        if (suffix == ".NS" || suffix == ".BO") {
            return normalized;
        }
    }
    
    // For symbols without suffix, assume US market (or could default to NSE for Indian stocks)
    // This is a heuristic - in production, you might want a symbol mapping database
    return normalized;
}

bool YFinanceHandler::is_valid_symbol(const std::string& symbol) const {
    if (symbol.empty()) {
        return false;
    }
    
    std::string normalized = normalize_symbol(symbol);
    
    // Check for Indian market suffixes
    if (normalized.length() >= 3) {
        std::string suffix = normalized.substr(normalized.length() - 3);
        if (suffix == ".NS" || suffix == ".BO") {
            std::string base = normalized.substr(0, normalized.length() - 3);
            // Base symbol should be alphanumeric and at least 1 character
            return !base.empty() && std::all_of(base.begin(), base.end(), 
                [](char c) { return std::isalnum(c); });
        }
    }
    
    // US stocks: alphanumeric, at least 1 character, max reasonable length (10)
    return normalized.length() >= 1 && normalized.length() <= 10 &&
           std::all_of(normalized.begin(), normalized.end(), 
               [](char c) { return std::isalnum(c); });
}

bool YFinanceHandler::is_indian_market_symbol(const std::string& symbol) const {
    std::string normalized = normalize_symbol(symbol);
    if (normalized.length() >= 3) {
        std::string suffix = normalized.substr(normalized.length() - 3);
        return suffix == ".NS" || suffix == ".BO";
    }
    return false;
}

bool YFinanceHandler::is_us_market_symbol(const std::string& symbol) const {
    return !is_indian_market_symbol(symbol) && is_valid_symbol(symbol);
}

// Safe type conversion helpers
double YFinanceHandler::safe_to_double(const nlohmann::json& value, double fallback) const {
    if (value.is_null()) {
        return fallback;
    }
    
    if (value.is_number()) {
        double result = value.get<double>();
        return std::isnan(result) ? fallback : result;
    }
    
    if (value.is_string()) {
        try {
            double result = std::stod(value.get<std::string>());
            return std::isnan(result) ? fallback : result;
        } catch (...) {
            return fallback;
        }
    }
    
    return fallback;
}

long long YFinanceHandler::safe_to_int64(const nlohmann::json& value, long long fallback) const {
    if (value.is_null()) {
        return fallback;
    }
    
    if (value.is_number()) {
        return static_cast<long long>(value.get<double>());
    }
    
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    
    return fallback;
}

// Fetch quote data (real-time/delayed)
bool YFinanceHandler::fetch_quote(const std::string& symbol, QuoteData& quote) {
    std::string normalized_symbol = normalize_symbol(symbol);
    
    // Use the quote endpoint from Yahoo Finance
    std::ostringstream url;
    url << "https://query1.finance.yahoo.com/v8/finance/chart/" << normalized_symbol;
    url << "?interval=1d&range=1d&includePrePost=false";
    
    apply_rate_limit();
    
    auto response = make_http_request_with_retry(url.str());
    
    if (response.response_code != 200 || response.data.empty()) {
        logger_->error("Failed to fetch quote for {}: HTTP {}", normalized_symbol, response.response_code);
        return false;
    }
    
    try {
        nlohmann::json json = nlohmann::json::parse(response.data);
        
        // Check for errors
        if (json.contains("chart") && json["chart"].contains("error")) {
            logger_->error("YFinance API error for {}: {}", normalized_symbol, 
                          json["chart"]["error"].dump());
            return false;
        }
        
        if (!json.contains("chart") || !json["chart"].contains("result") || 
            json["chart"]["result"].empty()) {
            logger_->error("Invalid quote response structure for {}", normalized_symbol);
            return false;
        }
        
        auto result = json["chart"]["result"][0];
        auto meta = result["meta"];
        
        // Extract quote data
        quote.symbol = meta.value("symbol", normalized_symbol);
        quote.price = safe_to_double(meta.value("regularMarketPrice", nlohmann::json()), 
                                     safe_to_double(meta.value("previousClose", nlohmann::json()), 0.0));
        quote.previous_close = safe_to_double(meta.value("previousClose", nlohmann::json()), quote.price);
        quote.change = quote.price - quote.previous_close;
        quote.change_percent = quote.previous_close > 0 ? (quote.change / quote.previous_close) * 100.0 : 0.0;
        quote.volume = safe_to_int64(meta.value("regularMarketVolume", nlohmann::json()), 0);
        quote.high = safe_to_double(meta.value("regularMarketDayHigh", nlohmann::json()), 
                                   safe_to_double(meta.value("dayHigh", nlohmann::json()), quote.price));
        quote.low = safe_to_double(meta.value("regularMarketDayLow", nlohmann::json()), 
                                  safe_to_double(meta.value("dayLow", nlohmann::json()), quote.price));
        quote.open = safe_to_double(meta.value("regularMarketOpen", nlohmann::json()), 
                                   safe_to_double(meta.value("open", nlohmann::json()), quote.price));
        
        // Extract metadata
        if (meta.contains("currency")) {
            quote.currency = meta["currency"].get<std::string>();
        }
        if (meta.contains("exchangeName")) {
            quote.exchange = meta["exchangeName"].get<std::string>();
        } else if (meta.contains("exchange")) {
            quote.exchange = meta["exchange"].get<std::string>();
        }
        if (meta.contains("longName")) {
            quote.name = meta["longName"].get<std::string>();
        } else if (meta.contains("shortName")) {
            quote.name = meta["shortName"].get<std::string>();
        }
        if (meta.contains("sector")) {
            quote.sector = meta["sector"].get<std::string>();
        }
        
        return true;
    } catch (const std::exception& e) {
        logger_->error("Error parsing quote response for {}: {}", normalized_symbol, e.what());
        return false;
    }
}

// Fetch multiple quotes
bool YFinanceHandler::fetch_quotes(const std::vector<std::string>& symbols, std::vector<QuoteData>& quotes) {
    quotes.clear();
    quotes.reserve(symbols.size());
    
    // Fetch quotes in batches to avoid rate limiting
    const size_t batch_size = 10;
    
    for (size_t i = 0; i < symbols.size(); i += batch_size) {
        size_t end = std::min(i + batch_size, symbols.size());
        
        for (size_t j = i; j < end; ++j) {
            QuoteData quote;
            if (fetch_quote(symbols[j], quote)) {
                quotes.push_back(quote);
            }
            
            // Small delay between requests
            if (j < end - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Delay between batches
        if (i + batch_size < symbols.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    
    return !quotes.empty();
}

} // namespace backtesting
