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

Tests build: `cmake --build build --config Release` (built if `-DBUILD_TESTS=ON` during configure — OFF by default).

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

- **Public headers** live under `{Module}/{module}/*.hpp` — implementations and private headers in `{Module}/src/*`
- **Namespaces**: `common::`, `flasher::`, `logger::`, `j2534::`
- **Config data**: `Common/common/data.yaml` (~5k lines) — ECU parameters per platform, loaded at runtime
- **Crypto keys**: `keys.cpp` at repo root
- **Tech specs** (9 docs in `docs/tech_specs/`): `transport_abstraction.md`, `d2_protocol_implementation.md`, `d2flasher_hfsm.md`, `d2_flasher_reader_common.md`, `ecu_reading.md`, `ISOTP.md`, `reader_flasher_params.md`, `restore_d2_transferdata_batching.md`, `code_logging_extension.md`
- **Transport abstraction spec**: `docs/tech_specs/transport_abstraction.md`
- **New transport types** (`Common/common/`): `CanFrame.hpp` — CAN message struct, `ICanChannel.hpp` — abstract channel interface, `J2534ChannelAdapter.hpp` — J2534 bridge
- **Channel safety**: J2534 channels opened in one thread crash when used from another — use `J2534ChannelProvider` (see README note)
- **Transport abstraction**: `*ProtocolCommonSteps`, flashers, and loggers use `ICanChannel` (send/receive `CanFrame`). J2534 is one adapter (`J2534ChannelAdapter`). Alternative transports (ELM327, ESP32, STM32) just implement `ICanChannel`.
- **Flasher hierarchy**: `FlasherBase` → `D2FlasherBase`/`UDSFlasher`/`KWPFlasher`. D2 flasher uses an HFSM (`Common/common/hfsm2/machine.hpp`, 17K+ lines, v2.6.0) in `D2FlasherImpl` for state orchestration.
- **Reader hierarchy**: `ReaderBase` → `D2ReaderTF80`/`D2ReaderME7`/`D2ReaderDEM`/`D2ReaderAW55`/`D2ReaderChecksum`/`UDSReader`. Created via `ReaderFactory`.
- **Logger architecture**: Dual-threaded — `_loggingThread` reads CAN and pushes `LogRecord` to a deque; `_callbackThread` pops records and dispatches to all registered `LoggerCallback` instances (`FileLogWriter`, `ConsoleLogWriter`).
- **keys.cpp**: Standalone reference copy of `VolvoGenerateKey()` and `p3_hash()`. The actual application uses duplicated versions in `VolvoFlasher.cpp`.

## CLI

**VolvoFlasher** subcommands: `flash`, `read`, `pin`, `test`, `wakeup`
```
VolvoFlasher [-d device] [-b baudrate] [-f platform] [-e ecu] [-p pin] <subcommand> [subcommand args]
  flash  -i <input.bin> [-s <sbl_path>]
  read   -o <output> -s <start_addr> -sz <size>
  pin    [-d]              # -d = scan downward
  test                     # hardcoded VAG MED91 test
  wakeup                   # wake up CAN network (no args)
```

**VolvoLogger** (no subcommands):
```
VolvoLogger --variables <path> -o <output> [-d device] [-b baudrate] [-f platform] [-e ecu] [-p print_count] [--verbose]
```

## Supported platforms

`P80`, `P1`, `P1_UDS`, `P2`, `P2_250`, `P2_UDS`, `P3`, `SPA`, `Ford_KWP`, `Ford_UDS`, `Haval_UDS`, `VAG`, `VAG_MED91`, `VAG_MED912`

## Tests

Only `Registry/RegistryTest/` exists — a standalone console app with custom `CHECK_THROWS_AS` / `CHECK_NO_THROW` macros. No test framework, no CI test step.

Additional partial test content (not built by default — requires `-DBUILD_TESTS=ON`):
- `Common/test/` — `D2MessageTest.cpp`, `D2RequestTest.cpp`
- `Flasher/test/` — `D2FlasherTest.cpp`, `MockICanChannel.hpp`

## Dependencies (Conan)

`boost/1.86.0`, `yaml-cpp/0.6.3`, `easyloggingpp/9.97.1`

## Misc

- Logging: easyloggingpp, initialized via `common::initLogger("application.log")` at startup
- **Log extension ТЗ**: `docs/tech_specs/code_logging_extension.md` — все шаги реализованы (кроме crash-хендлера, заблокирован conan)
- **LOG_MODULE macro**: `Common/common/LogHelper.hpp` — per-module логирование. Каждый `.cpp` определяет `LOG_MODULE_NAME` перед `<include "common/LogHelper.hpp>`, затем использует `LOG_MODULE(level)` вместо `LOG(level)`
- **VOLVOLOG_DEBUG**: env var, comma-separated module names, включает DEBUG/TRACE для указанных модулей
- **Console in Debug**: `_DEBUG` автоматически включает консоль + все уровни
- **Flow:**
  1. `initLogger("application.log")` — консоль (если `enableConsole`) + ротация 10 MB
  2. VolvoFlasher / VolvoLogger: `-v` или `--verbose` перезапускают initLogger с mode=verbose
- README is in Russian
- `.gitignore` excludes `build/`, `*.vcxproj`, `*.sln`, `CMakeUserPresets.json`
- No linter, formatter, or typecheck config present
