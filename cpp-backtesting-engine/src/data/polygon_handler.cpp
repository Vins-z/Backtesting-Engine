#include "data/polygon_handler.h"
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

PolygonHandler::PolygonHandler(
    const std::string& api_key,
    const std::string& cache_dir,
    bool enable_disk_cache,
    int cache_expiry_hours
) : api_key_(api_key),
    base_url_("https://api.polygon.io"),
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
            logger_->info("Polygon cache directory initialized: {}", cache_directory_);
        } catch (const std::exception& e) {
            logger_->warn("Failed to create cache directory: {}", e.what());
            enable_disk_cache_ = false;
        }
    }
    
    logger_->info("Polygon handler initialized (API key: {})", api_key_.empty() ? "not set" : "set");
}

PolygonHandler::~PolygonHandler() {
    curl_global_cleanup();
}

void PolygonHandler::initialize_logging() {
    try {
        logger_ = spdlog::get("polygon");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("polygon");
        }
    } catch (...) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("polygon", console_sink);
        spdlog::register_logger(logger_);
    }
}

bool PolygonHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) {
    try {
        logger_->info("Loading Polygon data for symbol: {} from {} to {}", symbol, start_date, end_date);
        
        if (api_key_.empty()) {
            logger_->error("Polygon API key is required");
            return false;
        }
        
        // Check cache first
        if (load_from_cache(symbol, start_date, end_date)) {
            logger_->info("Data loaded from cache for {}", symbol);
            cache_hits_++;
            return true;
        }
        
        cache_misses_++;
        
        // Apply rate limiting
        apply_rate_limit();
        
        // Fetch data from Polygon API
        std::string url = build_polygon_stocks_url(symbol, start_date, end_date);
        logger_->info("Fetching data from Polygon for {}: {}", symbol, url);
        auto response = make_http_request_with_retry(url);
        
        if (response.data.empty() || response.response_code != 200) {
            logger_->error("Failed to fetch data for symbol: {} (HTTP {})", symbol, response.response_code);
            failed_requests_++;
            return false;
        }
        
        // Parse the JSON response
        std::vector<OHLC> raw_data = parse_polygon_stocks_response(response.data, symbol);
        
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

bool PolygonHandler::load_options_chain(const std::string& symbol, const std::string& expiry_date) {
    try {
        logger_->info("Loading Polygon options chain for symbol: {} (expiry: {})", symbol, expiry_date);
        
        if (api_key_.empty()) {
            logger_->error("Polygon API key is required for options data");
            return false;
        }
        
        // Apply rate limiting
        apply_rate_limit();
        
        // Fetch options chain from Polygon API
        std::string url = build_polygon_options_url(symbol, expiry_date);
        logger_->info("Fetching options chain from Polygon: {}", url);
        auto response = make_http_request_with_retry(url);
        
        if (response.data.empty() || response.response_code != 200) {
            logger_->error("Failed to fetch options chain for symbol: {} (HTTP {})", symbol, response.response_code);
            return false;
        }
        
        // Parse the options response
        std::vector<OptionsData> options = parse_polygon_options_response(response.data);
        
        if (options.empty()) {
            logger_->warn("No options data parsed for symbol: {}", symbol);
            return false;
        }
        
        logger_->info("Successfully parsed {} options contracts for {}", options.size(), symbol);
        
        // Store options data
        std::string key = expiry_date.empty() ? symbol : symbol + "_" + expiry_date;
        options_data_[key] = options;
        
        return true;
        
    } catch (const std::exception& e) {
        logger_->error("Error loading options chain for {}: {}", symbol, e.what());
        return false;
    }
}

std::vector<OptionsData> PolygonHandler::get_options_chain(const std::string& symbol, const std::string& expiry_date) const {
    std::string key = expiry_date.empty() ? symbol : symbol + "_" + expiry_date;
    auto it = options_data_.find(key);
    if (it != options_data_.end()) {
        return it->second;
    }
    return {};
}

bool PolygonHandler::has_next() const {
    return current_index_ < current_data_.size();
}

OHLC PolygonHandler::get_next() {
    if (!has_next()) {
        return OHLC{};
    }
    
    OHLC data = current_data_[current_index_];
    current_index_++;
    return data;
}

void PolygonHandler::reset() {
    current_index_ = 0;
}

std::vector<std::string> PolygonHandler::get_symbols() const {
    return symbols_;
}

std::vector<OHLC> PolygonHandler::get_historical_data(const std::string& symbol) const {
    auto it = symbol_data_.find(symbol);
    if (it != symbol_data_.end()) {
        return it->second;
    }
    return {};
}

void PolygonHandler::apply_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time_).count();
    
    if (elapsed < MIN_REQUEST_INTERVAL_MS) {
        int sleep_ms = MIN_REQUEST_INTERVAL_MS - static_cast<int>(elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    
    last_request_time_ = std::chrono::steady_clock::now();
}

PolygonHandler::CurlResponse PolygonHandler::make_http_request(const std::string& url) {
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

PolygonHandler::CurlResponse PolygonHandler::make_http_request_with_retry(const std::string& url) {
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

size_t PolygonHandler::write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
    size_t total_size = size * nmemb;
    response->data.append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string PolygonHandler::build_polygon_stocks_url(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date
) const {
    std::ostringstream url;
    
    // Polygon API endpoint for aggregates (bars)
    // Format: /v2/aggs/ticker/{ticker}/range/{multiplier}/{timespan}/{from}/{to}
    url << base_url_ << "/v2/aggs/ticker/" << symbol << "/range/1/day/";
    
    // Convert dates to format YYYY-MM-DD
    url << start_date << "/" << end_date;
    url << "?apiKey=" << api_key_;
    
    return url.str();
}

std::string PolygonHandler::build_polygon_options_url(
    const std::string& symbol,
    const std::string& expiry_date
) const {
    std::ostringstream url;
    
    // Polygon API endpoint for options chain
    // Format: /v3/snapshot/options/{underlying}
    url << base_url_ << "/v3/snapshot/options/" << symbol;
    
    if (!expiry_date.empty()) {
        url << "?expiration_date=" << expiry_date;
    }
    
    url << (expiry_date.empty() ? "?" : "&") << "apiKey=" << api_key_;
    
    return url.str();
}

std::vector<OHLC> PolygonHandler::parse_polygon_stocks_response(const std::string& response, const std::string& symbol) const {
    std::vector<OHLC> data;
    
    try {
        nlohmann::json json = nlohmann::json::parse(response);
        
        if (json.contains("status") && json["status"] != "OK") {
            logger_->error("Polygon API error: {}", json.value("status", "Unknown error"));
            return data;
        }
        
        if (!json.contains("results") || !json["results"].is_array()) {
            logger_->error("No results array in Polygon response");
            return data;
        }
        
        for (const auto& item : json["results"]) {
            OHLC ohlc;
            ohlc.symbol = symbol;
            
            // Polygon returns timestamp in milliseconds
            int64_t timestamp_ms = item.value("t", 0LL);
            ohlc.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp_ms));
            
            ohlc.open = item.value("o", 0.0);
            ohlc.high = item.value("h", 0.0);
            ohlc.low = item.value("l", 0.0);
            ohlc.close = item.value("c", 0.0);
            ohlc.volume = item.value("v", 0.0);
            
            data.push_back(ohlc);
        }
        
    } catch (const std::exception& e) {
        logger_->error("Error parsing Polygon stocks response: {}", e.what());
    }
    
    return data;
}

std::vector<OptionsData> PolygonHandler::parse_polygon_options_response(const std::string& response) const {
    std::vector<OptionsData> options;
    
    try {
        nlohmann::json json = nlohmann::json::parse(response);
        
        if (json.contains("status") && json["status"] != "OK") {
            logger_->error("Polygon API error: {}", json.value("status", "Unknown error"));
            return options;
        }
        
        if (!json.contains("results") || !json["results"].is_array()) {
            logger_->error("No results array in Polygon options response");
            return options;
        }
        
        for (const auto& item : json["results"]) {
            OptionsData opt;
            
            if (item.contains("details")) {
                auto details = item["details"];
                opt.symbol = details.value("ticker", "");
                opt.expiry = details.value("expiration_date", "");
                opt.strike = details.value("strike_price", 0.0);
                opt.option_type = details.value("contract_type", "");
            }
            
            if (item.contains("last_quote")) {
                auto quote = item["last_quote"];
                opt.bid = quote.value("bid", 0.0);
                opt.ask = quote.value("ask", 0.0);
            }
            
            if (item.contains("last_trade")) {
                auto trade = item["last_trade"];
                opt.last = trade.value("price", 0.0);
            }
            
            opt.volume = item.value("day_volume", 0.0);
            opt.open_interest = item.value("open_interest", 0.0);
            opt.implied_volatility = item.value("implied_volatility", 0.0);
            
            // Greeks (if available)
            if (item.contains("greeks")) {
                auto greeks = item["greeks"];
                opt.delta = greeks.value("delta", 0.0);
                opt.gamma = greeks.value("gamma", 0.0);
                opt.theta = greeks.value("theta", 0.0);
                opt.vega = greeks.value("vega", 0.0);
            }
            
            options.push_back(opt);
        }
        
    } catch (const std::exception& e) {
        logger_->error("Error parsing Polygon options response: {}", e.what());
    }
    
    return options;
}

bool PolygonHandler::load_from_cache(const std::string& symbol, const std::string& start_date, const std::string& end_date) {
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

            // Validate requested date range against cache metadata to avoid returning mismatched data.
            if (cache_json.contains("start_date") && cache_json["start_date"].is_string() &&
                cache_json.contains("end_date") && cache_json["end_date"].is_string()) {
                const std::string cached_start = cache_json["start_date"].get<std::string>();
                const std::string cached_end = cache_json["end_date"].get<std::string>();
                if ((!start_date.empty() && cached_start != start_date) ||
                    (!end_date.empty() && cached_end != end_date)) {
                    return false;
                }
            }
            
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

void PolygonHandler::save_to_cache(const std::string& symbol, const std::vector<OHLC>& data, 
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

bool PolygonHandler::is_cache_valid(const std::string& symbol) const {
    auto it = cache_timestamps_.find(symbol);
    if (it == cache_timestamps_.end()) {
        return false;
    }
    
    auto age = std::chrono::duration_cast<std::chrono::hours>(
        std::chrono::system_clock::now() - it->second).count();
    
    return age < cache_expiry_hours_;
}

std::string PolygonHandler::get_cache_filename(const std::string& symbol) const {
    std::string safe_symbol = symbol;
    std::replace(safe_symbol.begin(), safe_symbol.end(), '/', '_');
    std::replace(safe_symbol.begin(), safe_symbol.end(), '\\', '_');
    return cache_directory_ + "/" + safe_symbol + ".json";
}

Timestamp PolygonHandler::parse_polygon_timestamp(const std::string& timestamp_str) const {
    // Polygon format: "2023-01-15" or Unix timestamp
    try {
        int64_t timestamp_ms = std::stoll(timestamp_str);
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp_ms));
    } catch (...) {
        // Try date format
        std::tm tm = {};
        std::istringstream ss(timestamp_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        if (ss.fail()) {
            return std::chrono::system_clock::now();
        }
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
}

nlohmann::json PolygonHandler::get_api_usage_stats() const {
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

