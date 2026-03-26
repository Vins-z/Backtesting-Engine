# CMake integration snippets

## `find_package` flow (after `cmake --install`)

## Current release

The recommended release tag is `v1.0.0`.
When using `FetchContent`, keep `GIT_TAG` set to the same version.

### Install (one-time)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build -j
cmake --install build
```

### Consume (in your app)

```cmake
find_package(BacktestingEngine CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE BacktestingEngine::backtesting_engine_shared)
```

If your prefix is not on the default CMake search paths:

- pass `-DCMAKE_PREFIX_PATH=/your/prefix` when configuring your app, or
- pass `-DBacktestingEngine_DIR=/your/prefix/lib/cmake/BacktestingEngine`

## `FetchContent` flow (no manual install)

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

