#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to an engine instance.
typedef struct BacktestEngineHandle BacktestEngineHandle;

// Create an engine from a JSON configuration string.
// Returns NULL on error.
BacktestEngineHandle* bt_create_from_config_json(const char* config_json);

// Run the backtest and return a newly allocated JSON string of the result.
// Returns NULL on error.
// The caller must free the returned string using bt_free_string().
char* bt_run_to_result_json(BacktestEngineHandle* h);

// Destroy an engine handle (safe to call with NULL).
void bt_destroy(BacktestEngineHandle* h);

// Free strings returned by this API (safe to call with NULL).
void bt_free_string(char* s);

#ifdef __cplusplus
} // extern "C"
#endif

