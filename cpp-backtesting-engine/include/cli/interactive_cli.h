#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include "engine/backtest_engine.h"

namespace backtesting {
namespace cli {

// Command handler function type
using CommandHandler = std::function<void(const std::vector<std::string>&)>;

// Interactive CLI class
class InteractiveCLI {
private:
    std::map<std::string, CommandHandler> commands_;
    std::vector<std::string> history_;
    std::string prompt_;
    bool running_;
    
    // Helper methods
    void initializeCommands();
    void printWelcome();
    void printHelp();
    void printPrompt();
    std::string readLine();
    std::vector<std::string> parseCommand(const std::string& line);
    void executeCommand(const std::vector<std::string>& args);
    void addToHistory(const std::string& command);
    
    // Command implementations
    void cmdHelp(const std::vector<std::string>& args);
    void cmdBacktest(const std::vector<std::string>& args);
    void cmdStrategy(const std::vector<std::string>& args);
    void cmdData(const std::vector<std::string>& args);
    void cmdResults(const std::vector<std::string>& args);
    void cmdClear(const std::vector<std::string>& args);
    void cmdExit(const std::vector<std::string>& args);
    void cmdHistory(const std::vector<std::string>& args);
    void cmdSymbols(const std::vector<std::string>& args);
    
    // Backtest execution
    void runBacktestInteractive();
    void displayBacktestProgress(const BacktestResult& result);
    void displayBacktestResults(const BacktestResult& result);
    
    // Utility functions
    std::string formatCurrency(double value);
    std::string formatPercentage(double value);
    std::string formatNumber(double value, int precision = 2);
    void printTable(const std::vector<std::vector<std::string>>& rows);
    void printProgressBar(int current, int total, int width = 50);
    
public:
    InteractiveCLI();
    ~InteractiveCLI() = default;
    
    // Main execution
    void run();
    void stop();
    
    // Command registration
    void registerCommand(const std::string& name, CommandHandler handler);
    void registerAlias(const std::string& alias, const std::string& command);
    
    // History management
    void saveHistory(const std::string& filename = ".backtest_history");
    void loadHistory(const std::string& filename = ".backtest_history");
    
    // Configuration
    void setPrompt(const std::string& prompt) { prompt_ = prompt; }
    std::string getPrompt() const { return prompt_; }
};

// Utility functions for CLI formatting
namespace utils {
    
    // Color codes for terminal output
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
        extern const std::string DIM;
        
        std::string colorize(const std::string& text, const std::string& color);
        bool isColorSupported();
    }
    
    // Table formatting
    class TableFormatter {
    private:
        std::vector<std::string> headers_;
        std::vector<std::vector<std::string>> rows_;
        std::vector<int> columnWidths_;
        bool showBorders_;
        
    public:
        TableFormatter(bool showBorders = true);
        void setHeaders(const std::vector<std::string>& headers);
        void addRow(const std::vector<std::string>& row);
        std::string format() const;
        void clear();
        void setShowBorders(bool show) { showBorders_ = show; }
    };
    
    // Progress bar
    class ProgressBar {
    private:
        int total_;
        int current_;
        int width_;
        std::string prefix_;
        std::string suffix_;
        char fillChar_;
        char emptyChar_;
        
    public:
        ProgressBar(int total, int width = 50, const std::string& prefix = "Progress");
        void update(int current);
        void setSuffix(const std::string& suffix) { suffix_ = suffix; }
        void setFillChar(char c) { fillChar_ = c; }
        void setEmptyChar(char c) { emptyChar_ = c; }
        void finish();
        std::string toString() const;
    };
    
    // ASCII chart generation
    class ASCIIChart {
    private:
        int width_;
        int height_;
        std::vector<double> data_;
        std::string title_;
        
    public:
        ASCIIChart(int width = 60, int height = 20);
        void setData(const std::vector<double>& data);
        void setTitle(const std::string& title) { title_ = title; }
        std::string generate() const;
    };
    
    // File utilities
    bool fileExists(const std::string& filename);
    bool directoryExists(const std::string& dirname);
    bool createDirectory(const std::string& dirname);
    std::string getCurrentDirectory();
    std::string getHomeDirectory();
    std::string expandPath(const std::string& path);
    
    // String utilities
    std::vector<std::string> splitString(const std::string& str, char delimiter);
    std::string trimString(const std::string& str);
    std::string toLowerCase(const std::string& str);
    std::string toUpperCase(const std::string& str);
    bool startsWith(const std::string& str, const std::string& prefix);
    bool endsWith(const std::string& str, const std::string& suffix);
    
    // Date/time utilities
    std::string getCurrentTimestamp();
    std::string formatTimestamp(const std::string& timestamp);
    bool isValidDate(const std::string& date);
    bool isValidSymbol(const std::string& symbol);
    
    // Number formatting
    std::string formatNumber(double value, int precision = 2);
    std::string formatCurrency(double value);
    std::string formatPercentage(double value);
    std::string formatLargeNumber(double value);
    
} // namespace utils

} // namespace cli
} // namespace backtesting
