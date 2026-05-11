#include "engine/backtest_engine.h"
#include <iostream>

// Regression: BacktestConfig::validate() used to const_cast away constness to
// inject default values for account_type/market_type. That is UB if the caller
// holds a true `const BacktestConfig&`. The function is now pure-const and
// defaults live in normalize().

namespace {

bool validate_via_const_ref(const backtesting::BacktestConfig& cfg) {
    return cfg.validate();
}

}

int main() {
    using namespace backtesting;

    BacktestConfig cfg;
    cfg.name = "unit-test";
    cfg.symbols = {"AAPL"};
    cfg.start_date = "2024-01-01";
    cfg.end_date = "2024-01-31";
    cfg.initial_capital = 100000.0;
    cfg.commission_rate = 0.001;
    cfg.slippage_rate = 0.001;
    cfg.strategy_name = "moving_average";

    // account_type and market_type intentionally left empty.

    if (!validate_via_const_ref(cfg)) {
        std::cerr << "validate() should accept a valid config without mutating it\n";
        return 1;
    }
    if (!cfg.account_type.empty() || !cfg.market_type.empty()) {
        std::cerr << "validate() must not mutate caller's config; got account_type="
                  << cfg.account_type << " market_type=" << cfg.market_type << "\n";
        return 1;
    }

    // normalize() is the explicit way to apply defaults.
    cfg.normalize();
    if (cfg.account_type != "CASH" || cfg.market_type != "OTHER") {
        std::cerr << "normalize() did not apply expected defaults\n";
        return 1;
    }

    // Invalid configs are rejected.
    BacktestConfig bad = cfg;
    bad.symbols.clear();
    if (validate_via_const_ref(bad)) {
        std::cerr << "validate() should reject config with no symbols\n";
        return 1;
    }

    return 0;
}
