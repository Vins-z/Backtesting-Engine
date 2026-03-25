#pragma once

#include "data/data_handler.h"
#include "data/csv_data_handler.h"
#include "data/api_data_handler.h"
#include "data/alpha_vantage_handler.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace backtesting {

// Multi-source data handler that aggregates data from multiple sources
class MultiSourceDataHandler : public DataHandler {
public:
    MultiSourceDataHandler();
    ~MultiSourceDataHandler() = default;
    
    // Add data source
    void add_data_source(const std::string& name, std::unique_ptr<DataHandler> handler);
    void remove_data_source(const std::string& name);
    
    // Load data from multiple sources
    bool load_symbol_data(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date
    ) override;
    
    // Get data with source priority
    MarketData get_next() override;
    bool has_next() const override;
    
    // Data quality and validation
    bool validate_data_quality(const std::string& symbol) const;
    std::vector<std::string> get_data_gaps(const std::string& symbol) const;
    bool fill_data_gaps(const std::string& symbol);
    
    // Real-time data integration
    void enable_real_time_data(const std::string& symbol, const std::string& source);
    void disable_real_time_data(const std::string& symbol);
    MarketData get_latest_data(const std::string& symbol) const;
    
    // Alternative data sources
    void add_alternative_data(
        const std::string& symbol,
        const std::vector<std::pair<Timestamp, double>>& data,
        const std::string& data_type
    );
    
    // Data fusion and aggregation
    MarketData fuse_data_sources(
        const std::string& symbol,
        const std::vector<std::string>& sources
    ) const;
    
    // Data preprocessing
    void set_data_preprocessing(const std::string& symbol, bool enable);
    MarketData preprocess_data(const MarketData& raw_data) const;
    
    // Data caching
    void enable_caching(bool enable);
    void clear_cache();
    bool is_cached(const std::string& symbol) const;
    
    // Data export
    bool export_data(
        const std::string& symbol,
        const std::string& format,
        const std::string& filename
    ) const;
    
    // Data statistics
    nlohmann::json get_data_statistics(const std::string& symbol) const;
    std::vector<std::string> get_available_symbols() const;
    std::vector<std::string> get_available_sources() const;

private:
    // Data sources
    std::unordered_map<std::string, std::unique_ptr<DataHandler>> data_sources_;
    std::unordered_map<std::string, std::string> symbol_source_mapping_;
    
    // Data fusion settings
    struct DataFusionConfig {
        bool enable_preprocessing = true;
        bool enable_caching = true;
        bool enable_real_time = false;
        std::string primary_source = "alpha_vantage";
        std::vector<std::string> backup_sources;
        double data_quality_threshold = 0.95;
    };
    
    DataFusionConfig config_;
    
    // Data storage
    std::unordered_map<std::string, std::vector<MarketData>> cached_data_;
    std::unordered_map<std::string, MarketData> latest_data_;
    std::unordered_map<std::string, std::vector<std::pair<Timestamp, double>>> alternative_data_;
    
    // Helper methods
    MarketData select_best_data_source(const std::string& symbol) const;
    bool validate_data_consistency(const std::vector<MarketData>& data_sets) const;
    MarketData merge_data_sets(const std::vector<MarketData>& data_sets) const;
    double calculate_data_quality_score(const MarketData& data) const;
    void update_data_statistics(const std::string& symbol, const MarketData& data);
};

// Data source factory
class DataSourceFactory {
public:
    static std::unique_ptr<DataHandler> create_data_source(
        const std::string& type,
        const nlohmann::json& config
    );
    
    static std::vector<std::string> get_available_types();
    static nlohmann::json get_default_config(const std::string& type);
};

} // namespace backtesting
