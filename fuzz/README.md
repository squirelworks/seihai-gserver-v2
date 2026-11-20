<!-- Modified by the Seihai project, 2026-11-16, 2026-11-19, 2026-11-20: see CHANGES-SEIHAI.md. -->
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
| `replay_handlers` | The same handlers, fed recorded sessions from files; any compiler, no libFuzzer |

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
fuzzes those handlers with bodies. The `sanitizers` job also runs a scripted
loopback session with the probe client, and `replay_handlers` (below). Still
open: an NPC-server and a gmap in the fuzz world. Known noise: a few
`stack-use-after-return` reports from `-fork` runs didn't reproduce on their
own and haven't been explained yet.

## replay_handlers

Replays recorded sessions through the same handlers as `fuzz_handlers`, on the
same socketless `Server` and level (`fuzz/handlers/harness.h`), with any
compiler. Build it from the main project with `-DREPLAY_HANDLERS=ON`; add
`-DSANITIZE=ON` to replay under ASan/UBSan, as the `sanitizers` job does:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTESTS=OFF -DUPNP=OFF -DSANITIZE=ON -DREPLAY_HANDLERS=ON -DANTLR4_JAR_LOCATION=antlr4-4.10.1-complete.jar
cmake --build build --target replay_handlers
python3 tools/lab/netdump2session.py tools/lab/fixtures/modern-3clients /tmp/sessions
mkdir -p /tmp/replay && cd /tmp/replay
ASAN_OPTIONS=detect_leaks=0 "$OLDPWD/bin/replay_handlers" /tmp/sessions/*.session \
  "$OLDPWD"/fuzz/corpus/sessions/*.session "$OLDPWD"/fuzz/corpus/handlers/*
```

Each argument is a file. A `.session` file is a text session; anything else is
a `fuzz_handlers` input, run once. `--seeds DIR` also writes each session
player's packets to `DIR` as a `fuzz_handlers` input (bodies cut at 255 bytes),
which the weekly `fuzz` workflow does before it fuzzes.

A session file:

```text
# a comment
login <name> <type byte, hex> <version token>
<name> <packet id> [<body byte, hex> ...]
```

The type byte is the one the client sends first (`20` client, `21` RC, `23` NC,
`24` and `25` the newer clients, `26` the newer RC); the token is its version
string (`GNW24011`, `G3D0311C`, `GSERV025`, `NCL21075`). Clients stand in the
fuzz level; RCs and NCs get every admin right. Every player stays until the end
of the file, so players of one session meet. A body is what follows the packet
id, Graal-encoded as sent.

`tools/lab/netdump2session.py SERVERDIR OUTDIR` writes one from a
`PACKET_LOGGING` server's `logs/networkdump.txt` and `logs/serverlog.txt`:
every client-to-server packet in log order, players renamed `p1`, `p2` and so
on, and no account names or passwords. Use it on our own loopback lab runs
only. `fuzz/corpus/sessions/` holds hand-written sessions (a 2.0 client, a
6.03r7 client, an RC and an NC together).
