#!/bin/bash

# Build script for C++ Backtesting Engine
set -e

echo "Building C++ Backtesting Engine..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 -DSPDLOG_BUILD_SHARED=ON

# Build the project
echo "Building project..."
# Use different commands for different platforms
if command -v nproc &> /dev/null; then
    make -j$(nproc)
elif command -v sysctl &> /dev/null; then
    # macOS
    make -j$(sysctl -n hw.ncpu)
else
    # Fallback to single-threaded build
    make
fi

echo "Build completed!"
echo "CLI Executable location: $(pwd)/backtest_engine"
echo "Server Executable location: $(pwd)/backtest_server"

# Make sure the executables are in the right place
if [ -f "backtest_engine" ]; then
    echo "✓ backtest_engine executable created successfully"
    # Test the executable
    echo "Testing backtest_engine..."
    ./backtest_engine --help || echo "Note: backtest_engine may not have --help option"
else
    echo "✗ Failed to create backtest_engine executable"
    exit 1
fi

if [ -f "backtest_server" ]; then
    echo "✓ backtest_server executable created successfully"
    
    # Skip server testing if in Docker build or if SKIP_SERVER_TEST is set
    if [ "$DOCKER_BUILD" = "true" ] || [ "$SKIP_SERVER_TEST" = "true" ]; then
        echo "Skipping server test (Docker build or SKIP_SERVER_TEST set)"
    else
        # Test the executable
        echo "Testing backtest_server..."
        
        # Start server in background
        PORT=8080 ./backtest_server &
        SERVER_PID=$!
        
        # Wait for server to start (up to 5 seconds)
        for i in {1..5}; do
            if curl -s http://localhost:8080/health > /dev/null; then
                echo "Server started successfully"
                break
            fi
            if [ $i -eq 5 ]; then
                echo "Server failed to start"
                kill $SERVER_PID
                exit 1
            fi
            sleep 1
        done
        
        # Test health endpoint
        HEALTH_RESPONSE=$(curl -s http://localhost:8080/health)
        if [[ $HEALTH_RESPONSE == *"healthy"* ]]; then
            echo "Health check passed"
        else
            echo "Health check failed"
            kill $SERVER_PID
            exit 1
        fi
        
        # Gracefully stop the server
        echo "Stopping server..."
        kill -TERM $SERVER_PID
        
        # Wait for server to stop (up to 10 seconds)
        for i in {1..10}; do
            if ! kill -0 $SERVER_PID 2>/dev/null; then
                echo "Server stopped successfully"
                break
            fi
            if [ $i -eq 10 ]; then
                echo "Server failed to stop gracefully, forcing shutdown"
                kill -9 $SERVER_PID
                # Don't exit with error code if build succeeded - this is just cleanup
                echo "Note: Build completed successfully, server shutdown timeout is non-critical"
            fi
            sleep 1
        done
    fi
else
    echo "✗ Failed to create backtest_server executable"
    exit 1
fi

echo ""
echo "Backend build complete!"
echo "CLI Usage: ./backtest_engine --config config.yaml"
echo "Server Usage: ./backtest_server (listens on PORT environment variable, defaults to 8080)"
