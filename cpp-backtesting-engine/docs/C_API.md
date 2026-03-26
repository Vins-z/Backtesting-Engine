# C API (JSON in / JSON out)

The shared library exports a minimal C API in:

- `include/c_api/backtest_c.h`

## Function overview

```c
typedef struct BacktestEngineHandle BacktestEngineHandle;

BacktestEngineHandle* bt_create_from_config_json(const char* config_json);
char* bt_run_to_result_json(BacktestEngineHandle* h);
void bt_destroy(BacktestEngineHandle* h);
void bt_free_string(char* s);
```

## JSON schema (what keys are used)

The wrapper reads these fields from the JSON object (unknown keys are ignored):

### Required
- `start_date` (string, format expected by the engine)
- `end_date` (string)
- `symbols` (array of strings) OR `symbol` (single string)

### Optional (defaults shown)
- `name` (string, default: `"library_backtest"`)
- `description` (string, default: `""`)
- `initial_capital` (number, default: `100000.0`)
- `commission_rate` (number, default: `0.001`)  
  - alias: `commission`
- `slippage_rate` (number, default: `0.001`)  
  - alias: `slippage`
- `data_source` (string, default: `"csv"`)
- `data_path` (string, default: `""`)
- `data_interval` (string, default: `"1d"`)
- `api_key` (string, default: `""`)
- `strategy_name` (string, default: `"moving_average"`)
- `strategy_params` (object mapping string -> number, default: `{}`)
- `strategy_definition_json` (object, optional; forwarded to the engine)

## Ownership / memory rules

1. `bt_create_from_config_json` returns an opaque handle. Call `bt_destroy(h)` when finished.
2. `bt_run_to_result_json(h)` returns a newly allocated C string (uses `malloc` internally).
   - Free it with `bt_free_string(str)` (do not use `free()` directly in calling code).

## Minimal C example

```c
#include "c_api/backtest_c.h"
#include <stdio.h>

int main() {
  const char* cfg =
    "{"
    "  \"symbol\": \"AAPL\","
    "  \"start_date\": \"2024-01-01\","
    "  \"end_date\": \"2024-01-25\","
    "  \"initial_capital\": 10000,"
    "  \"commission_rate\": 0.001,"
    "  \"slippage_rate\": 0.0005,"
    "  \"data_source\": \"csv\","
    "  \"data_path\": \"data/sp500\","
    "  \"data_interval\": \"1d\","
    "  \"strategy_name\": \"moving_average\","
    "  \"strategy_params\": {\"short_window\": 5, \"long_window\": 10}"
    "}";

  BacktestEngineHandle* h = bt_create_from_config_json(cfg);
  if (!h) {
    fprintf(stderr, "Failed to create engine\\n");
    return 1;
  }

  char* result_json = bt_run_to_result_json(h);
  if (!result_json) {
    bt_destroy(h);
    fprintf(stderr, "Backtest failed\\n");
    return 1;
  }

  printf("%s\\n", result_json);
  bt_free_string(result_json);
  bt_destroy(h);
  return 0;
}
```

## Notes for CSV data

When `data_source` is `"csv"`, `data_path` must point to a directory containing:

- `<SYMBOL>.csv` files

