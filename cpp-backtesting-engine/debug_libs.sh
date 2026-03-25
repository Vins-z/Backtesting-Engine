#!/bin/bash

echo "=== Debugging Shared Library Issues ==="
echo "Current directory: $(pwd)"
echo "Executable: $(ls -la backtest_server 2>/dev/null || echo 'backtest_server not found')"
echo ""

echo "=== Checking library dependencies ==="
if [ -f "backtest_server" ]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "Running otool on backtest_server (macOS):"
        otool -L ./backtest_server 2>&1 || echo "otool failed"
    else
        echo "Running ldd on backtest_server:"
        ldd ./backtest_server 2>&1 || echo "ldd failed"
    fi
else
    echo "backtest_server not found in current directory"
fi
echo ""

echo "=== Checking library cache ==="
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Checking Homebrew libraries:"
    ls -la /opt/homebrew/lib/ | grep -E "(spdlog|yaml|curl)" || echo "No Homebrew libraries found"
else
    echo "ldconfig -p | grep spdlog:"
    ldconfig -p | grep spdlog || echo "No spdlog libraries found"
fi
echo ""

echo "=== Checking common library locations ==="
if [[ "$OSTYPE" == "darwin"* ]]; then
    for lib in libspdlog.dylib libyaml-cpp.dylib libcurl.dylib; do
        echo "Looking for $lib:"
        find /opt/homebrew -name "$lib" 2>/dev/null | head -5 || echo "Not found in Homebrew"
        find /usr/local -name "$lib" 2>/dev/null | head -5 || echo "Not found in /usr/local"
    done
else
    for lib in libspdlog.so.1 libyaml-cpp.so.0.7 libcurl.so.4; do
        echo "Looking for $lib:"
        find /usr -name "$lib" 2>/dev/null | head -5 || echo "Not found"
    done
fi
echo ""

echo "=== Environment variables ==="
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "DYLD_LIBRARY_PATH: $DYLD_LIBRARY_PATH"
else
    echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
fi
echo "PATH: $PATH"
echo ""

echo "=== Package information ==="
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Homebrew packages:"
    brew list | grep -E "(spdlog|yaml-cpp|curl)" || echo "No relevant Homebrew packages found"
else
    dpkg -l | grep -E "(spdlog|yaml-cpp|curl)" || echo "No relevant packages found"
fi
echo ""

echo "=== Attempting to run with verbose output ==="
if [ -f "backtest_server" ]; then
    echo "Running: ./backtest_server --help"
    ./backtest_server --help 2>&1 || echo "Failed to run backtest_server"
fi
