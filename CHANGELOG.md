# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-11-05

### Added
- `protocol-codegen` command line tool that reads a JSON protocol schema (messages, fields, enums, endianness, CRC, versions) with path-precise error messages.
- C++17 generator emitting a single self-contained header with bounds-checked, allocation-free `noexcept` encode/decode, enum and boolean range checks, and CRC verification before payload access.
- Wireshark Lua dissector generator derived from the same schema, so firmware and ground tools cannot drift apart.
- `--check` mode that fails when committed generated files are stale, plus `--validate`, `--cpp-only`, `--lua-only` and `-o` options.
- Example schemas (`telemetry_link`, `nav_bus`) with generated output and a `demo` program.
- Unit, golden-output and round-trip tests run through CTest, with CI on GCC, Clang and ASan/UBSan.

[Unreleased]: https://github.com/bekirhankarakurt/protocol-codegen/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bekirhankarakurt/protocol-codegen/releases/tag/v1.0.0
