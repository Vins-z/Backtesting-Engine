#!/bin/bash

echo "Starting backtesting server..."
export DATA_CSV_PATH=${DATA_CSV_PATH:-"$(pwd)/cache/csv"}

# Set library paths for macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    export DYLD_LIBRARY_PATH="/opt/homebrew/lib:$DYLD_LIBRARY_PATH"
    export DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/lib:/usr/local/lib:$DYLD_FALLBACK_LIBRARY_PATH"
    echo "Set DYLD_LIBRARY_PATH for macOS: $DYLD_LIBRARY_PATH"
fi

# Load environment from .env.local if present (local dev)
if [ -f ".env.local" ]; then
    echo "Loading environment variables from .env.local"
    # shellcheck disable=SC2046
    export $(grep -v '^#' .env.local | xargs)
fi

# Check for existing server processes and kill them
echo "Checking for existing server processes..."
EXISTING_PIDS=$(lsof -ti :8080 2>/dev/null || true)
if [ -n "$EXISTING_PIDS" ]; then
    echo "Found existing server processes on port 8080: $EXISTING_PIDS"
    echo "Stopping existing processes..."
    echo "$EXISTING_PIDS" | xargs kill -9 2>/dev/null || true
    sleep 2
    echo "Existing processes stopped"
else
    echo "No existing server processes found"
fi

# Try to locate and run the server binary
SERVER_BIN=""
if [ -x "./backtest_server" ]; then
  SERVER_BIN="./backtest_server"
elif [ -x "./build/backtest_server" ]; then
  SERVER_BIN="./build/backtest_server"
elif [ -x "../build/backtest_server" ]; then
  SERVER_BIN="../build/backtest_server"
elif [ -x "./build/backtest_server/backtest_server" ]; then
  SERVER_BIN="./build/backtest_server/backtest_server"
fi

if [ -n "$SERVER_BIN" ]; then
  echo "Launching: $SERVER_BIN (DATA_CSV_PATH=$DATA_CSV_PATH)"
  echo "Server will run in foreground. Press Ctrl+C to stop."
  if "$SERVER_BIN"; then
      echo "Server started successfully"
      exit 0
  else
      echo "Server exited with error code $?"
      echo "Running debug script..."
      ./debug_libs.sh
      exit 1
  fi
else
  echo "Server binary not found in any expected location"
  echo "Please ensure the server is built: cd build && make"
  exit 1
fi
