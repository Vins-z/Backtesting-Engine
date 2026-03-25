# ADR 0001: Backend Module Boundaries

## Status
Accepted

## Context
`server.cpp` carries transport, orchestration, cache updates, and execution concerns in one place.

## Decision
Move toward clear boundaries:
- Transport: socket/http handling only.
- Services: market data refresh, backtest orchestration.
- Domain: engine, portfolio, execution, risk.

As an immediate step, request handling now uses a bounded worker queue to separate connection acceptance from request execution.

## Consequences
- Better stability under load.
- Clear path for extracting transport/service modules into separate compilation units.
