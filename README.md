# Backtesting Engine

C++ backtesting engine focused on correctness, reproducibility, and performance.

## Components

- `cpp-backtesting-engine`: C++20 engine and HTTP API
- `documentation/adr`: architecture decision records

## Quick Start

```bash
cd cpp-backtesting-engine
./build.sh
./backtest_server
```

## Test

```bash
cd cpp-backtesting-engine
ctest --test-dir build --output-on-failure
```

## Benchmark

```bash
cd cpp-backtesting-engine
./scripts/benchmark.sh config.yaml
```

## Open Source Standards

This repository includes:
- `LICENSE`
- `CONTRIBUTING.md`
- `CODE_OF_CONDUCT.md`
- `SECURITY.md`
- issue and PR templates

## License

MIT
