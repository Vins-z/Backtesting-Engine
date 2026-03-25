#pragma once

#include "engine/backtest_engine.h"
#include <string>
#include <vector>
#include <memory>

namespace backtesting {
namespace cli {

// Command line argument structure
struct CLIArgs {
    std::string data_file;
    std::string strategy_name;
    std::string config_file;
    std::string output_file;
    std::string log_file;
    bool verbose;
    bool help;
    bool version;
    std::vector<std::string> symbols;
    std::string start_date;
    std::string end_date;
    double initial_capital;
    double commission;
    double slippage;
    
    // Strategy parameters
    std::unordered_map<std::string, double> strategy_params;
    
    CLIArgs();
    bool validate() const;
    void print_help() const;
    void print_version() const;
};

// Command line parser
class CLIParser {
public:
    static CLIArgs parse_args(int argc, char* argv[]);
    
private:
    static void parse_strategy_params(const std::string& params_str, CLIArgs& args);
    static std::vector<std::string> split_string(const std::string& str, char delimiter);
};

// Main CLI application
class CLIApplication {
private:
    CLIArgs args_;
    std::unique_ptr<BacktestEngine> engine_;
    
    // Helper methods
    bool initialize_engine();
    bool load_configuration();
    bool run_backtest();
    bool save_results(const BacktestResult& result);
    void print_results(const BacktestResult& result) const;
    void print_progress(int current, int total) const;
    
public:
    CLIApplication(const CLIArgs& args);
    ~CLIApplication();
    
    // Main execution
    int run();
    
    // Static convenience methods
    static int run_with_args(int argc, char* argv[]);
    void print_banner();
};

// Utility functions for CLI
namespace utils {
    
    // String formatting
    std::string format_currency(double value);
    std::string format_percentage(double value);
    std::string format_number(double value, int precision = 2);
    std::string format_timestamp(const Timestamp& ts);
    
    // Table formatting
    class TableFormatter {
    private:
        std::vector<std::string> headers_;
        std::vector<std::vector<std::string>> rows_;
        std::vector<int> column_widths_;
        
    public:
        void set_headers(const std::vector<std::string>& headers);
        void add_row(const std::vector<std::string>& row);
        std::string format() const;
        void clear();
    };
    
    // Progress bar
    class ProgressBar {
    private:
        int total_;
        int current_;
        int width_;
        std::string prefix_;
        
    public:
        ProgressBar(int total, int width = 50, const std::string& prefix = "Progress");
        void update(int current);
        void finish();
    };
    
    // Color output
    namespace colors {
        extern const std::string RESET;
        extern const std::string RED;
        extern const std::string GREEN;
        extern const std::string YELLOW;
        extern const std::string BLUE;
        extern const std::string MAGENTA;
        extern const std::string CYAN;
        extern const std::string WHITE;
        extern const std::string BOLD;
        
        std::string colorize(const std::string& text, const std::string& color);
        bool is_color_supported();
    }
    
    // File utilities
    bool file_exists(const std::string& filename);
    bool directory_exists(const std::string& dirname);
    bool create_directory(const std::string& dirname);
    std::string get_current_directory();
    std::string get_executable_directory();
    
    // Validation utilities
    bool is_valid_date(const std::string& date);
    bool is_valid_symbol(const std::string& symbol);
    bool is_valid_strategy_name(const std::string& strategy);
    
} // namespace utils

} // namespace cli
} // namespace backtesting 