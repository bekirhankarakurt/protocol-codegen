# Contributing

Thanks for your interest. Bug reports, fixes and focused features are welcome.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPCG_WERROR=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Before opening a pull request, also run the sanitizer build:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPCG_WERROR=ON -DPCG_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

If you change a generator or an example schema, regenerate `examples/generated/` (the `cli_check_*` tests fail on stale output).

## Style

- Modern C++17, zero warnings with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- No dependencies beyond the standard library.
- New behavior comes with a test.

## Commits and pull requests

- Small, logical commits with plain imperative messages, e.g. `Add int64 field support`.
- One topic per pull request; describe what changed and how you tested it.
- Update `README.md` and `CHANGELOG.md` (under "Unreleased") when behavior or options change.
