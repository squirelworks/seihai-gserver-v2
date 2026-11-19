<!-- Modified by the Seihai project, 2026-11-16, 2026-11-19: see CHANGES-SEIHAI.md. -->
# Fuzz targets for seihai-gserver-v2 (#87)

Standalone Clang/libFuzzer project. It links `seihai-gs2lib` and the
header-only `IPacketHandler::processBuffer` path; it does **not** build
`gs2emu` or pull vcpkg/antlr.

## Build

```sh
CC=clang-18 CXX=clang++-18 cmake -S fuzz -B build-fuzz -G Ninja
cmake --build build-fuzz -j
```

## Run

```sh
./build-fuzz/fuzz_process_buffer -max_total_time=60 fuzz/corpus/process_buffer
```

## Targets

| Target | Entry point |
| --- | --- |
| `fuzz_process_buffer` | `IPacketHandler::processBuffer` (framing + decrypt + decompress + packet split) with a stub `handlePacket` |

Also for #87: `-DSANITIZE=ON` and the `sanitizers` workflow run the Catch
regression tests under ASan/UBSan, and `Catch_tests/ShortPackets` sends every
client id with an empty body through the real handlers. Still open: a scripted
loopback session, a replay harness fed by recorded lab sessions, and a fuzz
target over the real handlers seeded from it.
