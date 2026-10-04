#include "execution/execution_handler.h"
#include <cmath>
#include <iostream>

// Verify the execution model controls which OHLC component a market order
// is priced against. NEXT_BAR_OPEN delivers fills at the bar's open price
// (because the engine advances the bar pointer before calling the handler),
// while WORST_OF_BAR fills BUY at the bar's high and SELL at the bar's low.
//
// We zero out slippage_rate and the market_impact_factor so the fill price
// equals the model's base price exactly.

namespace {
const backtesting::OHLC kBar{
    std::chrono::system_clock::now(),
    /*open=*/100.0,
    /*high=*/110.0,
    /*low=*/ 95.0,
    /*close=*/105.0,
    /*volume=*/1'000'000LL,
    "AAPL"
};

bool nearly_equal(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    using namespace backtesting;

    const Order buy{1,  "AAPL", OrderType::MARKET, OrderSide::BUY,  10.0, 0.0, 0.0};
    const Order sell{2, "AAPL", OrderType::MARKET, OrderSide::SELL, 10.0, 0.0, 0.0};

    auto make = [&](ExecutionModel m) {
        return RealisticExecutionHandler(
            /*commission_rate=*/0.0,
            /*min_commission=*/0.0,
            /*max_commission=*/0.0,
            /*slippage_rate=*/0.0,
            /*market_impact_factor=*/0.0,
            /*seed=*/123,
            m);
    };

    // WORST_OF_BAR: BUY @ high, SELL @ low
    auto worst = make(ExecutionModel::WORST_OF_BAR);
    Fill bw = worst.execute_order(buy,  kBar);
    Fill sw = worst.execute_order(sell, kBar);
    if (!nearly_equal(bw.price, kBar.high) || !nearly_equal(sw.price, kBar.low)) {
        std::cerr << "WORST_OF_BAR expected BUY@high SELL@low, got BUY=" << bw.price
                  << " SELL=" << sw.price << "\n";
        return 1;
    }

    // CURRENT_BAR_OPEN: both sides @ open
    auto co = make(ExecutionModel::CURRENT_BAR_OPEN);
    Fill bo = co.execute_order(buy,  kBar);
    Fill so = co.execute_order(sell, kBar);
    if (!nearly_equal(bo.price, kBar.open) || !nearly_equal(so.price, kBar.open)) {
        std::cerr << "CURRENT_BAR_OPEN expected open=" << kBar.open
                  << " got BUY=" << bo.price << " SELL=" << so.price << "\n";
        return 1;
    }

    // CURRENT_BAR_CLOSE: both sides @ close
    auto cc = make(ExecutionModel::CURRENT_BAR_CLOSE);
    Fill bc = cc.execute_order(buy,  kBar);
    Fill sc = cc.execute_order(sell, kBar);
    if (!nearly_equal(bc.price, kBar.close) || !nearly_equal(sc.price, kBar.close)) {
        std::cerr << "CURRENT_BAR_CLOSE expected close=" << kBar.close
                  << " got BUY=" << bc.price << " SELL=" << sc.price << "\n";
        return 1;
    }

    // NEXT_BAR_OPEN at the handler level looks like CURRENT_BAR_OPEN because the
    // engine has already advanced the bar pointer before invoking the handler.
    auto next = make(ExecutionModel::NEXT_BAR_OPEN);
    Fill bn = next.execute_order(buy,  kBar);
    Fill sn = next.execute_order(sell, kBar);
    if (!nearly_equal(bn.price, kBar.open) || !nearly_equal(sn.price, kBar.open)) {
        std::cerr << "NEXT_BAR_OPEN expected open=" << kBar.open
                  << " got BUY=" << bn.price << " SELL=" << sn.price << "\n";
        return 1;
    }

    return 0;
}
