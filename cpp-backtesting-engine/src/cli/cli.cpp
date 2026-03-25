#include "cli/cli.h"
#include "engine/backtest_engine.h"
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <yaml-cpp/yaml.h>

namespace backtesting {
namespace cli {

CLIArgs::CLIArgs() 
    : verbose(false), help(false), version(false), 
      initial_capital(100000.0), commission(0.001), slippage(0.001) {}

bool CLIArgs::validate() const {
    if (help || version) return true;
    
    if (config_file.empty()) {
        std::cerr << "Error: Config file is required" << std::endl;
        return false;
    }
    
    if (symbols.empty()) {
        std::cerr << "Error: At least one symbol is required" << std::endl;
        return false;
    }
    
    if (start_date.empty() || end_date.empty()) {
        std::cerr << "Error: Start date and end date are required" << std::endl;
        return false;
    }
    
    return true;
}

void CLIArgs::print_help() const {
    std::cout << "C++ Backtesting Engine\n";
    std::cout << "Usage: backtest_engine [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -c, --config FILE      Configuration file (required)\n";
    std::cout << "  -o, --output FILE      Output file for results\n";
    std::cout << "  -l, --log FILE         Log file\n";
    std::cout << "  -v, --verbose          Verbose logging\n";
    std::cout << "  -h, --help             Show this help\n";
    std::cout << "  --version              Show version\n";
    std::cout << "\nExample:\n";
    std::cout << "  backtest_engine --config config.yaml --output results.json\n";
}

void CLIArgs::print_version() const {
    std::cout << "C++ Backtesting Engine v1.0.0\n";
}

CLIArgs CLIParser::parse_args(int argc, char* argv[]) {
    CLIArgs args;
    
    static struct option long_options[] = {
        {"config",   required_argument, 0, 'c'},
        {"output",   required_argument, 0, 'o'},
        {"log",      required_argument, 0, 'l'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {"version",  no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "c:o:l:vhV", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                args.config_file = optarg;
                break;
            case 'o':
                args.output_file = optarg;
                break;
            case 'l':
                args.log_file = optarg;
                break;
            case 'v':
                args.verbose = true;
                break;
            case 'h':
                args.help = true;
                break;
            case 'V':
                args.version = true;
                break;
            case '?':
                // Invalid option
                break;
            default:
                break;
        }
    }
    
    return args;
}

CLIApplication::CLIApplication(const CLIArgs& args) : args_(args) {}

CLIApplication::~CLIApplication() = default;

int CLIApplication::run() {
    try {
        if (args_.help) {
            args_.print_help();
            return 0;
        }
        
        if (args_.version) {
            args_.print_version();
            return 0;
        }
        
        if (!args_.validate()) {
            return 1;
        }
        
        // Initialize engine
        if (!initialize_engine()) {
            std::cerr << "Failed to initialize engine" << std::endl;
            return 1;
        }
        
        // Load configuration
        if (!load_configuration()) {
            std::cerr << "Failed to load configuration" << std::endl;
            return 1;
        }
        
        // Run backtest
        if (!run_backtest()) {
            std::cerr << "Backtest failed" << std::endl;
            return 1;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

bool CLIApplication::initialize_engine() {
    try {
        engine_ = std::make_unique<BacktestEngine>();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create engine: " << e.what() << std::endl;
        return false;
    }
}

bool CLIApplication::load_configuration() {
    try {
        return engine_->configure_from_yaml(args_.config_file);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return false;
    }
}

bool CLIApplication::run_backtest() {
    try {
        print_banner();
        
        auto result = engine_->run_backtest();
        
        // Save results
        if (!save_results(result)) {
            std::cerr << "Failed to save results" << std::endl;
            return false;
        }
        
        // Print results to console
        print_results(result);
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Backtest execution failed: " << e.what() << std::endl;
        return false;
    }
}

bool CLIApplication::save_results(const BacktestResult& result) {
    try {
        std::string output_file = args_.output_file;
        if (output_file.empty()) {
            // Output to stdout as JSON for API consumption
            std::cout << result.to_json().dump(4) << std::endl;
        } else {
            result.save_to_file(output_file);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save results: " << e.what() << std::endl;
        return false;
    }
}

void CLIApplication::print_results(const BacktestResult& result) const {
    if (!args_.verbose) return;
    
    std::cerr << "\n=== BACKTEST RESULTS ===" << std::endl;
    std::cerr << "Total Return: " << utils::format_percentage(result.metrics.total_return) << std::endl;
    std::cerr << "Total Trades: " << result.metrics.total_trades << std::endl;
    std::cerr << "Win Rate: " << utils::format_percentage(result.metrics.win_rate) << std::endl;
    std::cerr << "Max Drawdown: " << utils::format_percentage(result.metrics.max_drawdown) << std::endl;
    std::cerr << "Sharpe Ratio: " << utils::format_number(result.metrics.sharpe_ratio) << std::endl;
    std::cerr << "Profit Factor: " << utils::format_number(result.metrics.profit_factor) << std::endl;
    std::cerr << "Duration: " << result.duration_seconds << " seconds" << std::endl;
}

int CLIApplication::run_with_args(int argc, char* argv[]) {
    try {
        auto args = CLIParser::parse_args(argc, argv);
        CLIApplication app(args);
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

void CLIApplication::print_banner() {
    if (!args_.verbose) return;
    
    std::cerr << "=====================================\n";
    std::cerr << "     C++ BACKTESTING ENGINE\n";
    std::cerr << "=====================================\n";
}

// Utility functions
namespace utils {
    std::string format_currency(double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << "$" << value;
        return oss.str();
    }
    
    std::string format_percentage(double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (value * 100) << "%";
        return oss.str();
    }
    
    std::string format_number(double value, int precision) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        return oss.str();
    }
}

} // namespace cli
} // namespace backtesting 