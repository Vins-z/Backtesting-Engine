#include "engine/backtest_engine.h"
#include <iostream>

int main() {
    using namespace backtesting;

    try {
        BacktestConfig cfg{};
        cfg.name = "csv_example";
        cfg.symbols = {"AAPL"};
        cfg.start_date = "2024-01-01";
        cfg.end_date = "2024-01-25";
        cfg.initial_capital = 10000.0;
        cfg.commission_rate = 0.001;
        cfg.slippage_rate = 0.0005;
        cfg.data_source = "csv";
        cfg.data_path = "data/sp500";
        cfg.data_interval = "1d";
        cfg.strategy_name = "moving_average";
        cfg.strategy_params = {{"short_window", 5}, {"long_window", 10}};

        auto engine = BacktestEngine::create_from_config(cfg);
        BacktestResult r = engine->run_backtest();

        std::cout << r.to_json().dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

