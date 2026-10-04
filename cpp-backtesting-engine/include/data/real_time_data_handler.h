#pragma once

#include "data/data_handler.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <thread>
#include <atomic>

namespace backtesting {

class RealTimeDataHandler : public DataHandler {
public:
    RealTimeDataHandler(const std::string& api_key = "", 
                        const std::string& base_url = "https://query1.finance.yahoo.com");
    ~RealTimeDataHandler() override;

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
    std::string get_source_name() const override { return "real_time"; }

    bool load_data(const std::string& symbol, const std::string& period = "1y");
    bool load_data_alpha_vantage(const std::string& symbol);
    bool load_multiple_symbols(const std::vector<std::string>& symbols);
    void start_streaming();
    void stop_streaming();
    bool get_next_data(std::string& symbol, OHLC& data);
    bool has_more_data() const;
    OHLC get_latest_data(const std::string& symbol) const;
    OHLC get_real_time_quote(const std::string& symbol);

private:
    std::string api_key_;
    std::string base_url_;
    std::unordered_map<std::string, std::vector<OHLC>> data_;
    std::unordered_map<std::string, size_t> current_indices_;
    std::queue<std::pair<std::string, OHLC>> data_queue_;
    std::atomic<bool> is_streaming_{false};
    std::thread streaming_thread_;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data);
    std::string make_request(const std::string& url);
    bool parse_yahoo_response(const std::string& json_data, const std::string& symbol);
    bool parse_alpha_vantage_response(const std::string& json_data, const std::string& symbol);
    void streaming_worker();
};

} // namespace backtesting
