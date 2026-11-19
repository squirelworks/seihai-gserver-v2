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
| `fuzz_handlers` (in `fuzz/handlers`, built from the top level) | `handlePacket` of the real `PlayerClient`, `PlayerRC` and `PlayerNC` |

Also for #87: `-DSANITIZE=ON` and the `sanitizers` workflow run the Catch
regression tests under ASan/UBSan, and `Catch_tests/ShortPackets` sends every
client id with an empty body through the real handlers.

## The real handlers: `fuzz/handlers`

`fuzz_handlers` hands already-split packets to `PlayerClient`, `PlayerRC` and
`PlayerNC` as the socket loop does, with a game client that is logged in and
standing in a small level. It is built from the top-level project, because it
needs the whole server:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build-hf -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTESTS=ON -DUPNP=OFF -DSANITIZE=ON -DFUZZ_HANDLERS=ON -DANTLR4_JAR_LOCATION=<antlr 4.10.1 jar>
cmake --build build-hf --target fuzz_handlers
```

Run it from an **empty scratch folder**, not from `bin/`: it makes its world
(`servers/fuzz/`, `fuzz-world/`) in the working directory, and RC handlers
write files there.

```sh
mkdir -p /tmp/hf/run /tmp/hf/corpus && cd /tmp/hf/run
ASAN_OPTIONS=detect_leaks=0 /path/to/bin/fuzz_handlers -fork=2 -ignore_crashes=1 \
  -max_total_time=3600 -artifact_prefix=/tmp/hf/ /tmp/hf/corpus
```

Input: byte 0 picks the player (bits 0-1: game client, RC or NC; bits 2-7: the
client version), then `[id] [length] [body]` packets, up to 64.

Known noise, not bugs:

- With `CMAKE_BUILD_TYPE=Debug`, an RC Add Account stops on the
  `account.level` assert in `forceSaveAccount`, because the scratch world has
  no `(defaultaccount)`.
- A few `stack-use-after-return` reports from fork mode didn't reproduce when
  their input was run on its own. They may depend on state left by earlier
  inputs, and haven't been explained yet.

Still open: no NPC-server and no gmap in the scratch world yet, and no replay
corpus from recorded lab sessions.
