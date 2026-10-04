# Changelog

All notable changes to this project should be documented here.

## Unreleased

## 1.2.0 - 2026-10-05

Comprehensive backtesting engine correctness, risk calculation, accounting, and build system release:

- **Lookahead Bias Elimination**:
  - Removed full pre-simulation dataset initialization in `BacktestEngine::load_data()` which contaminated strategy states with future bars; strategy instances are strictly reset and fed incrementally bar-by-bar during the event loop.
- **Risk Management & Position Sizing**:
  - Fixed dimensionality error in `BasicRiskManager::check_signal()` where exposure was computed as `quantity * market_value`.
  - Fixed position sizing checks in `BasicRiskManager::check_position_size_limit()` to account for order side, preventing false risk rejections when closing or reducing existing long positions.
  - Added guards against division by zero in risk checks if total portfolio equity is zero or negative.
- **Financial Accounting & Valuation**:
  - Corrected short position valuation in `PortfolioManager::update_portfolio()` so short liability is properly deducted from cash.
  - Replaced fill-count trade statistics in `PortfolioManager::calculate_portfolio_stats()` and `BacktestResult::to_json()` with accurate FIFO lot round-trip matching, correcting win rate, profit factor, and trade counts.
  - Fixed per-share slippage double-counting in portfolio statistics.
  - Preserved zero-return observations for flat bars in cross-asset correlation analysis to prevent multi-symbol time-series desynchronization.
- **Cross-Asset Correlation**:
  - Aligned unequal return series in `MultiAssetPortfolio::compute_correlation_matrix()` using trailing common windows (`tail(min_len)`) instead of oldest common windows (`head(min_len)`).
- **Technical Indicators**:
  - Replaced dummy MACD signal line calculation with a true 9-period EMA over the running MACD series.
- **Data Ingestion**:
  - Corrected symbol propagation in `CSVDataHandler` to assign `bar.symbol` properly for loaded CSVs and multi-symbol directory scans, and made symbol extraction path-separator agnostic.
- **Replay, Real-Time & Advanced Analytics Integration**:
  - Restored and completed `ReplayEngine` with safe worker thread joining, proper portfolio queries, and timestamp serialization.
  - Restored `RealTimeDataHandler` with full `DataHandler` interface compliance.
  - Restored and completed `AdvancedPerformanceAnalyzer` with closed-trade FIFO PnL metrics, Kelly criterion, downside deviation, market correlation, and regime detection.
- **C-API Improvements**:
  - Added full parsing for `account_type`, `market_type`, `stop_loss_percentage`, `max_position_size`, `max_daily_loss`, and `max_portfolio_risk` in `config_from_json()`.
- **Build Performance & Test Suite**:
  - Optimized CMake build by moving Homebrew path resolution ahead of `find_package` and linking test executables directly to `BacktestingEngine::backtesting_engine_shared`.
  - Added unit test suites: `test_multi_symbol_csv`, `test_trade_metrics`, and `test_risk_short_exposure`.

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
