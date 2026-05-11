# Changelog

All notable changes to this project should be documented here.

## Unreleased

## 1.1.0 - 2026-05-11

Correctness-focused minor release. Default behavior is preserved; the new
`execution_model` field is a public API addition.

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
- Determinism: `BacktestConfig::seed` plumbed through the C-API JSON config so library
  consumers can request fully reproducible slippage from outside C++.
- CMake: vcpkg-friendly dependency discovery (`find_package`), optional TA-Lib, and
  consumer CI verification.
- Tests: added `test_utc_timestamp`, `test_execution_model`, `test_config_validate_const`
  (in addition to the existing `test_csv_date_filter`, `test_risk_exposure_units`,
  `test_determinism_execution_seed`, `test_realized_pnl_fifo`).

## 1.0.1

- CMake `find_package` export with `BacktestingEngine::backtesting_engine_shared`
- Minimal C API wrapper (`include/c_api/backtest_c.h`) documented under `cpp-backtesting-engine/docs/`
- Ongoing correctness + stability fixes (risk units, CSV date filtering, async lifecycle)
