## CSV Backtest Data

Use `data_source: "csv"` and set `data_path` to the directory containing `<SYMBOL>.csv`.

The CSV parser supports an optional first line of the form `# Metadata: {...}` and then expects the header row.

Bundled examples live under `data/sp500/`. Supported CSV columns are:
`Date,Open,High,Low,Close,Volume`

For offline, out-of-the-box CSV backtests, the repo includes small synthetic OHLC datasets for:
`AAPL`, `MSFT`, `GOOGL`, `TSLA`. Other `data/sp500/<SYMBOL>.csv` files are metadata/header placeholders.

Run backtest API with CSV:

```bash
curl -X POST http://localhost:8080/backtest -H 'Content-Type: application/json' -d '{
  "symbol": "AAPL",
  "start_date": "2024-01-01",
  "end_date": "2024-01-25",
  "initial_capital": 10000,
  "data_source": "csv",
  "data_path": "data/sp500",
  "strategy": { "indicators": [{"type": "SMA", "length": 5}] }
}'
```
# BackTest Pro Backend

A high-performance C++ backtesting engine with HTTP API for algorithmic trading strategies.

## Features

- **High Performance**: C++ implementation for fast backtesting calculations
- **Multiple Strategies**: Support for Moving Average, RSI, MACD, Bollinger Bands
- **HTTP API**: RESTful API for backend clients and integrations
- **Data Sources**: CSV files, API data providers (Alpha Vantage)
- **Risk Management**: Built-in risk controls and position sizing
- **Performance Analytics**: Comprehensive metrics and reporting

## Tech Stack

- **Language**: C++20
- **Build System**: CMake
- **HTTP Server**: Custom HTTP server implementation
- **Libraries**: 
  - Eigen3 (linear algebra)
  - spdlog (logging)
  - nlohmann/json (JSON parsing)
  - yaml-cpp (configuration)
  - libcurl (HTTP client)
  - TA-Lib (technical indicators)

## Project Structure

```
├── src/
│   ├── engine/              # Core backtesting engine
│   ├── strategy/            # Trading strategies
│   ├── data/                # Data handlers
│   ├── portfolio/           # Portfolio management
│   ├── execution/           # Order execution
│   ├── risk/                # Risk management
│   ├── performance/         # Performance analytics
│   ├── cli/                 # Command line interface
│   ├── main.cpp             # CLI entry point
│   └── server.cpp           # HTTP server entry point
├── include/
│   ├── engine/              # Engine headers
│   ├── strategy/            # Strategy headers
│   ├── data/                # Data handler headers
│   ├── portfolio/           # Portfolio headers
│   ├── execution/           # Execution headers
│   ├── risk/                # Risk management headers
│   ├── performance/         # Performance headers
│   ├── cli/                 # CLI headers
│   └── common/              # Common types and utilities
├── CMakeLists.txt           # Build configuration
├── build.sh                 # Build script
├── Dockerfile               # Container configuration
└── Dockerfile               # Container configuration
```

## Prerequisites

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    libcurl4-openssl-dev \
    libeigen3-dev
```

### macOS
```bash
brew install cmake pkg-config yaml-cpp nlohmann-json spdlog curl eigen
```

### Optional: TA-Lib
```bash
# Ubuntu/Debian
sudo apt-get install libta-lib-dev

# macOS
brew install ta-lib
```

## Building

### Quick Build
```bash
./build.sh
```

### Manual Build
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

## Using as a C++ library (for third-party apps)

This project exports a shared library target named `BacktestingEngine::backtesting_engine_shared`.

### Current release

Start by picking a single release tag/version for both:
`FetchContent` (the `GIT_TAG`) and your installed artifacts used by `find_package`.

At the moment, the recommended tag is `v1.0.0`.

### Option A (Recommended): `find_package` after install

Build + install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build -j
cmake --install build
```

Build/install from the same release tag you selected above (currently `v1.0.0`).

In your app’s `CMakeLists.txt`:

```cmake
find_package(BacktestingEngine CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE BacktestingEngine::backtesting_engine_shared)
```

If CMake can’t find the package automatically, configure one of:
- `CMAKE_PREFIX_PATH` to include `/your/prefix`
- `BacktestingEngine_DIR` to point at `${prefix}/lib/cmake/BacktestingEngine`

The installed package version corresponds to the release tag you built (e.g. `v1.0.0`).

### Option B: CMake `FetchContent` (no manual install)

```cmake
include(FetchContent)

FetchContent_Declare(
  backtesting_engine
  GIT_REPOSITORY https://github.com/Vins-z/Backtesting-Engine.git
  GIT_TAG v1.0.0 # keep in sync with "Current release"
)

FetchContent_MakeAvailable(backtesting_engine)

target_link_libraries(your_target PRIVATE BacktestingEngine::backtesting_engine_shared)
```

Note: `FetchContent_MakeAvailable` expects `backtesting_engine_shared` to be available as a target after adding this project. (This is true for the current build.)

### Option C: GitHub release tarball/zip

Download the release tarball, extract it, and follow **Option A**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build -j
cmake --install build
```

### Windows runtime note

Because this library is a shared object, your application must be able to locate its runtime DLLs.
After installing, ensure `%PATH%` includes `${prefix}/bin` (or copy the DLL next to your executable).

### Minimal C++ usage

```cpp
#include "engine/backtest_engine.h"
#include <iostream>

int main() {
  backtesting::BacktestConfig cfg{};
  cfg.name = "example";
  cfg.symbols = {"AAPL"};
  cfg.start_date = "2024-01-01";
  cfg.end_date = "2024-01-25";
  cfg.initial_capital = 10000.0;
  cfg.commission_rate = 0.001;
  cfg.slippage_rate = 0.0005;
  cfg.data_source = "csv";
  cfg.data_path = "data/sp500";
  cfg.data_interval = "1d";
  cfg.strategy_name = "moving_average";
  cfg.strategy_params = {{"short_window", 5}, {"long_window", 10}};

  auto engine = backtesting::BacktestEngine::create_from_config(cfg);
  auto result = engine->run_backtest();
  std::cout << result.to_json().dump(2) << std::endl;
  return 0;
}
```

## Using the C API (for bindings)

The shared library exports a tiny C API in `include/c_api/backtest_c.h`:

- `bt_create_from_config_json(const char*)`
- `bt_run_to_result_json(BacktestEngineHandle*)`
- `bt_free_string(char*)`
- `bt_destroy(BacktestEngineHandle*)`

Full JSON key reference + C examples are documented in `docs/C_API.md`.

## Usage

### CLI Mode
```bash
./backtest_engine --config config.yaml
```

### Server Mode
```bash
./backtest_server
```

The server will start on port 8080 (or the PORT environment variable).

## API Endpoints

### Health Check
```http
GET /health
```

Response:
```json
{
  "status": "healthy"
}
```

### Run Backtest
```http
POST /backtest
Content-Type: application/json

{
  "symbol": "AAPL",
  "start_date": "2023-01-01",
  "end_date": "2023-12-31",
  "initial_capital": 10000,
  "strategy": {
    "name": "Moving Average Crossover",
    "parameters": {
      "fast_period": 10,
      "slow_period": 30
    }
  }
}
```

## Configuration

Create a `config.yaml` file:

```yaml
data:
  source: "csv"
  path: "data/sp500"

strategy:
  name: "moving_average"
  parameters:
    fast_period: 10
    slow_period: 30

portfolio:
  initial_capital: 10000
  commission: 0.001
  slippage: 0.0001

risk:
  max_position_size: 0.1
  stop_loss: 0.05
  take_profit: 0.1
```

## Docker

### Build Image
```bash
docker build -t backtesting-engine .
```

### Run Container
```bash
docker run -p 8080:8080 backtesting-engine
```

## Deployment

### Docker / container deployment

The backend can be deployed anywhere that supports Docker:

- Google Cloud Run
- AWS ECS
- DigitalOcean App Platform
- Railway

You can also build and run locally:

```bash
docker build -t backtesting-engine .
docker run -p 8080:8080 backtesting-engine
```

## Development

### Adding New Strategies

1. Create strategy class in `src/strategy/`
2. Add header in `include/strategy/`
3. Register in `strategy_factory.cpp`
4. Update CMakeLists.txt

### Adding New Data Sources

1. Create data handler in `src/data/`
2. Add header in `include/data/`
3. Register in `data_handler_factory.cpp`
4. Update CMakeLists.txt

## Performance

The C++ backend is optimized for performance:
- **Memory efficient**: Minimal memory allocation during backtesting
- **Fast calculations**: Vectorized operations with Eigen3
- **Concurrent processing**: Multi-threaded data processing
- **Optimized I/O**: Efficient file and network operations

### Reproducible benchmark harness

```bash
chmod +x scripts/benchmark.sh
./scripts/benchmark.sh config.yaml
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

This project is licensed under the MIT License.
