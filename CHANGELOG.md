# Changelog

All notable changes to this project should be documented here.

## Unreleased

- Removed frontend and moved to backend-first repository shape.
- Removed Python engine and standardized repo to C++ backend only.
- Renamed backend folder to `cpp-backtesting-engine` for clearer project structure.
- Added backend CI and sanitizer build checks.
- Fixed key correctness issues in threading, risk units, CSV date filtering, and async lifecycle.
- Exposed `cpp-backtesting-engine` as an installable shared library via CMake `find_package`, added a minimal C JSON API wrapper, and published public integration docs + examples.
- Removed Render deployment artifacts/instructions to keep deployments platform-agnostic.
