#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/benchmarks/results"
mkdir -p "${OUT_DIR}"

if [[ ! -x "${BUILD_DIR}/backtest_engine" ]]; then
  echo "Building backtest_engine..."
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
  cmake --build "${BUILD_DIR}" --parallel
fi

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <config.yaml>"
  exit 1
fi

CONFIG_PATH="$1"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_FILE="${OUT_DIR}/benchmark-${STAMP}.txt"

echo "Running benchmark with config: ${CONFIG_PATH}" | tee "${OUT_FILE}"
/usr/bin/time -lp "${BUILD_DIR}/backtest_engine" --config "${CONFIG_PATH}" >> "${OUT_FILE}" 2>&1
echo "Benchmark written to ${OUT_FILE}"
