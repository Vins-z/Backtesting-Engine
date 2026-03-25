#include "portfolio/portfolio_manager.h"
#include <iostream>

int main() {
    backtesting::RiskConfig risk_cfg;
    risk_cfg.max_total_exposure_pct = 0.10;  // 10%
    risk_cfg.max_position_size_pct = 1.0;    // allow a full-size position for test setup
    risk_cfg.min_position_size = 1.0;

    backtesting::PortfolioManager pm(1000.0, risk_cfg);
    backtesting::OHLC ohlc{};
    ohlc.close = 100.0;
    pm.update_market_data("AAPL", ohlc);

    backtesting::Fill fill(
        1,
        "AAPL",
        backtesting::OrderSide::BUY,
        2.0,   // 20% exposure on 1000 capital
        100.0,
        0.0,
        0.0
    );
    pm.update_fill(fill);
    pm.update_portfolio(std::chrono::system_clock::now());

    if (!pm.is_risk_limit_exceeded()) {
        std::cerr << "Expected risk limit to be exceeded for 20% exposure > 10% max\n";
        return 1;
    }
    return 0;
}
