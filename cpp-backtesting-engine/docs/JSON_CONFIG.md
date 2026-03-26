# JSON config for `bt_create_from_config_json`

This JSON is the input to:

- `bt_create_from_config_json(const char* config_json)`

It is parsed by the C API wrapper and converted into `backtesting::BacktestConfig`.

## Example (offline CSV, synthetic `data/sp500/AAPL.csv`)

```json
{
  "symbol": "AAPL",
  "start_date": "2024-01-01",
  "end_date": "2024-01-25",
  "initial_capital": 10000,

  "commission_rate": 0.001,
  "slippage_rate": 0.0005,

  "data_source": "csv",
  "data_path": "data/sp500",
  "data_interval": "1d",

  "strategy_name": "moving_average",
  "strategy_params": {
    "short_window": 5,
    "long_window": 10
  }
}
```

## Required fields (practical)

If `start_date`, `end_date`, and `symbols`/`symbol` are missing, configuration validation will fail inside the engine.

