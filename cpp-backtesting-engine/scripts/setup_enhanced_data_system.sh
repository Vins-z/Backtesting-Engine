#!/usr/bin/env bash
set -euo pipefail

echo "=== C++ Data System Setup ==="

if [ ! -f "CMakeLists.txt" ]; then
  echo "Run this script from cpp-backtesting-engine."
  exit 1
fi

mkdir -p cache data/csv data/reports build
echo "Created cache and data directories."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
echo "Build completed."

ctest --test-dir build --output-on-failure
echo "CTest suite completed."

cat > .env << 'EOF'
# C++ engine runtime configuration
DATA_SOURCE=yfinance
CACHE_DIR=./cache
CACHE_EXPIRY_HOURS=24
# Optional: ALPHA_VANTAGE_API_KEY=your_key_here
EOF

cat > data_config.json << 'EOF'
{
  "data_source": "yfinance",
  "cache_directory": "./cache",
  "cache_expiry_hours": 24,
  "fallback_sources": ["alpha_vantage", "csv"]
}
EOF

echo "Wrote .env and data_config.json."
echo "Setup complete."
