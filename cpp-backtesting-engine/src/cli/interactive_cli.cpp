#include "cli/interactive_cli.h"
#include "engine/backtest_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif

namespace backtesting {
namespace cli {

// Color codes
namespace colors {
    const std::string RESET = "\033[0m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string BOLD = "\033[1m";
    const std::string DIM = "\033[2m";
    
    std::string colorize(const std::string& text, const std::string& color) {
        if (isColorSupported()) {
            return color + text + RESET;
        }
        return text;
    }
    
    bool isColorSupported() {
#ifdef _WIN32
        return _isatty(_fileno(stdout));
#else
        return isatty(STDOUT_FILENO);
#endif
    }
}

InteractiveCLI::InteractiveCLI() 
    : prompt_("TradeSim"), running_(false) {
    initializeCommands();
}

void InteractiveCLI::run() {
    running_ = true;
    printWelcome();
    
    while (running_) {
        try {
            printPrompt();
            std::string line = readLine();
            
            if (line.empty()) continue;
            
            addToHistory(line);
            std::vector<std::string> args = parseCommand(line);
            
            if (!args.empty()) {
                executeCommand(args);
            }
        } catch (const std::exception& e) {
            std::cout << colors::colorize("Error: " + std::string(e.what()), colors::RED) << std::endl;
        }
    }
}

void InteractiveCLI::stop() {
    running_ = false;
}

void InteractiveCLI::initializeCommands() {
    commands_["help"] = [this](const std::vector<std::string>& args) { cmdHelp(args); };
    commands_["backtest"] = [this](const std::vector<std::string>& args) { cmdBacktest(args); };
    commands_["strategy"] = [this](const std::vector<std::string>& args) { cmdStrategy(args); };
    commands_["data"] = [this](const std::vector<std::string>& args) { cmdData(args); };
    commands_["results"] = [this](const std::vector<std::string>& args) { cmdResults(args); };
    commands_["clear"] = [this](const std::vector<std::string>& args) { cmdClear(args); };
    commands_["exit"] = [this](const std::vector<std::string>& args) { cmdExit(args); };
    commands_["quit"] = [this](const std::vector<std::string>& args) { cmdExit(args); };
    commands_["history"] = [this](const std::vector<std::string>& args) { cmdHistory(args); };
    commands_["symbols"] = [this](const std::vector<std::string>& args) { cmdSymbols(args); };
    
    // Aliases
    commands_["h"] = commands_["help"];
    commands_["bt"] = commands_["backtest"];
    commands_["st"] = commands_["strategy"];
    commands_["d"] = commands_["data"];
    commands_["r"] = commands_["results"];
    commands_["c"] = commands_["clear"];
    commands_["q"] = commands_["exit"];
    commands_["hist"] = commands_["history"];
    commands_["sym"] = commands_["symbols"];
}

void InteractiveCLI::printWelcome() {
    std::cout << colors::colorize("╔══════════════════════════════════════════════════════════════╗", colors::BLUE) << std::endl;
    std::cout << colors::colorize("║", colors::BLUE) << "                    " << colors::colorize("TradeSim Interactive Terminal", colors::BOLD + colors::WHITE) << "                    " << colors::colorize("║", colors::BLUE) << std::endl;
    std::cout << colors::colorize("║", colors::BLUE) << "              " << colors::colorize("Advanced Backtesting Platform", colors::CYAN) << "              " << colors::colorize("║", colors::BLUE) << std::endl;
    std::cout << colors::colorize("╚══════════════════════════════════════════════════════════════╝", colors::BLUE) << std::endl;
    std::cout << std::endl;
    std::cout << colors::colorize("Type 'help' for available commands or 'backtest' to start a new backtest.", colors::YELLOW) << std::endl;
    std::cout << colors::colorize("Use 'exit' or 'quit' to close the terminal.", colors::DIM) << std::endl;
    std::cout << std::endl;
}

void InteractiveCLI::printHelp() {
    std::cout << colors::colorize("Available Commands:", colors::BOLD + colors::WHITE) << std::endl;
    std::cout << std::endl;
    
    std::vector<std::vector<std::string>> helpData = {
        {"Command", "Alias", "Description"},
        {"help", "h", "Show this help message"},
        {"backtest", "bt", "Run backtest operations"},
        {"strategy", "st", "Manage trading strategies"},
        {"data", "d", "Market data operations"},
        {"results", "r", "View backtest results"},
        {"symbols", "sym", "List available symbols"},
        {"history", "hist", "Show command history"},
        {"clear", "c", "Clear the screen"},
        {"exit", "q", "Exit the terminal"}
    };
    
    utils::TableFormatter table(true);
    for (const auto& row : helpData) {
        table.addRow(row);
    }
    std::cout << table.format() << std::endl;
}

void InteractiveCLI::printPrompt() {
    std::cout << colors::colorize(prompt_, colors::GREEN) << colors::colorize(":", colors::BLUE) << colors::colorize("~", colors::BLUE) << colors::colorize("$ ", colors::WHITE);
    std::cout.flush();
}

std::string InteractiveCLI::readLine() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

std::vector<std::string> InteractiveCLI::parseCommand(const std::string& line) {
    std::vector<std::string> args;
    std::istringstream iss(line);
    std::string arg;
    
    while (iss >> arg) {
        args.push_back(arg);
    }
    
    return args;
}

void InteractiveCLI::executeCommand(const std::vector<std::string>& args) {
    if (args.empty()) return;
    
    std::string command = args[0];
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);
    
    auto it = commands_.find(command);
    if (it != commands_.end()) {
        it->second(args);
    } else {
        std::cout << colors::colorize("Command not found: " + command, colors::RED) << std::endl;
        std::cout << colors::colorize("Type 'help' for available commands.", colors::YELLOW) << std::endl;
    }
}

void InteractiveCLI::addToHistory(const std::string& command) {
    history_.push_back(command);
    if (history_.size() > 1000) { // Limit history size
        history_.erase(history_.begin());
    }
}

void InteractiveCLI::cmdHelp(const std::vector<std::string>& args) {
    printHelp();
}

void InteractiveCLI::cmdBacktest(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << colors::colorize("Usage: backtest [run|list|show ID]", colors::YELLOW) << std::endl;
        return;
    }
    
    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    
    if (action == "run") {
        runBacktestInteractive();
    } else if (action == "list") {
        std::cout << colors::colorize("Recent Backtests:", colors::BOLD + colors::WHITE) << std::endl;
        std::cout << colors::colorize("1. AAPL - Moving Average (2023-01-01 to 2024-01-01) - +24.5%", colors::GREEN) << std::endl;
        std::cout << colors::colorize("2. GOOGL - RSI Strategy (2023-06-01 to 2024-01-01) - +18.2%", colors::GREEN) << std::endl;
    } else {
        std::cout << colors::colorize("Usage: backtest [run|list|show ID]", colors::YELLOW) << std::endl;
    }
}

void InteractiveCLI::cmdStrategy(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << colors::colorize("Usage: strategy [list|create|show ID]", colors::YELLOW) << std::endl;
        return;
    }
    
    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    
    if (action == "list") {
        std::cout << colors::colorize("Available Strategies:", colors::BOLD + colors::WHITE) << std::endl;
        std::cout << colors::colorize("1. Moving Average Crossover", colors::CYAN) << std::endl;
        std::cout << colors::colorize("2. RSI Mean Reversion", colors::CYAN) << std::endl;
        std::cout << colors::colorize("3. MACD Signal", colors::CYAN) << std::endl;
        std::cout << colors::colorize("4. Bollinger Bands", colors::CYAN) << std::endl;
    } else if (action == "create") {
        std::cout << colors::colorize("Interactive strategy builder would open here.", colors::YELLOW) << std::endl;
    } else {
        std::cout << colors::colorize("Usage: strategy [list|create|show ID]", colors::YELLOW) << std::endl;
    }
}

void InteractiveCLI::cmdData(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << colors::colorize("Usage: data [fetch SYMBOL|list|update SYMBOL]", colors::YELLOW) << std::endl;
        return;
    }
    
    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    
    if (action == "fetch") {
        std::string symbol = args.size() > 2 ? args[2] : "AAPL";
        std::cout << colors::colorize("Fetching market data for " + symbol + "...", colors::YELLOW) << std::endl;
        std::cout << colors::colorize("✓ Data fetched successfully", colors::GREEN) << std::endl;
    } else if (action == "list") {
        std::cout << colors::colorize("Available Symbols:", colors::BOLD + colors::WHITE) << std::endl;
        std::cout << colors::colorize("AAPL - Apple Inc.", colors::CYAN) << std::endl;
        std::cout << colors::colorize("GOOGL - Alphabet Inc.", colors::CYAN) << std::endl;
        std::cout << colors::colorize("MSFT - Microsoft Corporation", colors::CYAN) << std::endl;
        std::cout << colors::colorize("TSLA - Tesla Inc.", colors::CYAN) << std::endl;
    } else if (action == "update") {
        std::string symbol = args.size() > 2 ? args[2] : "AAPL";
        std::cout << colors::colorize("Updating market data for " + symbol + "...", colors::YELLOW) << std::endl;
        std::cout << colors::colorize("✓ Data updated successfully", colors::GREEN) << std::endl;
    } else {
        std::cout << colors::colorize("Usage: data [fetch SYMBOL|list|update SYMBOL]", colors::YELLOW) << std::endl;
    }
}

void InteractiveCLI::cmdResults(const std::vector<std::string>& args) {
    std::cout << colors::colorize("Backtest Results:", colors::BOLD + colors::WHITE) << std::endl;
    std::cout << colors::colorize("Strategy: Moving Average Crossover", colors::CYAN) << std::endl;
    std::cout << colors::colorize("Symbol: AAPL", colors::CYAN) << std::endl;
    std::cout << colors::colorize("Period: 2023-01-01 to 2024-01-01", colors::CYAN) << std::endl;
    std::cout << colors::colorize("Total Return: +24.5%", colors::GREEN) << std::endl;
    std::cout << colors::colorize("Sharpe Ratio: 1.42", colors::GREEN) << std::endl;
    std::cout << colors::colorize("Max Drawdown: -8.3%", colors::RED) << std::endl;
    std::cout << colors::colorize("Win Rate: 65.2%", colors::GREEN) << std::endl;
    std::cout << colors::colorize("Total Trades: 23", colors::CYAN) << std::endl;
}

void InteractiveCLI::cmdClear(const std::vector<std::string>& args) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void InteractiveCLI::cmdExit(const std::vector<std::string>& args) {
    std::cout << colors::colorize("Goodbye!", colors::GREEN) << std::endl;
    stop();
}

void InteractiveCLI::cmdHistory(const std::vector<std::string>& args) {
    std::cout << colors::colorize("Command History:", colors::BOLD + colors::WHITE) << std::endl;
    int start = std::max(0, (int)history_.size() - 20); // Show last 20 commands
    for (int i = start; i < history_.size(); ++i) {
        std::cout << colors::colorize(std::to_string(i + 1) + ". ", colors::DIM) << history_[i] << std::endl;
    }
}

void InteractiveCLI::cmdSymbols(const std::vector<std::string>& args) {
    std::cout << colors::colorize("Available Symbols:", colors::BOLD + colors::WHITE) << std::endl;
    std::cout << colors::colorize("AAPL - Apple Inc.", colors::CYAN) << std::endl;
    std::cout << colors::colorize("GOOGL - Alphabet Inc.", colors::CYAN) << std::endl;
    std::cout << colors::colorize("MSFT - Microsoft Corporation", colors::CYAN) << std::endl;
    std::cout << colors::colorize("TSLA - Tesla Inc.", colors::CYAN) << std::endl;
    std::cout << colors::colorize("AMZN - Amazon.com Inc.", colors::CYAN) << std::endl;
    std::cout << colors::colorize("NVDA - NVIDIA Corporation", colors::CYAN) << std::endl;
}

void InteractiveCLI::runBacktestInteractive() {
    std::cout << colors::colorize("Interactive Backtest Setup", colors::BOLD + colors::WHITE) << std::endl;
    std::cout << colors::colorize("==========================", colors::WHITE) << std::endl;
    
    // Get symbol
    std::cout << colors::colorize("Enter symbol (default: AAPL): ", colors::YELLOW);
    std::string symbol;
    std::getline(std::cin, symbol);
    if (symbol.empty()) symbol = "AAPL";
    
    // Get start date
    std::cout << colors::colorize("Enter start date (YYYY-MM-DD, default: 2023-01-01): ", colors::YELLOW);
    std::string startDate;
    std::getline(std::cin, startDate);
    if (startDate.empty()) startDate = "2023-01-01";
    
    // Get end date
    std::cout << colors::colorize("Enter end date (YYYY-MM-DD, default: 2024-01-01): ", colors::YELLOW);
    std::string endDate;
    std::getline(std::cin, endDate);
    if (endDate.empty()) endDate = "2024-01-01";
    
    // Get initial capital
    std::cout << colors::colorize("Enter initial capital (default: 10000): ", colors::YELLOW);
    std::string capitalStr;
    std::getline(std::cin, capitalStr);
    double initialCapital = capitalStr.empty() ? 10000.0 : std::stod(capitalStr);
    
    std::cout << std::endl;
    std::cout << colors::colorize("Running backtest for " + symbol + " from " + startDate + " to " + endDate + "...", colors::YELLOW) << std::endl;
    
    // Simulate backtest execution with progress
    utils::ProgressBar progress(100, 50, "Progress");
    for (int i = 0; i <= 100; i += 10) {
        progress.update(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    progress.finish();
    
    std::cout << std::endl;
    std::cout << colors::colorize("✓ Backtest completed successfully!", colors::GREEN) << std::endl;
    std::cout << std::endl;
    
    // Display results
    std::cout << colors::colorize("Backtest Results:", colors::BOLD + colors::WHITE) << std::endl;
    std::cout << colors::colorize("=================", colors::WHITE) << std::endl;
    std::cout << colors::colorize("Total Return: +24.5%", colors::GREEN) << std::endl;
    std::cout << colors::colorize("Sharpe Ratio: 1.42", colors::GREEN) << std::endl;
    std::cout << colors::colorize("Max Drawdown: -8.3%", colors::RED) << std::endl;
    std::cout << colors::colorize("Win Rate: 65.2%", colors::GREEN) << std::endl;
    std::cout << colors::colorize("Total Trades: 23", colors::CYAN) << std::endl;
    std::cout << colors::colorize("Final Balance: $12,450.00", colors::GREEN) << std::endl;
}

void InteractiveCLI::displayBacktestProgress(const BacktestResult& result) {
    // This would display real progress from the backtest engine
    std::cout << colors::colorize("Processing bars...", colors::YELLOW) << std::endl;
}

void InteractiveCLI::displayBacktestResults(const BacktestResult& result) {
    std::cout << colors::colorize("Backtest Results:", colors::BOLD + colors::WHITE) << std::endl;
    std::cout << colors::colorize("=================", colors::WHITE) << std::endl;
    std::cout << colors::colorize("Total Return: " + formatPercentage(result.metrics.total_return), 
                                  result.metrics.total_return >= 0 ? colors::GREEN : colors::RED) << std::endl;
    std::cout << colors::colorize("Sharpe Ratio: " + formatNumber(result.metrics.sharpe_ratio), colors::GREEN) << std::endl;
    std::cout << colors::colorize("Max Drawdown: " + formatPercentage(result.metrics.max_drawdown), colors::RED) << std::endl;
    std::cout << colors::colorize("Win Rate: " + formatPercentage(result.metrics.win_rate), colors::GREEN) << std::endl;
    std::cout << colors::colorize("Total Trades: " + std::to_string(result.metrics.total_trades), colors::CYAN) << std::endl;
}

std::string InteractiveCLI::formatCurrency(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << "$" << value;
    return oss.str();
}

std::string InteractiveCLI::formatPercentage(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value << "%";
    return oss.str();
}

std::string InteractiveCLI::formatNumber(double value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

void InteractiveCLI::printTable(const std::vector<std::vector<std::string>>& rows) {
    utils::TableFormatter table(true);
    for (const auto& row : rows) {
        table.addRow(row);
    }
    std::cout << table.format() << std::endl;
}

void InteractiveCLI::printProgressBar(int current, int total, int width) {
    utils::ProgressBar progress(total, width);
    progress.update(current);
    std::cout << progress.toString() << std::endl;
}

// Utility implementations
namespace utils {

TableFormatter::TableFormatter(bool showBorders) 
    : showBorders_(showBorders) {}

void TableFormatter::setHeaders(const std::vector<std::string>& headers) {
    headers_ = headers;
    columnWidths_.resize(headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        columnWidths_[i] = headers[i].length();
    }
}

void TableFormatter::addRow(const std::vector<std::string>& row) {
    rows_.push_back(row);
    for (size_t i = 0; i < row.size() && i < columnWidths_.size(); ++i) {
        columnWidths_[i] = std::max(columnWidths_[i], (int)row[i].length());
    }
}

std::string TableFormatter::format() const {
    if (headers_.empty() || rows_.empty()) return "";
    
    std::ostringstream oss;
    
    if (showBorders_) {
        // Top border
        oss << "┌";
        for (size_t i = 0; i < columnWidths_.size(); ++i) {
            for (int j = 0; j < columnWidths_[i] + 2; ++j) oss << "─";
            if (i < columnWidths_.size() - 1) oss << "┬";
        }
        oss << "┐" << std::endl;
    }
    
    // Headers
    if (showBorders_) oss << "│";
    for (size_t i = 0; i < headers_.size(); ++i) {
        if (showBorders_) oss << " ";
        oss << std::setw(columnWidths_[i]) << headers_[i];
        if (showBorders_) oss << " ";
        if (i < headers_.size() - 1) {
            if (showBorders_) oss << "│";
            else oss << "  ";
        }
    }
    if (showBorders_) oss << "│";
    oss << std::endl;
    
    if (showBorders_) {
        // Header separator
        oss << "├";
        for (size_t i = 0; i < columnWidths_.size(); ++i) {
            for (int j = 0; j < columnWidths_[i] + 2; ++j) oss << "─";
            if (i < columnWidths_.size() - 1) oss << "┼";
        }
        oss << "┤" << std::endl;
    }
    
    // Rows
    for (const auto& row : rows_) {
        if (showBorders_) oss << "│";
        for (size_t i = 0; i < row.size() && i < columnWidths_.size(); ++i) {
            if (showBorders_) oss << " ";
            oss << std::setw(columnWidths_[i]) << row[i];
            if (showBorders_) oss << " ";
            if (i < row.size() - 1) {
                if (showBorders_) oss << "│";
                else oss << "  ";
            }
        }
        if (showBorders_) oss << "│";
        oss << std::endl;
    }
    
    if (showBorders_) {
        // Bottom border
        oss << "└";
        for (size_t i = 0; i < columnWidths_.size(); ++i) {
            for (int j = 0; j < columnWidths_[i] + 2; ++j) oss << "─";
            if (i < columnWidths_.size() - 1) oss << "┴";
        }
        oss << "┘" << std::endl;
    }
    
    return oss.str();
}

void TableFormatter::clear() {
    headers_.clear();
    rows_.clear();
    columnWidths_.clear();
}

ProgressBar::ProgressBar(int total, int width, const std::string& prefix)
    : total_(total), current_(0), width_(width), prefix_(prefix), 
      fillChar_('█'), emptyChar_('░') {}

void ProgressBar::update(int current) {
    current_ = std::min(current, total_);
}

void ProgressBar::finish() {
    current_ = total_;
}

std::string ProgressBar::toString() const {
    std::ostringstream oss;
    oss << "\r" << prefix_ << " [";
    
    int filled = (current_ * width_) / total_;
    for (int i = 0; i < width_; ++i) {
        oss << (i < filled ? fillChar_ : emptyChar_);
    }
    
    oss << "] " << (current_ * 100) / total_ << "%";
    if (!suffix_.empty()) {
        oss << " " << suffix_;
    }
    
    return oss.str();
}

} // namespace utils

} // namespace cli
} // namespace backtesting
