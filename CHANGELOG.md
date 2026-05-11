# Changelog

All notable changes to this project should be documented here.

## Unreleased

- Correctness: `BacktestConfig::execution_model` enum (`next_bar_open`, `current_bar_open`,
  `current_bar_close`, `worst_of_bar`). Default remains `worst_of_bar` for backward
  compatibility, but library consumers are encouraged to select `next_bar_open` to
  eliminate intra-bar look-ahead. The engine queues orders for the next bar and the
  execution handler picks the model-appropriate base price.
- Correctness: all date-string parsing in CSV/IEX/Polygon/AlphaVantage/yfinance handlers now
  goes through a shared UTC helper (`common/time_utils.h`); previously each handler used
  `std::mktime`, which interprets input as local time and produced host-dependent results.
- Correctness: `BacktestConfig::validate()` is now truly `const`. Defaulting of
  `account_type`/`market_type` moved into `BacktestConfig::normalize()`, which is called
  automatically by `BacktestEngine::configure()` and `create_from_config()`.
- Tests: added `test_utc_timestamp`, `test_execution_model`, `test_config_validate_const`.
- (next) CMake: vcpkg-friendly dependency discovery (`find_package`), optional TA-Lib, and consumer CI verification.

## 1.0.1

- CMake `find_package` export with `BacktestingEngine::backtesting_engine_shared`
- Minimal C API wrapper (`include/c_api/backtest_c.h`) documented under `cpp-backtesting-engine/docs/`
- Ongoing correctness + stability fixes (risk units, CSV date filtering, async lifecycle)
