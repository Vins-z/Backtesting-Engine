# Backend Benchmarking

This folder contains reproducible benchmark outputs for the C++ engine.

## Run

```bash
cd cpp-backtesting-engine
chmod +x scripts/benchmark.sh
./scripts/benchmark.sh config.yaml
```

## Output

Benchmark runs write timestamped files under `benchmarks/results/` containing:
- command and config used
- wall-clock timing
- memory usage from `/usr/bin/time`

## Reporting policy

Only publish "fastest" claims backed by benchmark outputs committed from this harness.
