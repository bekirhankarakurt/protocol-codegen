# protocol-codegen

Describe a binary link once in JSON; get a bounds-checked, CRC-framed C++17 serializer and a Wireshark Lua dissector that can never drift apart.

[![CI](https://github.com/bekirhankarakurt/protocol-codegen/actions/workflows/ci.yml/badge.svg)](https://github.com/bekirhankarakurt/protocol-codegen/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](#requirements)

## Why / what

- **One source of truth.** Messages, fields, enums, endianness, CRC and versions live in one schema; firmware and ground tools stop disagreeing about byte offsets.
- **Safe by construction.** Generated code never indexes past a buffer, rejects out-of-range enums and booleans, and verifies a CRC before any payload is touched.
- **Zero dependencies at run time.** Output is a single self-contained header: `noexcept` message encode/decode with no allocation and no exceptions (only the `encode_frame` convenience wrapper uses `std::vector`).
- **Debuggable on the wire.** The Lua dissector shows decoded fields, enum names and a CRC verdict in Wireshark, using the same offsets as the C++ code.
- **Tested end to end.** Golden-file tests pin the generator output; round-trip, bit-flip, truncation and fuzz tests run the generated code under ASan/UBSan.

## Demo

`build/demo` encodes five messages with the generated `tlink.hpp`, decodes them back and shows corruption detection (real output):

```text
$ ./build/demo
Heartbeat      A5 02 01 00 0E 00 40 E2 01 00 02 01 42 41 59 4B 41 52 30 31 B7 B9
Attitude       A5 02 02 00 18 00 CD CC 4C 3D 8F C2 F5 BD C3 F5 C8 3F 00 00 80 3F 00 00 20 C0 00 00 80 3E CD 0D
GpsFix         A5 02 03 00 1F 00 40 22 20 18 24 0A 06 00 D0 5A 71 18 C0 C0 45 11 C0 51 02 00 92 09 0E CD CC CC CC CC CC EC 3F 6C 41
Command        A5 02 0A 00 13 00 01 00 01 00 00 40 40 00 00 00 00 00 00 00 00 00 00 00 00 C5 0C
BatteryStatus  A5 02 14 00 0D 00 8C 0F 87 0F 96 0F 80 0F 2C CF FF FF 48 01 44

Decoding:
  Heartbeat      Ok (22 bytes)
  Attitude       Ok (32 bytes)
  GpsFix         Ok (39 bytes)
  Command        Ok (27 bytes)
  BatteryStatus  Ok (21 bytes)

Flipped one payload bit: BadCrc
Cut the frame short:     Truncated
```

`./build/demo out.pcap` also writes those frames as UDP packets. Opened with the generated dissector (`tshark -X lua_script:examples/generated/tlink.lua -r out.pcap -V`), frame 1 reads:

```text
Telemetry Link
    Sync: 0xa5
    Version: 2
    Message ID: Heartbeat (1)
    Payload length: 14
    Heartbeat
        uptime_ms: 123456
        mode: FLIGHT (2)
        armed: true (1)
        callsign: BAYKAR01
    CRC-16: 0xb9b7 [correct]
```

## Quick start (60 seconds)

```bash
git clone https://github.com/bekirhankarakurt/protocol-codegen.git && cd protocol-codegen
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

```bash
./build/protocol-codegen examples/telemetry_link.json -o out    # writes out/tlink.hpp and out/tlink.lua
./build/demo demo.pcap                                          # encode/decode demo + pcap for Wireshark
```

Use the header from your code (`-Iout`):

```cpp
#include "tlink.hpp"

tlink::CommandAck ack;
ack.command = tlink::CommandId::REBOOT;
ack.result = tlink::CommandResult::ACCEPTED;

std::vector<std::uint8_t> wire;
if (tlink::encode_frame(ack, wire) != tlink::Status::Ok) { /* handle */ }

tlink::Message message;                       // std::variant of all messages
std::size_t used = 0;
if (tlink::decode_frame(wire.data(), wire.size(), message, &used) == tlink::Status::Ok) {
    std::visit([](const auto& m) { std::puts(m.kName); }, message);
}
```

## Requirements

| Requirement | Version | Needed for |
|---|---|---|
| C++17 compiler | GCC 11+, Clang 14+ (CI: current Ubuntu GCC and Clang) | generator, tests, generated header |
| CMake | 3.16+ | build |
| Wireshark / tshark | 3.x or 4.x (tested with 4.2) | optional, to use the Lua dissector |
| Lua (`luac`) | 5.2+ | optional, syntax-checking the dissector |

No third-party libraries. Hardware is never required: the demo and tests are the replay/simulation path.

### Build options

| CMake option | Default | Effect |
|---|---|---|
| `PCG_BUILD_TESTS` | `ON` | Build the CTest suite |
| `PCG_BUILD_EXAMPLES` | `ON` | Build `demo` |
| `PCG_WERROR` | `OFF` | Warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`) become errors |
| `PCG_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) |
| `CMAKE_BUILD_TYPE` | `Release` | Standard CMake build type |

## Usage

```text
protocol-codegen <schema.json> [options]
```

| Option | Description |
|---|---|
| `-o, --out DIR` | Output directory (default: current directory) |
| `--cpp-only` | Generate only `<name>.hpp` |
| `--lua-only` | Generate only `<name>.lua` |
| `--check` | Compare existing outputs with fresh ones, write nothing; exit 1 if stale |
| `--validate` | Parse and validate the schema only |
| `-h, --help` / `--version` | Help / version |

Exit codes: `0` success, `1` schema/I/O error or stale output, `2` usage error. Schema errors name the offending path, e.g. `error: messages[5].fields[2].name: 'detail' is a reserved word`.

### Recipes

**1. Generate and compile into a project**

```bash
./build/protocol-codegen link.json -o include/generated
```
Commit the output; the header has no build-time dependency on this tool.

**2. Fail CI when generated files are stale**

```bash
./build/protocol-codegen examples/nav_bus.json --check -o examples/generated   # prints "up to date"
```

**3. Inspect a capture in Wireshark**

```bash
wireshark -X lua_script:examples/generated/tlink.lua demo.pcap
tshark -X lua_script:examples/generated/tlink.lua -r demo.pcap -Y tlink -T fields -e _ws.col.info -e tlink.crc
```
Frames are matched on the schema's `udp_port`. Wireshark disables Lua when started as root; run it as a normal user.

**4. Parse a byte stream with resynchronisation**

```cpp
std::size_t offset = 0;
while (offset < buffer.size()) {
    tlink::Message message;
    std::size_t used = 0;
    const tlink::Status status = tlink::decode_frame(buffer.data() + offset, buffer.size() - offset, message, &used);
    if (status == tlink::Status::Ok)             { handle(message); offset += used; }
    else if (status == tlink::Status::Truncated) { break; }       // wait for more bytes
    else                                         { ++offset; }    // skip a byte, hunt for next sync
}
```

**5. Talk to an older peer**

```cpp
tlink::encode_frame(heartbeat, wire, /*version=*/1);      // Ok: Heartbeat exists in v1
tlink::encode_frame(battery,   wire, /*version=*/1);      // UnsupportedVersion: BatteryStatus is since 2
```

## Architecture

```text
 schema.json
     |
     v
 +-----------+     +----------------+     +-------------------+
 | json.cpp  | --> |  schema.cpp    | --> | emit_cpp.cpp      | --> <name>.hpp  (header-only codec)
 | parser    |     | typed model +  |     +-------------------+
 +-----------+     | validation     |     +-------------------+
                   +----------------+ --> | emit_lua.cpp      | --> <name>.lua  (Wireshark dissector)
                                          +-------------------+
 main.cpp: CLI (--check / --validate / -o)

 Generated header:  Writer/Reader (bounds-checked) -> Message::encode/decode -> encode_frame/decode_frame
```

Design trade-offs:

| Decision | Why | Cost |
|---|---|---|
| Fixed-size messages only | Wire size is a compile-time constant; no allocation, trivial bounds proofs | No variable-length fields (use fixed arrays / `char[N]`) |
| Bitwise CRC, no lookup tables | Tiny, header-only, constant memory | Slower than table-driven CRC on large payloads |
| Sticky-error `Reader`/`Writer` | Generated code is branch-free; one status check per message | Error detail is the first failure only |
| Decode validates enums and bools | Corrupt-but-CRC-valid data cannot yield undefined enum values | Newer peers' unknown enum values are rejected, not passed through |
| Version in every frame, `since` per message | Mixed-version fleets can still exchange common messages | Fields cannot be added to an existing message without a new message ID |
| Own JSON parser (integers only) | Keeps the tool dependency-free | Schemas cannot contain fractional numbers |
| Lua CRC in pure arithmetic | Runs on Lua 5.2 - 5.4 inside any Wireshark build | Slow for very large frames (irrelevant at telemetry sizes) |

## Schema and wire format

Top-level keys:

| Key | Required | Meaning |
|---|---|---|
| `name` | yes | Lowercase identifier; becomes the C++ namespace, Lua protocol name and file stem |
| `title` | no | Human-readable name (Wireshark) |
| `version` | yes | Protocol version, 1-255 |
| `endianness` | no | `little` (default) or `big`; frame header and default for payloads |
| `crc` | no | `crc16` (default) or `crc32` |
| `sync` | yes | Frame start byte, 0-255 |
| `udp_port` | yes | UDP port the dissector registers for |
| `enums` | no | `{name, type, values}`; `type` is an integer type, `values` maps names to integers |
| `messages` | yes | `{name, id, since?, endianness?, doc?, fields}`; `id` 0-65535, `since` defaults to 1 |

Field types: `uint8` `int8` `uint16` `int16` `uint32` `int32` `uint64` `int64` `float32` `float64` `bool`, an enum name, any of these as `T[N]` (N = 1-65535), and `char[N]` for fixed-size text.

Frame layout (all header fields use the schema endianness):

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | sync | Fixed marker from the schema |
| 1 | 1 | version | Must be 1..schema version and >= the message's `since` |
| 2 | 2 | message id | |
| 4 | 2 | payload length | Must equal the message's fixed size |
| 6 | n | payload | Fields in declaration order, no padding |
| 6+n | 2 or 4 | CRC | Over bytes 1..6+n-1 |

| CRC | Algorithm | Check value for `"123456789"` |
|---|---|---|
| `crc16` | CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR | `0x29B1` |
| `crc32` | CRC-32/ISO-HDLC: reflected poly 0xEDB88320, init and final XOR 0xFFFFFFFF | `0xCBF43926` |

Encodings: integers and floats use the message endianness (IEEE-754 for floats); `bool` is one byte, `0` or `1`; enums use their underlying type; `char[N]` is raw bytes (NUL padding is the caller's convention).

`decode_frame` returns a `Status`:

| Status | Meaning |
|---|---|
| `Ok` | Frame decoded; `consumed` set |
| `Truncated` | More bytes needed |
| `BadSync` / `BadCrc` / `BadLength` | Framing, checksum or payload-size violation |
| `UnknownMessage` / `UnsupportedVersion` | Id not in schema / version too new or older than the message's `since` |
| `InvalidValue` | Enum or bool outside its domain |
| `BufferTooSmall` | Encoding target too small (low-level `Writer` use) |

## Testing

```bash
cmake -S . -B build -DPCG_WERROR=ON -DPCG_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j && ctest --test-dir build --output-on-failure
```

| Suite | Covers |
|---|---|
| `json` | Parser: escapes, ordering, error positions, limits, rejects malformed input |
| `schema` | Both example schemas; every validation rule and error message |
| `golden` | Generator output byte-identical to the committed `examples/generated/*` |
| `roundtrip` | Round-trip of all messages (both endiannesses, CRC-16/32), byte-exact wire format against independent CRC code, every truncation, every single-bit flip, version rules, 20,000 random/mutated frames |
| `cli_check_*` | `--check` confirms committed outputs are current |

The dissector was verified with tshark 4.2 against captures from `demo` (CRC verdicts, enums, arrays, big-endian and per-message endianness). CI runs GCC, Clang and a GCC ASan/UBSan job on Linux, all with `-Werror`.

## Troubleshooting / FAQ

- **`stale: examples/generated/...` from `--check`.** The schema or generator changed; re-run without `--check` and commit the output.
- **tshark shows plain `Data` instead of the protocol.** Check the packet uses the schema's `udp_port`, and that you are not root (Wireshark disables Lua for root).
- **`BadCrc` on a frame that looks right.** Both ends must agree on `crc` kind, endianness and the byte range: the CRC covers everything after the sync byte, up to the CRC itself.
- **`InvalidValue` on decode.** The peer sent an enum value your schema does not list; add it to the schema (older nodes will still reject it).
- **Can I add a field to an existing message?** No; that changes the wire size. Add a new message with `since` set to the new `version`.
- **Variable-length data?** Use a fixed array with a count field; the format is fixed-size by design.
- **`error: ... is a reserved word`.** Names are checked against C++ keywords and identifiers used by the generated code.

## Project layout

```text
.
├── CMakeLists.txt              build, options, tests
├── src/
│   ├── json.{hpp,cpp}          minimal JSON parser (integers only)
│   ├── schema.{hpp,cpp}        typed schema model and validation
│   ├── emit.hpp                generator API
│   ├── emit_cpp.cpp            header-only C++17 serializer generator
│   ├── emit_lua.cpp            Wireshark Lua dissector generator
│   └── main.cpp                command-line driver
├── examples/
│   ├── telemetry_link.json     telemetry/command link: little-endian, CRC-16, versioned
│   ├── nav_bus.json            big-endian, CRC-32, per-message endianness override
│   ├── generated/              committed generator output (checked by golden tests)
│   └── demo.cpp                encode/decode demo, pcap writer
├── tests/                      dependency-free test harness and suites
└── .github/workflows/ci.yml    GCC, Clang, ASan/UBSan
```

## Roadmap

- [x] JSON schema with enums, arrays, fixed text, endianness, versions
- [x] Header-only C++17 codec with bounds checks and CRC-16/32 framing
- [x] Wireshark Lua dissector with CRC verification
- [x] Golden-file and round-trip tests, sanitizer CI
- [ ] Variable-length arrays with a length prefix
- [ ] Bitfields and scaled integer fields (engineering units)
- [ ] Table-driven CRC option
- [ ] Python and Rust code generators
- [ ] Markdown protocol documentation generator

## License

MIT, see [LICENSE](LICENSE).

Bekirhan Karakurt - [github.com/bekirhankarakurt](https://github.com/bekirhankarakurt)
