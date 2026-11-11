# Building the Seihai fork on Linux, Windows and macOS

This file is part of the Seihai fork (GPL-3.0, see `LICENSE.md` and `CHANGES-SEIHAI.md`).
It explains how `.github/workflows/seihai-release.yml` builds `gs2emu` and what
had to change for current compilers, so the next person can keep the four
targets green.

## Targets

| Target | Runner | Compiler | Standard library | vcpkg triplet |
|---|---|---|---|---|
| Linux x86_64 | ubuntu-24.04 | GCC 14 | libstdc++ (has `<generator>`) | x64-linux |
| Windows x86_64 | windows-latest | MSVC 14.51 (`/std:c++latest`) | MSVC STL (has `<generator>`) | x64-windows-static |
| macOS x86_64 | macos-15-intel | Homebrew LLVM | libc++ (no `std::generator`) | x64-osx |
| macOS arm64 | macos-15-intel (cross-compiled, `CMAKE_OSX_ARCHITECTURES=arm64`) | Homebrew LLVM | libc++ (no `std::generator`) | arm64-osx |

Apple clang is not used: the project needs C++23 features it lacks. Homebrew
LLVM 18 was tried first and rejected the Xcode 16.4 SDK headers, so the
workflow takes the current `brew install llvm`.

## Steps the workflow runs

1. Java 21 (for the antlr generator), then per OS: `gcc-14`/`ninja` on Linux,
   `ninja llvm lld autoconf automake libtool bison flex` on macOS, the MSVC
   developer prompt on Windows.
2. vcpkg is cloned into `$VCPKG_DIR` (not `VCPKG_ROOT`, which the MSVC prompt
   overwrites) and bootstrapped.
3. `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DTESTS=OFF -DUPNP=OFF` with the
   vcpkg toolchain and the triplet from the matrix. On macOS `CC`/`CXX` point at
   Homebrew clang and Homebrew bison/flex go first on `PATH`.
4. `cmake --build build`. On Linux the Seihai regression tests are then built
   with `-DTESTS=ON` and run (`Catch_tests/RC` and `Catch_tests/Gpak`; the
   other test folders are upstream's and are not built).
5. `bin/` plus `LICENSE.md`, `CHANGES-SEIHAI.md` and `README.md` are packed.
   The packed `default` and `classic` lab servers are then started and must
   listen on `127.0.0.1:14802` and `14803`. The cross-built arm64 binary cannot
   run on the Intel runner, so it gets an architecture check instead.
6. A `v*` tag, or a manual run with a version, attaches the Linux and Windows
   archives and a `-source.tar.gz` from `git archive` to a draft release, with
   notes from `docs/releases/v<version>.md`. The owner publishes the draft.
   The macOS builds are a separate best-effort job; their archives are added to
   the draft when they finish. A manual run can skip them with `skip_macos`.

## Compiler issues fixed so far (2026-10-31 and 2026-11-04)

Each one has a row in `CHANGES-SEIHAI.md`.

| Where | Compiler | Problem | Fix |
|---|---|---|---|
| `Level.cpp` | MSVC | narrowing conversion of a baddy index | explicit `uint8_t` cast |
| `PlayerClient.cpp` | MSVC | `uniform_int_distribution<int8_t>` is not allowed | use `int` |
| `PlayerProps.cpp` thrown NPC | MSVC | `int` to `int16_t` narrowing in a braced `LocalPixelPosition` | explicit `int16_t` casts |
| `CommonTypes.h` `toSystemClock` | libc++ | stray `)`, and `file_clock::to_sys` returns nanoseconds in `__int128` | `time_point_cast<clock::duration>` |
| `std/generator.h` | libc++ | libc++ declares `std::ranges::elements_of` but not `std::generator`; the stand-in's own `elements_of` clashed | 2026-11-04: upstream's own copy is used again; since `5a90281` it uses libc++'s `elements_of` when libc++ has one (our 2026-10-31 rename is gone) |
| `StringUtils.h` `toFloat` | libc++ on macOS | float `from_chars` is unavailable before macOS 26 | `strtof` on that platform only |
| `FileSystem.cpp` | libc++ | `std::this_thread::sleep_for` without `<thread>` | include `<thread>` |
| `filesystem/watch/` | macOS | macOS is `PLATFORM_UNIX` too, so it compiled the inotify watcher; the macOS file was empty | Linux file guarded with `!__APPLE__`; macOS gets a no-op watcher |
| `UPNP.cpp` | macOS | `<malloc.h>` does not exist | `<cstdlib>` |
| link of `bin/gs2emu` | macOS | Apple's `ld` rejects the GNU-format archives that `llvm-ar` writes (`archive member '/' not a mach-o file`) | workflow links with Homebrew's `lld` (`-fuse-ld=.../ld64.lld`) |
| `main.cpp` | macOS | `_NSGetExecutablePath` without `<mach-o/dyld.h>`; home path kept the file name | include the header, keep the folder only |
| `Level.cpp` `mapPosition.value_or({0, 0})` (2026-11-04) | GCC 14 | cannot deduce the type from a braced list | `MapPosition{0, 0}` |
| `Level.h` pixel sizes into `PixelPosition` (2026-11-04) | MSVC | `uint32_t` to `int32_t` narrowing | explicit `int32_t` casts |
| `Extents.h` `Rectangle` from two corners (2026-11-04) | MSVC | `int` to `uint16_t` narrowing | cast each side length to the size type |

## When a target goes red

1. Open the failed job's log and search for `error:` (clang/GCC) or `error C`
   (MSVC). Ninja keeps building other files after the first failure, so one log
   can show several unrelated errors; fix all of them in one push.
2. Fix the smallest thing that makes the code valid on every compiler. Prefer
   standard C++ over `#ifdef`; when a platform guard is needed, keep the other
   platforms on the original code.
3. Add the notice line at the top of each changed file, a row to
   `CHANGES-SEIHAI.md` and a row to the table above.
4. Push to the pull request branch and read the next run. A whole job takes
   about 10 minutes on Linux and Windows and 20 to 25 on macOS (most of it is
   vcpkg in the Configure step).

## TODO

- [ ] **macOS file watching.** `FileWatchOS_OSX.cpp` is a no-op, so on macOS files
      changed while the server runs are not reloaded. Implement it with kqueue
      (`EVFILT_VNODE` on each directory, then rescan it) or FSEvents, and keep the
      same event fix-ups as the Linux watcher (temp file renamed over the original
      gives one Modified event).

- [ ] Run the macOS binary once in the lab to confirm the home-path fix
      (`Calculated home path:` should print the `bin` folder).
- [ ] Consider caching vcpkg (`actions/cache` on `$VCPKG_DIR/installed` keyed on
      `vcpkg.json` and the triplet) to cut build time.
- [ ] Move `actions/checkout` and `actions/setup-java` to their Node 24
      majors when they are released as stable (the runner warns about Node 20).
- [ ] Offer the compiler fixes upstream to xtjoeytx/GServer-v2 (ask the owner first).
