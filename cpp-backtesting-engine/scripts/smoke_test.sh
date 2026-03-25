#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://localhost:8080}"

echo "Running backend smoke tests against ${BASE_URL}" 

curl -sS -m 20 "${BASE_URL}/health" | jq . || { echo "Health check failed"; exit 1; }

curl -sS -m 20 "${BASE_URL}/api/market-data/AAPL" || echo "Market data endpoint may not be initialized yet"

curl -sS -m 30 -X POST "${BASE_URL}/backtest" \
  -H "Content-Type: application/json" \
  -d '{
    "symbol": "AAPL",
    "start_date": "2024-01-01",
    "end_date": "2024-02-01",
    "initial_capital": 10000,
    "data_source": "yfinance",
    "data_interval": "1d",
    "strategy": {"indicators": [{"type": "SMA", "length": 20}]}
  }' || echo "Backtest endpoint returned an error"

curl -sS -m 30 -X POST "${BASE_URL}/api/portfolio/backtest" \
  -H "Content-Type: application/json" \
  -d '{
    "symbols": ["AAPL", "GOOGL", "MSFT"],
    "weights": [0.4, 0.3, 0.3],
    "start_date": "2024-01-01",
    "end_date": "2024-02-01",
    "initial_capital": 10000
  }' || echo "Portfolio endpoint returned an error"

echo "Smoke tests completed (inspect output above for any failures)." 
