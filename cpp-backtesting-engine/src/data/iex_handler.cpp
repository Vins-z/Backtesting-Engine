#include "data/iex_handler.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <curl/curl.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace backtesting {

IEXHandler::IEXHandler(
    const std::string& api_key,
    const std::string& cache_dir,
    bool enable_disk_cache,
    int cache_expiry_hours
) : api_key_(api_key),
    base_url_("https://cloud.iexapis.com/stable"),
    cache_directory_(cache_dir),
    enable_disk_cache_(enable_disk_cache),
    cache_expiry_hours_(cache_expiry_hours),
    current_index_(0),
    last_request_time_(std::chrono::steady_clock::now() - std::chrono::milliseconds(MIN_REQUEST_INTERVAL_MS)) {
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    initialize_logging();
    
    if (enable_disk_cache_) {
        try {
            std::filesystem::create_directories(cache_directory_);
            logger_->info("IEX cache directory initialized: {}", cache_directory_);
        } catch (const std::exception& e) {
            logger_->warn("Failed to create cache directory: {}", e.what());
            enable_disk_cache_ = false;
        }
    }
    
    logger_->info("IEX handler initialized (API key: {})", api_key_.empty() ? "not set (using free tier)" : "set");
}

IEXHandler::~IEXHandler() {
    curl_global_cleanup();
}

void IEXHandler::initialize_logging() {
    try {
        logger_ = spdlog::get("iex");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("iex");
        }
    } catch (...) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("iex", console_sink);
        spdlog::register_logger(logger_);
    }
}

bool IEXHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) {
    try {
        logger_->info("Loading IEX data for symbol: {} from {} to {}", symbol, start_date, end_date);
        
        // Check cache first
        if (load_from_cache(symbol, start_date, end_date)) {
            logger_->info("Data loaded from cache for {}", symbol);
            cache_hits_++;
            return true;
        }
        
        cache_misses_++;
        
        // Apply rate limiting
        apply_rate_limit();
        
        // Fetch data from IEX API
        std::string url = build_iex_url(symbol, start_date, end_date);
        logger_->info("Fetching data from IEX for {}: {}", symbol, url);
        auto response = make_http_request_with_retry(url);
        
        if (response.data.empty() || response.response_code != 200) {
            logger_->error("Failed to fetch data for symbol: {} (HTTP {})", symbol, response.response_code);
            failed_requests_++;
            return false;
        }
        
        // Parse the JSON response
        std::vector<OHLC> raw_data = parse_iex_response(response.data, symbol);
        
        if (raw_data.empty()) {
            logger_->error("No data parsed for symbol: {}", symbol);
            failed_requests_++;
            return false;
        }
        
        logger_->info("Successfully parsed {} data points for {}", raw_data.size(), symbol);
        
        // Store the data
        symbol_data_[symbol] = raw_data;
        if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
            symbols_.push_back(symbol);
        }
        
        // Add to current data for iteration
        current_data_.insert(current_data_.end(), raw_data.begin(), raw_data.end());
        
        // Sort by timestamp
        std::sort(current_data_.begin(), current_data_.end(), 
                  [](const OHLC& a, const OHLC& b) {
                      return a.timestamp < b.timestamp;
                  });
        
        // Save to cache
        save_to_cache(symbol, raw_data, start_date, end_date);
        
        successful_requests_++;
        return true;
        
    } catch (const std::exception& e) {
        logger_->error("Error loading data for {}: {}", symbol, e.what());
        failed_requests_++;
        return false;
    }
}

bool IEXHandler::has_next() const {
    return current_index_ < current_data_.size();
}

OHLC IEXHandler::get_next() {
    if (!has_next()) {
        return OHLC{};
    }
    
    OHLC data = current_data_[current_index_];
    current_index_++;
    return data;
}

void IEXHandler::reset() {
    current_index_ = 0;
}

std::vector<std::string> IEXHandler::get_symbols() const {
    return symbols_;
}

std::vector<OHLC> IEXHandler::get_historical_data(const std::string& symbol) const {
    auto it = symbol_data_.find(symbol);
    if (it != symbol_data_.end()) {
        return it->second;
    }
    return {};
}

void IEXHandler::apply_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time_).count();
    
    if (elapsed < MIN_REQUEST_INTERVAL_MS) {
        int sleep_ms = MIN_REQUEST_INTERVAL_MS - static_cast<int>(elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    
    last_request_time_ = std::chrono::steady_clock::now();
}

IEXHandler::CurlResponse IEXHandler::make_http_request(const std::string& url) {
    CurlResponse response;
    CURL* curl = curl_easy_init();
    
    if (!curl) {
        response.error_message = "Failed to initialize CURL";
        return response;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.response_code);
    }
    
    curl_easy_cleanup(curl);
    return response;
}

IEXHandler::CurlResponse IEXHandler::make_http_request_with_retry(const std::string& url) {
    CurlResponse response;
    for (int attempt = 0; attempt < max_retry_attempts_; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(retry_delay_seconds_ * attempt));
        }
        
        response = make_http_request(url);
        
        if (response.response_code == 200 && !response.data.empty()) {
            return response;
        }
    }
    
    return response;
}

size_t IEXHandler::write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
    size_t total_size = size * nmemb;
    response->data.append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string IEXHandler::build_iex_url(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) const {
    std::ostringstream url;
    
    // IEX Cloud API endpoint for historical data
    // Format: /stock/{symbol}/chart/{range}
    // For date range, we use the 'range' parameter or query params
    
    url << base_url_ << "/stock/" << symbol << "/chart/1y";
    
    if (!api_key_.empty()) {
        url << "?token=" << api_key_;
    }
    
    return url.str();
}

std::vector<OHLC> IEXHandler::parse_iex_response(const std::string& response, const std::string& symbol) const {
    std::vector<OHLC> data;
    
    try {
        nlohmann::json json = nlohmann::json::parse(response);
        
        if (!json.is_array()) {
            logger_->error("IEX response is not an array");
            return data;
        }
        
        for (const auto& item : json) {
            if (!item.contains("date") || !item.contains("close")) {
                continue;
            }
            
            OHLC ohlc;
            ohlc.symbol = symbol;
            ohlc.timestamp = parse_iex_timestamp(item["date"].get<std::string>());
            ohlc.open = item.value("open", item["close"].get<double>());
            ohlc.high = item.value("high", item["close"].get<double>());
            ohlc.low = item.value("low", item["close"].get<double>());
            ohlc.close = item["close"].get<double>();
            ohlc.volume = item.value("volume", 0.0);
            
            data.push_back(ohlc);
        }
        
    } catch (const std::exception& e) {
        logger_->error("Error parsing IEX response: {}", e.what());
    }
    
    return data;
}

Timestamp IEXHandler::parse_iex_timestamp(const std::string& timestamp_str) const {
    // IEX format: "2023-01-15" or "2023-01-15T00:00:00Z"
    std::tm tm = {};
    std::istringstream ss(timestamp_str);
    
    if (timestamp_str.find('T') != std::string::npos) {
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    } else {
        ss >> std::get_time(&tm, "%Y-%m-%d");
    }
    
    if (ss.fail()) {
        return std::chrono::system_clock::now();
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

bool IEXHandler::load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date) {
    if (!enable_disk_cache_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Check memory cache
    auto it = data_cache_.find(symbol);
    if (it != data_cache_.end() && is_cache_valid(symbol)) {
        symbol_data_[symbol] = it->second;
        if (std::find(symbols_.begin(), symbols_.end(), symbol) == symbols_.end()) {
            symbols_.push_back(symbol);
        }
        return true;
    }
    
    // Check disk cache
    std::string cache_file = get_cache_filename(symbol);
    if (std::filesystem::exists(cache_file)) {
        try {
            std::ifstream file(cache_file);
            nlohmann::json cache_json;
            file >> cache_json;
            
            std::vector<OHLC> cached_data;
            for (const auto& item : cache_json["data"]) {
                OHLC ohlc;
                ohlc.symbol = item["symbol"];
                ohlc.timestamp = std::chrono::system_clock::from_time_t(item["timestamp"]);
                ohlc.open = item["open"];
                ohlc.high = item["high"];
                ohlc.low = item["low"];
                ohlc.close = item["close"];
                ohlc.volume = item["volume"];
                cached_data.push_back(ohlc);
            }
            
            if (!cached_data.empty() && is_cache_valid(symbol)) {
                data_cache_[symbol] = cached_data;
                symbol_data_[symbol] = cached_data;
                cache_timestamps_[symbol] = std::chrono::system_clock::now();
                return true;
            }
        } catch (const std::exception& e) {
            logger_->warn("Failed to load cache for {}: {}", symbol, e.what());
        }
    }
    
    return false;
}

void IEXHandler::save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
                               const std::string& start_date, const std::string& end_date) {
    if (!enable_disk_cache_ || data.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Save to memory cache
    data_cache_[symbol] = data;
    cache_timestamps_[symbol] = std::chrono::system_clock::now();
    
    // Save to disk cache
    std::string cache_file = get_cache_filename(symbol);
    try {
        nlohmann::json cache_json;
        cache_json["symbol"] = symbol;
        cache_json["start_date"] = start_date;
        cache_json["end_date"] = end_date;
        cache_json["cached_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        nlohmann::json data_array = nlohmann::json::array();
        for (const auto& ohlc : data) {
            nlohmann::json item;
            item["symbol"] = ohlc.symbol;
            item["timestamp"] = std::chrono::system_clock::to_time_t(ohlc.timestamp);
            item["open"] = ohlc.open;
            item["high"] = ohlc.high;
            item["low"] = ohlc.low;
            item["close"] = ohlc.close;
            item["volume"] = ohlc.volume;
            data_array.push_back(item);
        }
        cache_json["data"] = data_array;
        
        std::ofstream file(cache_file);
        file << cache_json.dump(2);
    } catch (const std::exception& e) {
        logger_->warn("Failed to save cache for {}: {}", symbol, e.what());
    }
}

bool IEXHandler::is_cache_valid(const std::string& symbol) const {
    auto it = cache_timestamps_.find(symbol);
    if (it == cache_timestamps_.end()) {
        return false;
    }
    
    auto age = std::chrono::duration_cast<std::chrono::hours>(
        std::chrono::system_clock::now() - it->second).count();
    
    return age < cache_expiry_hours_;
}

std::string IEXHandler::get_cache_filename(const std::string& symbol) const {
    std::string safe_symbol = symbol;
    std::replace(safe_symbol.begin(), safe_symbol.end(), '/', '_');
    std::replace(safe_symbol.begin(), safe_symbol.end(), '\\', '_');
    return cache_directory_ + "/" + safe_symbol + ".json";
}

nlohmann::json IEXHandler::get_api_usage_stats() const {
    nlohmann::json stats;
    stats["successful_requests"] = successful_requests_;
    stats["failed_requests"] = failed_requests_;
    stats["cache_hits"] = cache_hits_;
    stats["cache_misses"] = cache_misses_;
    stats["cache_hit_rate"] = (cache_hits_ + cache_misses_ > 0) 
        ? static_cast<double>(cache_hits_) / (cache_hits_ + cache_misses_) 
        : 0.0;
    return stats;
}

} // namespace backtesting

