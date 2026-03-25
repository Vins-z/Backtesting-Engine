#include <iostream>
#include <memory>
#include <vector>
#include "data/data_handler_factory.h"
#include "engine/backtest_engine.h"
#include "strategy/moving_average_strategy.h"

using namespace backtesting;

int main() {
    std::cout << "=== YFinance Data System Usage Example ===" << std::endl;
    
    try {
        // 1. Create YFinance data handler through factory
        std::cout << "Creating YFinance data handler..." << std::endl;
        auto data_handler = DataHandlerFactory::create("yfinance", "./cache", "");
        
        if (!data_handler) {
            std::cerr << "Failed to create data handler" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Data handler created: " << data_handler->get_source_name() << std::endl;
        
        // 2. Load market data for multiple symbols
        std::cout << "\nLoading market data..." << std::endl;
        std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL"};
        
        for (const auto& symbol : symbols) {
            std::cout << "Loading " << symbol << "..." << std::endl;
            bool success = data_handler->load_symbol_data(symbol, "2023-01-01", "2023-12-31");
            
            if (success) {
                auto data = data_handler->get_historical_data(symbol);
                std::cout << "  ✓ Loaded " << data.size() << " data points" << std::endl;
                
                if (!data.empty()) {
                    std::cout << "  First close: $" << data.front().close << std::endl;
                    std::cout << "  Last close: $" << data.back().close << std::endl;
                }
            } else {
                std::cout << "  ✗ Failed to load data" << std::endl;
            }
        }
        
        // 3. Create backtest configuration
        std::cout << "\nSetting up backtest configuration..." << std::endl;
        BacktestConfig config;
        config.symbols = symbols;
        config.start_date = "2023-01-01";
        config.end_date = "2023-12-31";
        config.initial_capital = 100000.0;
        config.commission = 0.001;
        config.slippage = 0.0005;
        config.verbose_logging = true;
        
        // 4. Create trading strategy
        std::cout << "Creating moving average strategy..." << std::endl;
        auto strategy = std::make_unique<MovingAverageStrategy>(20, 50);
        
        // 5. Create and run backtest
        std::cout << "Running backtest..." << std::endl;
        auto engine = std::make_unique<BacktestEngine>(config, std::move(data_handler));
        engine->set_strategy(std::move(strategy));
        
        bool success = engine->run();
        
        if (success) {
            std::cout << "✓ Backtest completed successfully" << std::endl;
            
            // 6. Display results
            auto results = engine->get_results();
            std::cout << "\n=== Backtest Results ===" << std::endl;
            std::cout << "Initial capital: $" << config.initial_capital << std::endl;
            std::cout << "Final portfolio value: $" << results.final_portfolio_value << std::endl;
            std::cout << "Total return: " << (results.total_return * 100) << "%" << std::endl;
            std::cout << "Sharpe ratio: " << results.sharpe_ratio << std::endl;
            std::cout << "Max drawdown: " << (results.max_drawdown * 100) << "%" << std::endl;
            std::cout << "Total trades: " << results.total_trades << std::endl;
            std::cout << "Win rate: " << (results.win_rate * 100) << "%" << std::endl;
            
        } else {
            std::cout << "✗ Backtest failed" << std::endl;
            return 1;
        }
        
        // 7. Display data quality information
        std::cout << "\n=== Data Quality Information ===" << std::endl;
        std::cout << "Data source: " << data_handler->get_source_name() << std::endl;
        std::cout << "Loaded symbols: ";
        auto loaded_symbols = data_handler->get_symbols();
        for (size_t i = 0; i < loaded_symbols.size(); ++i) {
            std::cout << loaded_symbols[i];
            if (i < loaded_symbols.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
        
        // 8. Demonstrate data iteration
        std::cout << "\n=== Data Iteration Example ===" << std::endl;
        data_handler->reset();
        int count = 0;
        while (data_handler->has_next() && count < 3) {
            auto data = data_handler->get_next();
            std::cout << "Day " << (count + 1) << ": " << data.symbol 
                      << " - Close: $" << data.close << std::endl;
            count++;
        }
        
        std::cout << "\n=== Example Complete ===" << std::endl;
        std::cout << "The YFinance data system provides reliable, high-quality market data" << std::endl;
        std::cout << "with comprehensive validation, caching, and error handling." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
