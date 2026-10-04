#include "portfolio/portfolio_manager.h"
#include "performance/advanced_metrics.h"
#include <cassert>
#include <iostream>
#include <cmath>

int main() {
    backtesting::PortfolioManager pm(10000.0);

    // Trade 1: Buy 100 shares @ $100, Sell 100 shares @ $110 (+$1000 profit)
    backtesting::Fill buy1(1, "AAPL", backtesting::OrderSide::BUY, 100, 100.0, 0.0, 0.0);
    pm.update_fill(buy1);

    backtesting::Fill sell1(2, "AAPL", backtesting::OrderSide::SELL, 100, 110.0, 0.0, 0.0);
    pm.update_fill(sell1);

    // Trade 2: Buy 50 shares @ $200, Sell 50 shares @ $190 (-$500 loss)
    backtesting::Fill buy2(3, "MSFT", backtesting::OrderSide::BUY, 50, 200.0, 0.0, 0.0);
    pm.update_fill(buy2);

    backtesting::Fill sell2(4, "MSFT", backtesting::OrderSide::SELL, 50, 190.0, 0.0, 0.0);
    pm.update_fill(sell2);

    auto stats = pm.calculate_portfolio_stats();

    // 2 round trips: 1 winning ($1000), 1 losing ($500)
    // Win rate should be 50.0%, NOT 25.0%
    std::cout << "Total trades: " << stats.total_trades << "\n";
    std::cout << "Winning trades: " << stats.winning_trades << "\n";
    std::cout << "Losing trades: " << stats.losing_trades << "\n";
    std::cout << "Win rate: " << stats.win_rate << "\n";
    std::cout << "Profit factor: " << stats.profit_factor << "\n";

    assert(stats.total_trades == 2 && "Expected 2 round-trip trades");
    assert(stats.winning_trades == 1 && "Expected 1 winning trade");
    assert(stats.losing_trades == 1 && "Expected 1 losing trade");
    assert(std::abs(stats.win_rate - 0.5) < 0.001 && "Win rate must be 0.5 (50%)");
    assert(std::abs(stats.profit_factor - 2.0) < 0.001 && "Profit factor must be 1000/500 = 2.0");

    // Test AdvancedPerformanceAnalyzer
    backtesting::AdvancedPerformanceAnalyzer analyzer;
    auto trades = pm.get_trade_history();
    std::vector<std::pair<backtesting::Timestamp, backtesting::Price>> eq_curve = {
        {buy1.timestamp, 10000.0},
        {sell1.timestamp, 11000.0},
        {sell2.timestamp, 10500.0}
    };
    auto adv = analyzer.calculate_advanced_metrics(eq_curve, trades, 10000.0);
    std::cout << "Advanced metrics win_loss_ratio: " << adv.win_loss_ratio << "\n";
    std::cout << "Advanced metrics expectancy: " << adv.expectancy << "\n";
    assert(std::abs(adv.avg_win - 1000.0) < 0.001 && "avg_win must be 1000");
    assert(std::abs(adv.avg_loss - 500.0) < 0.001 && "avg_loss must be 500");
    assert(std::abs(adv.win_loss_ratio - 2.0) < 0.001 && "win_loss_ratio must be 2.0");
    // Expectancy: 0.5 * 1000 - 0.5 * 500 = 250
    assert(std::abs(adv.expectancy - 250.0) < 0.001 && "expectancy must be 250.0");

    std::cout << "test_trade_metrics passed successfully.\n";
    return 0;
}
