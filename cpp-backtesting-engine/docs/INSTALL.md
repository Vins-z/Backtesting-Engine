# Install & Consume (Public Integration)

This repository exports a shared library target named:

- `BacktestingEngine::backtesting_engine_shared`

You can integrate it into your application using one of the options below.

## Prerequisites

- C++20 compiler
- CMake 3.20+
- Dependencies installed (Eigen3, spdlog, nlohmann/json, yaml-cpp, libcurl, pthread)
- Optional: TA-Lib (some indicators may be unavailable if not installed)

## Current release

The recommended release tag is `v1.0.1`.
When using `FetchContent`, set `GIT_TAG` to the same version.

## Option A (Recommended): Build + install, then `find_package`

### Build + install

From `Backtesting/cpp-backtesting-engine/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build -j
cmake --install build
```

### Consumer CMake

```cmake
find_package(BacktestingEngine CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE BacktestingEngine::backtesting_engine_shared)
```

If CMake can’t locate the package, set one of:

- `CMAKE_PREFIX_PATH=/your/prefix`
- `BacktestingEngine_DIR=/your/prefix/lib/cmake/BacktestingEngine`

### Windows runtime note

Because the library is a shared object, your application must be able to locate its runtime DLLs.
After installing, ensure `%PATH%` includes `${prefix}/bin` (or copy the DLL next to your executable).

## Option B: `FetchContent` (no manual install step)

```cmake
include(FetchContent)

FetchContent_Declare(
  backtesting_engine
  GIT_REPOSITORY https://github.com/Vins-z/Backtesting-Engine.git
  GIT_TAG v1.0.1 
)

FetchContent_MakeAvailable(backtesting_engine)

target_link_libraries(your_target PRIVATE BacktestingEngine::backtesting_engine_shared)
```

## Option C: GitHub release tarball/zip

Download the release tarball/zip, extract it, then follow **Option A** (build + install + `find_package`).

