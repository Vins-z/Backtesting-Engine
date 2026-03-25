#pragma once

#include "common/types.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace backtesting {

// Validation rules and thresholds
struct ValidationRules {
    // Price validation
    double min_price = 0.01;
    double max_price = 1000000.0;
    double max_daily_change_pct = 50.0; // Maximum daily price change percentage
    double max_intraday_range_pct = 30.0; // Maximum high-low range as percentage of close
    
    // Volume validation
    long min_volume = 0;
    long max_volume = 1000000000000L; // 1 trillion shares
    double max_volume_spike_ratio = 5.0; // Maximum volume spike ratio
    
    // OHLC relationship validation
    bool enforce_ohlc_relationships = true;
    double price_precision_tolerance = 0.0001; // For floating point comparisons
    
    // Temporal validation
    bool enforce_chronological_order = true;
    int max_gap_days = 10; // Maximum gap between consecutive trading days
    
    // Statistical validation
    double outlier_z_threshold = 4.0; // Z-score threshold for outlier detection
    int min_data_points_for_stats = 30; // Minimum points needed for statistical validation
    bool remove_outliers = false; // Whether to automatically remove outliers
    
    // Data completeness
    double min_completeness_ratio = 0.8; // Minimum ratio of valid data points
    bool require_recent_data = true; // Whether recent data is required
    int max_data_age_days = 7; // Maximum age for "recent" data
};

// Individual validation result
struct ValidationResult {
    bool is_valid = true;
    std::string error_message;
    std::string warning_message;
    std::string validation_type;
    Timestamp data_timestamp;
    
    // Detailed validation info
    std::unordered_map<std::string, double> metrics;
    
    nlohmann::json to_json() const;
};

// Comprehensive validation report
struct ValidationReport {
    std::string symbol;
    Timestamp validation_timestamp;
    bool overall_valid = true;
    double quality_score = 0.0; // 0-100 quality score
    std::string quality_grade; // A, B, C, D, F
    
    // Counts
    int total_data_points = 0;
    int valid_data_points = 0;
    int warnings = 0;
    int errors = 0;
    int missing_values = 0;
    int outliers = 0;
    int outliers_detected = 0;
    int outliers_removed = 0;
    
    // Detailed results
    std::vector<ValidationResult> validation_results;
    std::vector<ValidationResult> warnings_list;
    std::vector<ValidationResult> errors_list;
    
    // Statistical summary
    struct StatisticalSummary {
        double mean_price = 0.0;
        double std_price = 0.0;
        double min_price = 0.0;
        double max_price = 0.0;
        double mean_volume = 0.0;
        double price_volatility = 0.0;
        double max_daily_return = 0.0;
        double min_daily_return = 0.0;
    } statistics;
    
    // Time range
    Timestamp start_date;
    Timestamp end_date;
    int time_span_days = 0;
    
    nlohmann::json to_json() const;
    std::string to_string() const;
    void save_to_file(const std::string& filename) const;
};

// Market data validator class
class DataValidator {
private:
    ValidationRules rules_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // Internal validation methods
    ValidationResult validate_price_data(const OHLC& data) const;
    ValidationResult validate_volume_data(const OHLC& data) const;
    ValidationResult validate_ohlc_relationships(const OHLC& data) const;
    ValidationResult validate_temporal_consistency(const std::vector<OHLC>& data) const;
    ValidationResult validate_statistical_properties(const std::vector<OHLC>& data) const;
    
    // Statistical analysis
    std::vector<double> calculate_daily_returns(const std::vector<OHLC>& data) const;
    std::vector<int> detect_outliers(const std::vector<OHLC>& data) const;
    double calculate_z_score(double value, double mean, double std_dev) const;
    double calculate_quality_score(const ValidationReport& report) const;
    std::string assign_quality_grade(double score) const;
    
    // Utility methods
    bool is_trading_day(const Timestamp& timestamp) const;
    int calculate_trading_days_between(const Timestamp& start, const Timestamp& end) const;
    
public:
    explicit DataValidator(const ValidationRules& rules = ValidationRules());
    ~DataValidator() = default;
    
    // Configuration
    void set_validation_rules(const ValidationRules& rules) { rules_ = rules; }
    const ValidationRules& get_validation_rules() const { return rules_; }
    void enable_debug_logging(bool enable = true);
    
    // Single data point validation
    ValidationResult validate_single_point(const OHLC& data) const;
    
    // Dataset validation
    ValidationReport validate_dataset(const std::vector<OHLC>& data, const std::string& symbol = "") const;
    
    // Batch validation
    std::unordered_map<std::string, ValidationReport> validate_multiple_symbols(
        const std::unordered_map<std::string, std::vector<OHLC>>& symbol_data) const;
    
    // Data cleaning and correction
    std::vector<OHLC> clean_dataset(const std::vector<OHLC>& data, ValidationReport& report) const;
    std::vector<OHLC> remove_outliers(const std::vector<OHLC>& data, std::vector<int>& removed_indices) const;
    std::vector<OHLC> interpolate_missing_data(const std::vector<OHLC>& data) const;
    
    // Quality assessment
    bool is_dataset_acceptable(const ValidationReport& report, double min_quality_score = 70.0) const;
    std::vector<std::string> get_recommendations(const ValidationReport& report) const;
    
    // Reporting
    std::string generate_validation_summary(const ValidationReport& report) const;
    nlohmann::json export_validation_metrics(const ValidationReport& report) const;
    
    // Real-time validation for streaming data
    class StreamingValidator {
    private:
        const DataValidator* parent_;
        std::vector<OHLC> recent_data_;
        int buffer_size_;
        ValidationReport latest_report_;
        
    public:
        StreamingValidator(const DataValidator* parent, int buffer_size = 100);
        
        ValidationResult validate_new_point(const OHLC& new_data);
        ValidationReport get_latest_report() const { return latest_report_; }
        void reset_buffer();
        bool is_data_quality_declining() const;
    };
    
    std::unique_ptr<StreamingValidator> create_streaming_validator(int buffer_size = 100) const;
};

// Specialized validators for different data sources
class AlphaVantageDataValidator : public DataValidator {
public:
    AlphaVantageDataValidator();
    
    // Alpha Vantage specific validation
    ValidationResult validate_api_response(const nlohmann::json& response) const;
    bool is_api_error_response(const nlohmann::json& response) const;
    std::string extract_api_error_message(const nlohmann::json& response) const;
};

class CSVDataValidator : public DataValidator {
private:
    struct CSVFormat {
        std::vector<std::string> expected_columns;
        std::string date_format;
        char delimiter;
        bool has_header;
        
        CSVFormat() : delimiter(','), has_header(true) {}
    };
    
    CSVFormat csv_format_;
    
public:
    CSVDataValidator(const CSVFormat& format = CSVFormat());
    
    // CSV specific validation
    ValidationResult validate_csv_structure(const std::string& csv_content) const;
    ValidationResult validate_csv_headers(const std::vector<std::string>& headers) const;
    ValidationResult validate_csv_row(const std::vector<std::string>& row, int row_number) const;
};

// Data quality monitor for continuous monitoring
class DataQualityMonitor {
private:
    std::unique_ptr<DataValidator> validator_;
    std::unordered_map<std::string, ValidationReport> historical_reports_;
    std::unordered_map<std::string, std::vector<double>> quality_trends_;
    
    // Alert thresholds
    double quality_decline_threshold_ = 10.0; // Percentage decline that triggers alert
    int trend_analysis_window_ = 10; // Number of reports to analyze for trends
    
public:
    DataQualityMonitor(std::unique_ptr<DataValidator> validator);
    
    // Monitoring methods
    void add_validation_report(const std::string& symbol, const ValidationReport& report);
    bool is_quality_declining(const std::string& symbol) const;
    double get_quality_trend(const std::string& symbol) const;
    
    // Alerting
    std::vector<std::string> get_quality_alerts() const;
    nlohmann::json get_quality_dashboard() const;
    
    // Historical analysis
    ValidationReport get_latest_report(const std::string& symbol) const;
    std::vector<ValidationReport> get_historical_reports(const std::string& symbol, int limit = 10) const;
    
    // Configuration
    void set_decline_threshold(double threshold) { quality_decline_threshold_ = threshold; }
    void set_trend_window(int window) { trend_analysis_window_ = window; }
};

} // namespace backtesting 