#include "execution/execution_handler.h"
#include <cmath>
#include <iostream>

static bool nearly_equal(double a, double b, double eps = 1e-12) {
    return std::fabs(a - b) <= eps;
}

int main() {
    using namespace backtesting;

    const OHLC bar{
        std::chrono::system_clock::now(),
        100.0, // open
        105.0, // high
        95.0,  // low
        102.0, // close
        1'000'000,
        "AAPL"
    };

    const Order order{1, "AAPL", OrderType::MARKET, OrderSide::BUY, 10.0, 0.0, 0.0};

    RealisticExecutionHandler h1(0.001, 1.0, 0.005, 0.001, 0.001, /*seed=*/42);
    RealisticExecutionHandler h2(0.001, 1.0, 0.005, 0.001, 0.001, /*seed=*/42);

    const Fill f1 = h1.execute_order(order, bar);
    const Fill f2 = h2.execute_order(order, bar);

    if (!nearly_equal(f1.price, f2.price) ||
        !nearly_equal(f1.commission, f2.commission) ||
        !nearly_equal(f1.slippage, f2.slippage) ||
        !nearly_equal(f1.quantity, f2.quantity)) {
        std::cerr << "Determinism failure: same seed produced different fills\n";
        std::cerr << "f1: price=" << f1.price << " comm=" << f1.commission << " slip=" << f1.slippage << " qty=" << f1.quantity << "\n";
        std::cerr << "f2: price=" << f2.price << " comm=" << f2.commission << " slip=" << f2.slippage << " qty=" << f2.quantity << "\n";
        return 1;
    }

    RealisticExecutionHandler h3(0.001, 1.0, 0.005, 0.001, 0.001, /*seed=*/7);
    const Fill f3 = h3.execute_order(order, bar);

    // Different seeds should typically produce different slippage/price; allow rare equality.
    if (nearly_equal(f1.price, f3.price) && nearly_equal(f1.slippage, f3.slippage)) {
        std::cerr << "Expected different seed to change fill (rare collision)\n";
        return 1;
    }

    return 0;
}

