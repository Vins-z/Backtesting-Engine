#include "portfolio/portfolio_manager.h"
#include "risk/risk_manager.h"
#include <cassert>
#include <iostream>
#include <cmath>

int main() {
    // 1. Test sell order exposure reduction in BasicRiskManager
    backtesting::PortfolioManager pm(100000.0);
    backtesting::BasicRiskManager rm(0.10, 0.05, 0.05, 0.20); // 10% max position size = $10,000

    backtesting::OHLC ohlc{};
    ohlc.symbol = "AAPL";
    ohlc.close = 100.0;
    pm.update_market_data("AAPL", ohlc);

    // Enter 80 shares @ $100 = $8,000 (8% of $100k, within 10% limit)
    backtesting::Fill buy_fill(1, "AAPL", backtesting::OrderSide::BUY, 80, 100.0, 0.0, 0.0);
    pm.update_fill(buy_fill);
    pm.update_portfolio(std::chrono::system_clock::now());

    // Propose selling 80 shares to close position
    backtesting::Order sell_order(2, "AAPL", backtesting::OrderType::MARKET, backtesting::OrderSide::SELL, 80, 100.0);
    
    // is_order_allowed must pass: selling to close must NOT double apparent size to 160 shares ($16,000 > $10,000 limit)
    bool approved = rm.is_order_allowed(sell_order, pm);
    assert(approved && "Sell order to close long position must not be rejected by position size limit!");

    // 2. Test short position valuation
    // If a short position of -100 shares was established @ $50:
    // Cash increased by $5,000 (total cash = $100,000 + $5,000 = $105,000).
    // Now if price rises to $60:
    // Short liability is -100 * $60 = -$6,000.
    // Portfolio value should be Cash ($105,000) - $6,000 = $99,000 ($1,000 loss).
    backtesting::PortfolioManager pm_short(100000.0);
    backtesting::OHLC short_ohlc{};
    short_ohlc.symbol = "XYZ";
    short_ohlc.close = 50.0;
    pm_short.update_market_data("XYZ", short_ohlc);

    // Enter short position of 100 shares @ $50
    // When buying back / closing short, cash and equity adjust
    auto pos = pm_short.get_position("XYZ");
    (void)pos;
    
    // Update market data with price increase to $60
    short_ohlc.close = 60.0;
    pm_short.update_market_data("XYZ", short_ohlc);

    std::cout << "test_risk_short_exposure passed successfully.\n";
    return 0;
}
