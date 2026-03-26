#include "engine/backtest_engine.h"
#include <iostream>
#include <cstdlib>
#include <string>

int main() {
    using namespace backtesting;

    try {
        const char* enable_net = std::getenv("BACKTESTINGENGINE_ENABLE_YFINANCE_NETWORK");
        const bool allow_yfinance_network =
            (enable_net != nullptr) && (std::string(enable_net) == "1");

        BacktestConfig cfg{};
        cfg.name = "yfinance_example";
        cfg.symbols = {"AAPL"};
        cfg.start_date = "2024-01-01";
        cfg.end_date = "2024-01-25";
        cfg.initial_capital = 100000.0;
        cfg.commission_rate = 0.001;
        cfg.slippage_rate = 0.0005;
        cfg.data_source = allow_yfinance_network ? "yfinance" : "csv";
        cfg.data_path = allow_yfinance_network ? "./cache" : "data/sp500";
        cfg.data_interval = "1d";
        cfg.strategy_name = "moving_average";
        cfg.strategy_params = {{"short_window", 20}, {"long_window", 50}};
        cfg.verbose_logging = true;

        auto run_with_config = [](const BacktestConfig& config) {
            auto engine = BacktestEngine::create_from_config(config);
            return engine->run_backtest();
        };

        BacktestResult r;
        if (!allow_yfinance_network) {
            r = run_with_config(cfg);
        } else {
            try {
                r = run_with_config(cfg);
            } catch (const std::exception& yfinance_err) {
                std::cerr << "YFinance run failed; falling back to CSV. Error: " << yfinance_err.what() << "\n";

                BacktestConfig csv_cfg = cfg;
                csv_cfg.data_source = "csv";
                csv_cfg.data_path = "data/sp500";

                r = run_with_config(csv_cfg);
            }
        }

        std::cout << r.to_json().dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
