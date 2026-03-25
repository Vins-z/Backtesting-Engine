#include "data/data_validator.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace backtesting {

// ValidationResult implementation
nlohmann::json ValidationResult::to_json() const {
    nlohmann::json j;
    j["is_valid"] = is_valid;
    j["error_message"] = error_message;
    j["warning_message"] = warning_message;
    j["validation_type"] = validation_type;
    j["data_timestamp"] = std::chrono::system_clock::to_time_t(data_timestamp);
    j["metrics"] = metrics;
    return j;
}

// ValidationReport implementation
nlohmann::json ValidationReport::to_json() const {
    nlohmann::json j;
    j["symbol"] = symbol;
    j["validation_timestamp"] = std::chrono::system_clock::to_time_t(validation_timestamp);
    j["overall_valid"] = overall_valid;
    j["quality_score"] = quality_score;
    j["quality_grade"] = quality_grade;
    
    j["counts"] = {
        {"total_data_points", total_data_points},
        {"valid_data_points", valid_data_points},
        {"warnings", warnings},
        {"errors", errors},
        {"missing_values", missing_values},
        {"outliers", outliers},
        {"outliers_detected", outliers_detected},
        {"outliers_removed", outliers_removed}
    };
    
    j["statistics"] = {
        {"mean_price", statistics.mean_price},
        {"std_price", statistics.std_price},
        {"min_price", statistics.min_price},
        {"max_price", statistics.max_price},
        {"mean_volume", statistics.mean_volume},
        {"price_volatility", statistics.price_volatility},
        {"max_daily_return", statistics.max_daily_return},
        {"min_daily_return", statistics.min_daily_return}
    };
    
    j["time_range"] = {
        {"start_date", std::chrono::system_clock::to_time_t(start_date)},
        {"end_date", std::chrono::system_clock::to_time_t(end_date)},
        {"time_span_days", time_span_days}
    };
    
    j["validation_results"] = nlohmann::json::array();
    for (const auto& result : validation_results) {
        j["validation_results"].push_back(result.to_json());
    }
    
    j["warnings_list"] = nlohmann::json::array();
    for (const auto& warning : warnings_list) {
        j["warnings_list"].push_back(warning.to_json());
    }
    
    j["errors_list"] = nlohmann::json::array();
    for (const auto& error : errors_list) {
        j["errors_list"].push_back(error.to_json());
    }
    
    return j;
}

std::string ValidationReport::to_string() const {
    std::ostringstream oss;
    oss << "=== Data Validation Report for " << symbol << " ===\n";
    oss << "Quality Score: " << std::fixed << std::setprecision(1) << quality_score << "% (" << quality_grade << ")\n";
    oss << "Overall Valid: " << (overall_valid ? "YES" : "NO") << "\n";
    oss << "Data Points: " << valid_data_points << "/" << total_data_points << " valid\n";
    oss << "Warnings: " << warnings << ", Errors: " << errors << "\n";
    oss << "Outliers: " << outliers_detected << " detected";
    if (outliers_removed > 0) {
        oss << " (" << outliers_removed << " removed)";
    }
    oss << "\n";
    
    if (time_span_days > 0) {
        oss << "Time Range: " << time_span_days << " days\n";
    }
    
    if (!errors_list.empty()) {
        oss << "\nERRORS:\n";
        for (const auto& error : errors_list) {
            oss << "  - " << error.validation_type << ": " << error.error_message << "\n";
        }
    }
    
    if (!warnings_list.empty()) {
        oss << "\nWARNINGS:\n";
        for (const auto& warning : warnings_list) {
            oss << "  - " << warning.validation_type << ": " << warning.warning_message << "\n";
        }
    }
    
    return oss.str();
}

void ValidationReport::save_to_file(const std::string& filename) const {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << std::setw(2) << to_json() << std::endl;
    }
}

// DataValidator implementation
DataValidator::DataValidator(const ValidationRules& rules) : rules_(rules) {
    // Initialize logger
    try {
        logger_ = spdlog::get("data_validator");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("data_validator");
        }
    } catch (...) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("data_validator", console_sink);
        spdlog::register_logger(logger_);
    }
    
    logger_->info("Data validator initialized with rules");
}

void DataValidator::enable_debug_logging(bool enable) {
    if (enable) {
        logger_->set_level(spdlog::level::debug);
        logger_->debug("Debug logging enabled for data validator");
    } else {
        logger_->set_level(spdlog::level::info);
    }
}

ValidationResult DataValidator::validate_single_point(const OHLC& data) const {
    ValidationResult overall_result;
    overall_result.data_timestamp = data.timestamp;
    overall_result.validation_type = "single_point";
    
    // Validate price data
    auto price_result = validate_price_data(data);
    if (!price_result.is_valid) {
        overall_result.is_valid = false;
        overall_result.error_message += price_result.error_message + " ";
    }
    
    // Validate volume data
    auto volume_result = validate_volume_data(data);
    if (!volume_result.is_valid) {
        overall_result.is_valid = false;
        overall_result.error_message += volume_result.error_message + " ";
    }
    
    // Validate OHLC relationships
    auto ohlc_result = validate_ohlc_relationships(data);
    if (!ohlc_result.is_valid) {
        overall_result.is_valid = false;
        overall_result.error_message += ohlc_result.error_message + " ";
    }
    
    // Store metrics
    overall_result.metrics["open"] = data.open;
    overall_result.metrics["high"] = data.high;
    overall_result.metrics["low"] = data.low;
    overall_result.metrics["close"] = data.close;
    overall_result.metrics["volume"] = static_cast<double>(data.volume);
    overall_result.metrics["daily_range_pct"] = ((data.high - data.low) / data.close) * 100.0;
    
    return overall_result;
}

ValidationReport DataValidator::validate_dataset(const std::vector<OHLC>& data, const std::string& symbol) const {
    ValidationReport report;
    report.symbol = symbol;
    report.validation_timestamp = std::chrono::system_clock::now();
    report.total_data_points = data.size();
    
    if (data.empty()) {
        report.overall_valid = false;
        ValidationResult error;
        error.validation_type = "dataset_size";
        error.error_message = "Dataset is empty";
        error.is_valid = false;
        report.errors_list.push_back(error);
        report.errors = 1;
        report.quality_score = 0.0;
        report.quality_grade = "F";
        return report;
    }
    
    // Set time range
    auto sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end(), [](const OHLC& a, const OHLC& b) {
        return a.timestamp < b.timestamp;
    });
    
    report.start_date = sorted_data.front().timestamp;
    report.end_date = sorted_data.back().timestamp;
    report.time_span_days = std::chrono::duration_cast<std::chrono::hours>(
        report.end_date - report.start_date).count() / 24;
    
    // Validate each data point
    for (const auto& point : data) {
        auto result = validate_single_point(point);
        report.validation_results.push_back(result);
        
        if (result.is_valid) {
            report.valid_data_points++;
        } else {
            report.errors_list.push_back(result);
            report.errors++;
        }
    }
    
    // Temporal consistency validation
    auto temporal_result = validate_temporal_consistency(sorted_data);
    if (!temporal_result.is_valid) {
        if (!temporal_result.error_message.empty()) {
            report.errors_list.push_back(temporal_result);
            report.errors++;
        }
        if (!temporal_result.warning_message.empty()) {
            report.warnings_list.push_back(temporal_result);
            report.warnings++;
        }
    }
    
    // Statistical validation
    if (static_cast<int>(data.size()) >= rules_.min_data_points_for_stats) {
        auto stats_result = validate_statistical_properties(data);
        if (!stats_result.is_valid) {
            if (!stats_result.error_message.empty()) {
                report.errors_list.push_back(stats_result);
                report.errors++;
            }
            if (!stats_result.warning_message.empty()) {
                report.warnings_list.push_back(stats_result);
                report.warnings++;
            }
        }
        
        // Calculate statistical summary
        std::vector<double> prices;
        std::vector<double> volumes;
        for (const auto& point : data) {
            prices.push_back(point.close);
            volumes.push_back(static_cast<double>(point.volume));
        }
        
        report.statistics.mean_price = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
        report.statistics.mean_volume = std::accumulate(volumes.begin(), volumes.end(), 0.0) / volumes.size();
        report.statistics.min_price = *std::min_element(prices.begin(), prices.end());
        report.statistics.max_price = *std::max_element(prices.begin(), prices.end());
        
        // Calculate standard deviation
        double price_variance = 0.0;
        for (double price : prices) {
            price_variance += std::pow(price - report.statistics.mean_price, 2);
        }
        report.statistics.std_price = std::sqrt(price_variance / prices.size());
        
        // Calculate daily returns and volatility
        auto daily_returns = calculate_daily_returns(data);
        if (!daily_returns.empty()) {
            report.statistics.max_daily_return = *std::max_element(daily_returns.begin(), daily_returns.end());
            report.statistics.min_daily_return = *std::min_element(daily_returns.begin(), daily_returns.end());
            
            double mean_return = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
            double return_variance = 0.0;
            for (double ret : daily_returns) {
                return_variance += std::pow(ret - mean_return, 2);
            }
            report.statistics.price_volatility = std::sqrt(return_variance / daily_returns.size()) * std::sqrt(252); // Annualized
        }
    }
    
    // Detect outliers
    auto outlier_indices = detect_outliers(data);
    report.outliers_detected = outlier_indices.size();
    
    // Calculate quality score and grade
    report.quality_score = calculate_quality_score(report);
    report.quality_grade = assign_quality_grade(report.quality_score);
    
    // Determine overall validity
    double completeness_ratio = static_cast<double>(report.valid_data_points) / report.total_data_points;
    report.overall_valid = (completeness_ratio >= rules_.min_completeness_ratio) && 
                          (report.quality_score >= 50.0) && 
                          (report.errors == 0);
    
    logger_->info("Validation completed for {}: {} ({}% quality, {}/{} valid points)", 
                 symbol, report.quality_grade, report.quality_score, 
                 report.valid_data_points, report.total_data_points);
    
    return report;
}

ValidationResult DataValidator::validate_price_data(const OHLC& data) const {
    ValidationResult result;
    result.validation_type = "price_validation";
    result.data_timestamp = data.timestamp;
    
    // Check for NaN or infinite values
    if (std::isnan(data.open) || std::isnan(data.high) || std::isnan(data.low) || std::isnan(data.close) ||
        std::isinf(data.open) || std::isinf(data.high) || std::isinf(data.low) || std::isinf(data.close)) {
        result.is_valid = false;
        result.error_message = "Price contains NaN or infinite values";
        return result;
    }
    
    // Check price ranges
    std::vector<double> prices = {data.open, data.high, data.low, data.close};
    for (double price : prices) {
        if (price < rules_.min_price || price > rules_.max_price) {
            result.is_valid = false;
            result.error_message = "Price outside valid range [" + 
                std::to_string(rules_.min_price) + ", " + std::to_string(rules_.max_price) + "]";
            return result;
        }
    }
    
    // Check daily range
    double daily_range_pct = ((data.high - data.low) / data.close) * 100.0;
    if (daily_range_pct > rules_.max_intraday_range_pct) {
        result.warning_message = "Large intraday range: " + std::to_string(daily_range_pct) + "%";
    }
    
    result.metrics["daily_range_pct"] = daily_range_pct;
    result.is_valid = true;
    return result;
}

ValidationResult DataValidator::validate_volume_data(const OHLC& data) const {
    ValidationResult result;
    result.validation_type = "volume_validation";
    result.data_timestamp = data.timestamp;
    
    if (data.volume < rules_.min_volume || data.volume > rules_.max_volume) {
        result.is_valid = false;
        result.error_message = "Volume outside valid range [" + 
            std::to_string(rules_.min_volume) + ", " + std::to_string(rules_.max_volume) + "]";
        return result;
    }
    
    result.metrics["volume"] = static_cast<double>(data.volume);
    result.is_valid = true;
    return result;
}

ValidationResult DataValidator::validate_ohlc_relationships(const OHLC& data) const {
    ValidationResult result;
    result.validation_type = "ohlc_relationships";
    result.data_timestamp = data.timestamp;
    
    if (!rules_.enforce_ohlc_relationships) {
        result.is_valid = true;
        return result;
    }
    
    // High should be >= Low
    if (data.high < data.low - rules_.price_precision_tolerance) {
        result.is_valid = false;
        result.error_message = "High price is less than low price";
        return result;
    }
    
    // High should be >= Open and Close
    if (data.high < data.open - rules_.price_precision_tolerance || 
        data.high < data.close - rules_.price_precision_tolerance) {
        result.is_valid = false;
        result.error_message = "High price is less than open or close price";
        return result;
    }
    
    // Low should be <= Open and Close
    if (data.low > data.open + rules_.price_precision_tolerance || 
        data.low > data.close + rules_.price_precision_tolerance) {
        result.is_valid = false;
        result.error_message = "Low price is greater than open or close price";
        return result;
    }
    
    result.is_valid = true;
    return result;
}

ValidationResult DataValidator::validate_temporal_consistency(const std::vector<OHLC>& data) const {
    ValidationResult result;
    result.validation_type = "temporal_consistency";
    
    if (data.size() < 2) {
        result.is_valid = true;
        return result;
    }
    
    // Check chronological order
    if (rules_.enforce_chronological_order) {
        for (size_t i = 1; i < data.size(); ++i) {
            if (data[i].timestamp <= data[i-1].timestamp) {
                result.is_valid = false;
                result.error_message = "Data points are not in chronological order";
                return result;
            }
        }
    }
    
    // Check for large gaps
    int large_gaps = 0;
    for (size_t i = 1; i < data.size(); ++i) {
        auto gap_hours = std::chrono::duration_cast<std::chrono::hours>(
            data[i].timestamp - data[i-1].timestamp).count();
        int gap_days = gap_hours / 24;
        
        if (gap_days > rules_.max_gap_days) {
            large_gaps++;
        }
    }
    
    if (large_gaps > 0) {
        result.warning_message = "Found " + std::to_string(large_gaps) + " large gaps in data";
    }
    
    result.metrics["large_gaps"] = static_cast<double>(large_gaps);
    result.is_valid = true;
    return result;
}

ValidationResult DataValidator::validate_statistical_properties(const std::vector<OHLC>& data) const {
    ValidationResult result;
    result.validation_type = "statistical_properties";
    
    if (static_cast<int>(data.size()) < rules_.min_data_points_for_stats) {
        result.warning_message = "Insufficient data points for statistical validation";
        result.is_valid = true;
        return result;
    }
    
    // Calculate daily returns for volatility analysis
    auto daily_returns = calculate_daily_returns(data);
    
    if (daily_returns.empty()) {
        result.warning_message = "Cannot calculate daily returns";
        result.is_valid = true;
        return result;
    }
    
    // Check for extreme returns
    auto max_return = *std::max_element(daily_returns.begin(), daily_returns.end());
    auto min_return = *std::min_element(daily_returns.begin(), daily_returns.end());
    
    if (std::abs(max_return) > rules_.max_daily_change_pct / 100.0 || 
        std::abs(min_return) > rules_.max_daily_change_pct / 100.0) {
        result.warning_message = "Extreme daily returns detected: " + 
            std::to_string(max_return * 100.0) + "%, " + std::to_string(min_return * 100.0) + "%";
    }
    
    result.metrics["max_daily_return"] = max_return;
    result.metrics["min_daily_return"] = min_return;
    result.metrics["return_count"] = static_cast<double>(daily_returns.size());
    
    result.is_valid = true;
    return result;
}

std::vector<double> DataValidator::calculate_daily_returns(const std::vector<OHLC>& data) const {
    std::vector<double> returns;
    
    if (data.size() < 2) {
        return returns;
    }
    
    // Sort by timestamp
    auto sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end(), [](const OHLC& a, const OHLC& b) {
        return a.timestamp < b.timestamp;
    });
    
    for (size_t i = 1; i < sorted_data.size(); ++i) {
        if (sorted_data[i-1].close > 0) {
            double return_pct = (sorted_data[i].close - sorted_data[i-1].close) / sorted_data[i-1].close;
            returns.push_back(return_pct);
        }
    }
    
    return returns;
}

std::vector<int> DataValidator::detect_outliers(const std::vector<OHLC>& data) const {
    std::vector<int> outlier_indices;
    
    if (static_cast<int>(data.size()) < rules_.min_data_points_for_stats) {
        return outlier_indices;
    }
    
    // Calculate mean and standard deviation for close prices
    std::vector<double> prices;
    for (const auto& point : data) {
        prices.push_back(point.close);
    }
    
    double mean = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
    
    double variance = 0.0;
    for (double price : prices) {
        variance += std::pow(price - mean, 2);
    }
    double std_dev = std::sqrt(variance / prices.size());
    
    // Detect outliers using Z-score
    for (size_t i = 0; i < data.size(); ++i) {
        double z_score = std::abs(calculate_z_score(data[i].close, mean, std_dev));
        if (z_score > rules_.outlier_z_threshold) {
            outlier_indices.push_back(i);
        }
    }
    
    return outlier_indices;
}

double DataValidator::calculate_z_score(double value, double mean, double std_dev) const {
    if (std_dev == 0.0) {
        return 0.0;
    }
    return (value - mean) / std_dev;
}

double DataValidator::calculate_quality_score(const ValidationReport& report) const {
    if (report.total_data_points == 0) {
        return 0.0;
    }
    
    // Base score from data completeness
    double completeness_score = static_cast<double>(report.valid_data_points) / report.total_data_points * 50.0;
    
    // Penalty for errors and warnings
    double error_penalty = std::min(static_cast<double>(report.errors) * 10.0, 30.0);
    double warning_penalty = std::min(static_cast<double>(report.warnings) * 2.0, 10.0);
    
    // Bonus for good statistical properties
    double stats_bonus = 0.0;
    if (report.statistics.price_volatility > 0 && report.statistics.price_volatility < 1.0) {
        stats_bonus += 10.0; // Reasonable volatility
    }
    
    // Penalty for outliers
    double outlier_penalty = std::min(static_cast<double>(report.outliers_detected) / report.total_data_points * 100.0, 20.0);
    
    double final_score = completeness_score + stats_bonus - error_penalty - warning_penalty - outlier_penalty;
    return std::max(0.0, std::min(100.0, final_score));
}

std::string DataValidator::assign_quality_grade(double score) const {
    if (score >= 90.0) return "A - Excellent";
    if (score >= 80.0) return "B - Good";
    if (score >= 70.0) return "C - Fair";
    if (score >= 60.0) return "D - Poor";
    return "F - Unacceptable";
}

std::vector<OHLC> DataValidator::clean_dataset(const std::vector<OHLC>& data, ValidationReport& report) const {
    std::vector<OHLC> cleaned_data;
    cleaned_data.reserve(data.size());
    
    // Remove invalid data points
    for (const auto& point : data) {
        auto validation_result = validate_single_point(point);
        if (validation_result.is_valid) {
            cleaned_data.push_back(point);
        }
    }
    
    // Remove outliers if configured
    if (rules_.remove_outliers) {
        std::vector<int> removed_indices;
        cleaned_data = remove_outliers(cleaned_data, removed_indices);
        report.outliers_removed = removed_indices.size();
    }
    
    logger_->info("Data cleaning: {} -> {} points", data.size(), cleaned_data.size());
    return cleaned_data;
}

std::vector<OHLC> DataValidator::remove_outliers(const std::vector<OHLC>& data, std::vector<int>& removed_indices) const {
    auto outlier_indices = detect_outliers(data);
    removed_indices = outlier_indices;
    
    std::vector<OHLC> filtered_data;
    filtered_data.reserve(data.size());
    
    for (size_t i = 0; i < data.size(); ++i) {
        if (std::find(outlier_indices.begin(), outlier_indices.end(), i) == outlier_indices.end()) {
            filtered_data.push_back(data[i]);
        }
    }
    
    return filtered_data;
}

} // namespace backtesting 