#include "portfolio/portfolio_manager.h"
#include <cmath>
#include <iostream>

static bool nearly_equal(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

int main() {
    using namespace backtesting;

    RiskConfig risk_cfg;
    risk_cfg.max_position_size_pct = 1.0;
    risk_cfg.min_position_size = 1.0;

    PortfolioManager pm(100000.0, risk_cfg);

    // Buy 10 @ 100 with $1 commission
    pm.update_fill(Fill{1, "AAPL", OrderSide::BUY, 10.0, 100.0, 1.0, 0.0});
    // Buy 10 @ 110 with $1 commission
    pm.update_fill(Fill{2, "AAPL", OrderSide::BUY, 10.0, 110.0, 1.0, 0.0});
    // Sell 15 @ 120 with $1 commission
    pm.update_fill(Fill{3, "AAPL", OrderSide::SELL, 15.0, 120.0, 1.0, 0.0});

    // FIFO lots, commission allocated per share.
    // Buy1 cost/share = 100 + 1/10 = 100.1
    // Buy2 cost/share = 110 + 1/10 = 110.1
    // Sell proceeds/share = 120 - 1/15
    const double proceeds_per_share = 120.0 - (1.0 / 15.0);
    const double expected =
        (proceeds_per_share - 100.1) * 10.0 +
        (proceeds_per_share - 110.1) * 5.0;

    const double realized = pm.get_realized_pnl();
    if (!nearly_equal(realized, expected)) {
        std::cerr << "Realized P&L mismatch. expected=" << expected << " got=" << realized << "\n";
        return 1;
    }

    return 0;
}

