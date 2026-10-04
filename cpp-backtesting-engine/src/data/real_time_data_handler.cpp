#include "data/real_time_data_handler.h"
#include "common/time_utils.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <ctime>

namespace backtesting {

size_t RealTimeDataHandler::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    data->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string RealTimeDataHandler::make_request(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response_data;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            spdlog::error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
            return "";
        }
    }
    return response_data;
}

bool RealTimeDataHandler::parse_yahoo_response(const std::string& json_data, const std::string& symbol) {
    try {
        auto json = nlohmann::json::parse(json_data);
        
        if (json.contains("chart") && json["chart"].contains("result") && 
            !json["chart"]["result"].empty()) {
            
            auto result = json["chart"]["result"][0];
            if (!result.contains("timestamp") || !result.contains("indicators")) {
                spdlog::error("Invalid Yahoo Finance response format");
                return false;
            }
            
            auto timestamps = result["timestamp"];
            auto quotes = result["indicators"]["quote"][0];
            std::vector<OHLC> ohlc_data;
            
            for (size_t i = 0; i < timestamps.size(); ++i) {
                OHLC ohlc;
                ohlc.symbol = symbol;
                std::time_t sec = static_cast<std::time_t>(timestamps[i].get<uint64_t>());
                ohlc.timestamp = std::chrono::system_clock::from_time_t(sec);
                ohlc.open = quotes["open"][i].is_null() ? 0.0 : quotes["open"][i].get<double>();
                ohlc.high = quotes["high"][i].is_null() ? 0.0 : quotes["high"][i].get<double>();
                ohlc.low = quotes["low"][i].is_null() ? 0.0 : quotes["low"][i].get<double>();
                ohlc.close = quotes["close"][i].is_null() ? 0.0 : quotes["close"][i].get<double>();
                ohlc.volume = quotes["volume"][i].is_null() ? 0.0 : quotes["volume"][i].get<double>();
                
                if (ohlc.open > 0 && ohlc.high > 0 && ohlc.low > 0 && ohlc.close > 0) {
                    ohlc_data.push_back(ohlc);
                }
            }
            
            data_[symbol] = std::move(ohlc_data);
            current_indices_[symbol] = 0;
            spdlog::info("Loaded {} data points for symbol {}", data_[symbol].size(), symbol);
            return true;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error parsing Yahoo Finance response: {}", e.what());
    }
    return false;
}

bool RealTimeDataHandler::parse_alpha_vantage_response(const std::string& json_data, const std::string& symbol) {
    try {
        auto json = nlohmann::json::parse(json_data);
        
        if (json.contains("Error Message")) {
            spdlog::error("Alpha Vantage API Error: {}", json["Error Message"].get<std::string>());
            return false;
        }
        if (json.contains("Note")) {
            spdlog::warn("Alpha Vantage API Note: {}", json["Note"].get<std::string>());
            return false;
        }
        
        if (json.contains("Time Series (Daily)")) {
            auto time_series = json["Time Series (Daily)"];
            std::vector<OHLC> ohlc_data;
            
            for (auto& [date, data] : time_series.items()) {
                OHLC ohlc;
                ohlc.symbol = symbol;
                ohlc.timestamp = parse_utc_timestamp_or(date, std::chrono::system_clock::time_point{});
                ohlc.open = std::stod(data["1. open"].get<std::string>());
                ohlc.high = std::stod(data["2. high"].get<std::string>());
                ohlc.low = std::stod(data["3. low"].get<std::string>());
                ohlc.close = std::stod(data["4. close"].get<std::string>());
                ohlc.volume = std::stod(data["5. volume"].get<std::string>());
                ohlc_data.push_back(ohlc);
            }
            
            std::sort(ohlc_data.begin(), ohlc_data.end(), 
                      [](const OHLC& a, const OHLC& b) {
                          return a.timestamp < b.timestamp;
                      });
            
            data_[symbol] = std::move(ohlc_data);
            current_indices_[symbol] = 0;
            spdlog::info("Loaded {} data points for symbol {}", data_[symbol].size(), symbol);
            return true;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error parsing Alpha Vantage response: {}", e.what());
    }
    return false;
}

void RealTimeDataHandler::streaming_worker() {
    while (is_streaming_) {
        for (auto& [symbol, data] : data_) {
            if (current_indices_[symbol] < data.size()) {
                data_queue_.push({symbol, data[current_indices_[symbol]]});
                current_indices_[symbol]++;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

RealTimeDataHandler::RealTimeDataHandler(const std::string& api_key, const std::string& base_url)
    : api_key_(api_key), base_url_(base_url), is_streaming_(false) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

RealTimeDataHandler::~RealTimeDataHandler() {
    stop_streaming();
    curl_global_cleanup();
}

bool RealTimeDataHandler::load_symbol_data(
    const std::string& symbol,
    const std::string& /*start_date*/,
    const std::string& /*end_date*/
) {
    return load_data(symbol);
}

bool RealTimeDataHandler::load_data(const std::string& symbol, const std::string& /*period*/) {
    std::string url = base_url_ + "/v8/finance/chart/" + symbol + 
                     "?period1=0&period2=9999999999&interval=1d&includePrePost=true&events=div%7Csplit%7Cearn";
    std::string response = make_request(url);
    if (response.empty()) {
        spdlog::error("Failed to fetch data for symbol: {}", symbol);
        return false;
    }
    return parse_yahoo_response(response, symbol);
}

bool RealTimeDataHandler::load_data_alpha_vantage(const std::string& symbol) {
    if (api_key_.empty()) {
        spdlog::error("Alpha Vantage API key not provided");
        return false;
    }
    std::string url = "https://www.alphavantage.co/query?function=TIME_SERIES_DAILY_ADJUSTED&symbol=" + 
                     symbol + "&apikey=" + api_key_ + "&outputsize=full";
    std::string response = make_request(url);
    if (response.empty()) {
        spdlog::error("Failed to fetch data from Alpha Vantage for symbol: {}", symbol);
        return false;
    }
    return parse_alpha_vantage_response(response, symbol);
}

bool RealTimeDataHandler::load_multiple_symbols(const std::vector<std::string>& symbols) {
    bool success = true;
    for (const auto& symbol : symbols) {
        bool loaded = load_data(symbol);
        if (!loaded) {
            loaded = load_data_alpha_vantage(symbol);
        }
        success = success && loaded;
    }
    return success;
}

void RealTimeDataHandler::start_streaming() {
    if (!is_streaming_) {
        is_streaming_ = true;
        streaming_thread_ = std::thread(&RealTimeDataHandler::streaming_worker, this);
        spdlog::info("Started real-time data streaming");
    }
}

void RealTimeDataHandler::stop_streaming() {
    if (is_streaming_) {
        is_streaming_ = false;
        if (streaming_thread_.joinable()) {
            if (std::this_thread::get_id() == streaming_thread_.get_id()) {
                streaming_thread_.detach();
            } else {
                streaming_thread_.join();
            }
        }
        spdlog::info("Stopped real-time data streaming");
    }
}

bool RealTimeDataHandler::get_next_data(std::string& symbol, OHLC& data) {
    if (data_queue_.empty()) {
        return false;
    }
    auto front = data_queue_.front();
    data_queue_.pop();
    symbol = front.first;
    data = front.second;
    return true;
}

bool RealTimeDataHandler::has_more_data() const {
    return !data_queue_.empty() || is_streaming_;
}

bool RealTimeDataHandler::has_next() const {
    return has_more_data();
}

OHLC RealTimeDataHandler::get_next() {
    std::string sym;
    OHLC data{};
    if (get_next_data(sym, data)) {
        return data;
    }
    return OHLC{};
}

void RealTimeDataHandler::reset() {
    for (auto& [symbol, index] : current_indices_) {
        index = 0;
    }
    while (!data_queue_.empty()) {
        data_queue_.pop();
    }
}

OHLC RealTimeDataHandler::get_latest_data(const std::string& symbol) const {
    auto it = data_.find(symbol);
    if (it != data_.end() && !it->second.empty()) {
        return it->second.back();
    }
    return OHLC{};
}

std::vector<OHLC> RealTimeDataHandler::get_historical_data(const std::string& symbol) const {
    auto it = data_.find(symbol);
    if (it != data_.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> RealTimeDataHandler::get_symbols() const {
    std::vector<std::string> symbols;
    for (const auto& [symbol, data] : data_) {
        symbols.push_back(symbol);
    }
    return symbols;
}

OHLC RealTimeDataHandler::get_real_time_quote(const std::string& symbol) {
    std::string url = base_url_ + "/v8/finance/chart/" + symbol + 
                     "?range=1d&interval=1m&includePrePost=true";
    std::string response = make_request(url);
    if (!response.empty()) {
        try {
            auto json = nlohmann::json::parse(response);
            if (json.contains("chart") && json["chart"].contains("result") && 
                !json["chart"]["result"].empty()) {
                auto result = json["chart"]["result"][0];
                auto meta = result["meta"];
                OHLC quote;
                quote.symbol = symbol;
                quote.timestamp = std::chrono::system_clock::now();
                quote.open = meta.value("regularMarketOpen", 0.0);
                quote.high = meta.value("regularMarketDayHigh", 0.0);
                quote.low = meta.value("regularMarketDayLow", 0.0);
                quote.close = meta.value("regularMarketPrice", 0.0);
                quote.volume = meta.value("regularMarketVolume", 0.0);
                return quote;
            }
        } catch (const std::exception& e) {
            spdlog::error("Error parsing real-time quote: {}", e.what());
        }
    }
    return OHLC{};
}

} // namespace backtesting