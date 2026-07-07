# AGENTS.md — VolvoTools

C++20, Windows-only CMake project for flashing/logging Volvo (and other) ECUs via J2534 devices.

## Build (exact order)

```powershell
git submodule update --init
conan install . --build=missing
cmake --preset conan-default
cmake --build build --config Release
```

CI (`windows-2022`) also builds x86 separately: `cmake --preset conan-default -A Win32`.

`prepare_build.bat` is a shortcut for the first three steps using two conan profiles.

## Submodules (5)

Must be init'd after clone. All are read-only dependencies:
- `Registry/` — Windows Registry C++ wrapper
- `j2534/` — J2534 API wrapper
- `argparse/` — argparse C++ header library
- `libintelhex/` — Intel HEX parser
- `fast-cpp-csv-parser/` — CSV parser

## Package map

| Package | Type | Path | Links to |
|---|---|---|---|
| Common | static lib | `Common/` | yaml-cpp, Boost, j2534, Registry, intelhex, easyloggingpp |
| j2534 | static lib | `j2534/` | (standalone) |
| Registry | static lib | `Registry/` | (standalone, Windows-only) |
| Flasher | static lib | `Flasher/` | Common, j2534 |
| Logger | static lib | `Logger/` | Common, j2534 |
| argparse | header-only interface | `argparse/` | (standalone) |
| intelhex | static lib | `libintelhex/` | (standalone) |
| **VolvoFlasher** | exe | `VolvoFlasher/src/VolvoFlasher.cpp` | Common, j2534, Flasher, argparse |
| **VolvoLogger** | exe | `VolvoLogger/src/VolvoLogger.cpp` | Logger, Common, j2534, argparse |

## Architecture

- **Public headers** live under e.g. `Flasher/flasher/*.hpp` — implementations in `Flasher/src/*`
- **Namespaces**: `common::`, `flasher::`, `logger::`, `j2534::`
- **Config data**: `Common/common/data.yaml` (~5k lines) — ECU parameters per platform, loaded at runtime
- **Crypto keys**: `keys.cpp` at repo root
- **Transport abstraction spec**: `docs/tech_specs/transport_abstraction.md`
- **New transport types** (`Common/common/`): `CanFrame.hpp` — CAN message struct, `ICanChannel.hpp` — abstract channel interface, `J2534ChannelAdapter.hpp` — J2534 bridge
- **Channel safety**: J2534 channels opened in one thread crash when used from another — use `J2534ChannelProvider` (see README note)
- **Transport abstraction**: `*ProtocolCommonSteps`, flashers, and loggers use `ICanChannel` (send/receive `CanFrame`). J2534 is one adapter (`J2534ChannelAdapter`). Alternative transports (ELM327, ESP32, STM32) just implement `ICanChannel`.

## CLI

**VolvoFlasher** subcommands: `flash`, `read`, `pin`, `test`, `wakeup`
```
VolvoFlasher [-d device] [-b baudrate] [-f platform] [-e ecu] [-p pin] <subcommand> [subcommand args]
  flash  -i <input.bin> [-s <sbl_path>]
  read   -o <output> -s <start_addr> -sz <size>
  pin    [-d]              # -d = scan downward
```

**VolvoLogger** (no subcommands):
```
VolvoLogger -v <variables> -o <output> [-d device] [-b baudrate] [-f platform] [-e ecu] [-p print_count]
```

## Supported platforms

`P80`, `P1`, `P1_UDS`, `P2`, `P2_250`, `P2_UDS`, `P3`, `SPA`, `Ford_KWP`, `Ford_UDS`, `Haval_UDS`, `VAG`, `VAG_MED91`, `VAG_MED912`

## Tests

Only `Registry/RegistryTest/` exists — a standalone console app with custom `CHECK_THROWS_AS` / `CHECK_NO_THROW` macros. No test framework, no CI test step. No tests for Common, Flasher, Logger, or the executables.

## Dependencies (Conan)

`boost/1.86.0`, `yaml-cpp/0.6.3`, `easyloggingpp/9.97.1`

## Misc

- Logging: easyloggingpp, initialized via `common::initLogger("application.log")` at startup
- README is in Russian
- `.gitignore` excludes `build/`, `*.vcxproj`, `*.sln`, `CMakeUserPresets.json`
- No linter, formatter, or typecheck config present
