#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>

namespace backtesting {

// Base data handler interface
class DataHandler {
public:
    virtual ~DataHandler() = default;
    
    // Load data for a symbol within date range
    virtual bool load_symbol_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) = 0;
    
    // Check if more data is available
    virtual bool has_next() const = 0;
    
    // Get next market data point
    virtual OHLC get_next() = 0;
    
    // Reset to beginning of data
    virtual void reset() = 0;
    
    // Get all loaded symbols
    virtual std::vector<std::string> get_symbols() const = 0;
    
    // Get historical data for analysis
    virtual std::vector<OHLC> get_historical_data(const std::string& symbol) const = 0;
    
    // Get data source name
    virtual std::string get_source_name() const = 0;
};

// CSV file data handler
class CSVDataHandler : public DataHandler {
private:
    std::string data_path_;
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, std::vector<OHLC>> symbol_data_;
    std::vector<OHLC> current_data_;
    size_t current_index_;
    
public:
    explicit CSVDataHandler(const std::string& data_path);
    
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
    std::string get_source_name() const override { return "CSV"; }
    
private:
    bool load_csv_file(const std::string& filename);
    OHLC parse_csv_line(const std::vector<std::string>& cells, int date_idx, int open_idx, int high_idx, int low_idx, int close_idx, int volume_idx) const;
    Timestamp parse_timestamp(const std::string& date_str) const;
    std::string find_symbol_csv_path(const std::string& symbol) const;
};

// API data handler (Yahoo Finance, Alpha Vantage, etc.)
class APIDataHandler : public DataHandler {
private:
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, std::vector<OHLC>> symbol_data_;
    std::vector<OHLC> current_data_;
    size_t current_index_;
    
public:
    APIDataHandler();
    
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
    std::string get_source_name() const override { return "API"; }
    
private:
    bool fetch_yahoo_finance_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    );
    
    std::string build_yahoo_url(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) const;
    
         std::vector<OHLC> parse_yahoo_response(const std::string& response) const;
     
     // Helper methods for sample data generation
     std::vector<OHLC> generate_sample_data(
         const std::string& symbol,
         const std::string& start_date,
         const std::string& end_date
     ) const;
     
     Timestamp parse_date(const std::string& date_str) const;
};

} // namespace backtesting 