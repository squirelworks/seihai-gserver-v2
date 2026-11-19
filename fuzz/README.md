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
| `fuzz_handlers` | `PlayerClient::handlePacket` on a `Server` with no socket: the real client packet handlers, for 1.39, 2.0, 2.22, 4.04r2, 5.12 and 6.03r7 clients standing in a small level, plus `PlayerRC` and `PlayerNC` |

## fuzz_handlers

Built from the main project, not from this folder, because it links the whole
server library. Clang only:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build-fz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTESTS=OFF -DUPNP=OFF -DSANITIZE=ON -DFUZZ_HANDLERS=ON -DANTLR4_JAR_LOCATION=antlr4-4.10.1-complete.jar
cmake --build build-fz --target fuzz_handlers
mkdir -p /tmp/corpus /tmp/fz-run && cp fuzz/corpus/handlers/* /tmp/corpus/
(cd /tmp/fz-run && ASAN_OPTIONS=detect_leaks=0 "$OLDPWD/bin/fuzz_handlers" -max_total_time=600 -max_len=4096 -timeout=10 /tmp/corpus)
```

Run it from an empty scratch folder, as above. It writes its level to
`fuzz-world/` in the working directory, and RC handlers write files under
`servers/fuzz/` there.

Input: one byte picks the client generation and version, or an RC or NC with
every admin right (no folder rights), then records of
`[id] [body length] [body]`. `FUZZ_HANDLERS` adds `-fsanitize=fuzzer-no-link`
to every target so the fuzzer follows the server's own branches. The weekly
`fuzz` workflow runs it for 20 minutes.

Also for #87: `-DSANITIZE=ON` and the `sanitizers` workflow run the Catch
regression tests under ASan/UBSan, and `Catch_tests/ShortPackets` sends every
client id with an empty body through the real handlers, and `fuzz_handlers`
fuzzes those handlers with bodies. Still open: a scripted loopback session and a
replay harness fed by recorded lab sessions, to seed `fuzz_handlers` further;
an NPC-server and a gmap in its world. Known noise: a few
`stack-use-after-return` reports from `-fork` runs didn't reproduce on their
own and haven't been explained yet.
