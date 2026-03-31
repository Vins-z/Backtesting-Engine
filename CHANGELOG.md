# Changelog

All notable changes to this project should be documented here.

## Unreleased

- (next) CMake: vcpkg-friendly dependency discovery (`find_package`), optional TA-Lib, and consumer CI verification.

## 1.0.1

- CMake `find_package` export with `BacktestingEngine::backtesting_engine_shared`
- Minimal C API wrapper (`include/c_api/backtest_c.h`) documented under `cpp-backtesting-engine/docs/`
- Ongoing correctness + stability fixes (risk units, CSV date filtering, async lifecycle)
