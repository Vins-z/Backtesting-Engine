# Contributing

## Development setup

1. Build backend:
   - `cd cpp-backtesting-engine && ./build.sh`
2. Run backend tests:
   - `cd cpp-backtesting-engine && ctest --test-dir build --output-on-failure`

## Pull requests

- Keep PRs focused and small.
- Add tests for behavior changes.
- Update docs for any API or config changes.
- Ensure CI is green before requesting review.

## Commit guidance

- Use clear messages explaining why the change exists.
- Prefer one logical change per commit.
