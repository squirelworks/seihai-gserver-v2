# Seihai changes to GServer-v2

This is the Seihai fork of [GServer-v2](https://github.com/xtjoeytx/GServer-v2),
branch `beta4` at `365bbd2` (2026-10-26). It is released under the same licence,
the GNU General Public License version 3 (see `LICENSE.md`). The original authors
are credited in `README.md`.

Every change below was checked on a private loopback lab with real Graal
clients and with the [Seihai](https://github.com/squirelworks/seihai) client.
Each file we changed carries a one-line notice at its top.

## 2026-10-31

| Change | Files | Clients it fixes |
|---|---|---|
| The rest of the login frame (the `PLI_LANGUAGE` that 2.05 and 2.17 send with the login) is handed to the new player instead of dropped. Before, the gen-3 iterator ran one step behind for the whole session ("Invalid PlayerProp"). | `IPacketHandler.h`, `PlayerLogin.cpp` | 2.05, 2.17 |
| Login detection uses a flag instead of "packet count is 0". `PLI_PACKETCOUNT`, which 1.33 to 1.39 send every 10 s, reset the count, so the next frame parsed as a login and the player was kicked. | `IPacketHandler.h`, `PlayerLogin.cpp` | 1.33 to 1.39 |
| `PLO_SETACTIVELEVEL` is sent to Original clients from 2.01 up, and `PLO_LEVELNAME` comes before the board on first load. | `PlayerClientOriginal.cpp` | 2.05 (stuck on loading) |
| An account is never loaded hidden: the hidden status bit is cleared on load. | `FlatFileAccountLoader.cpp` | 4.x (invisible after reconnect) |
| `MAXHP 3.00`, as the stock account files write it, is read as 3 instead of 0. | `FlatFileAccountLoader.cpp` | all (new accounts had no hearts) |
| Id 25 weapon uploads are accepted and ignored instead of kicking the player. | `PlayerClient.h`, `PlayerClient.cpp`, `PlayerClientPackets.cpp` | 1.33 to 1.37 |
| On a classic server prop 10 is sent in the bow form. The gani string was read as the bow and the rest of the props were lost. | `PropertySerializers.cpp` | 1.33 to 1.41 |
| gs2lib comes from our mirror [seihai-gs2lib](https://github.com/squirelworks/seihai-gs2lib) (branch `seihai`): the 3.153 token, and no abort on unknown tokens. | `server/CMakeLists.txt` | 3.04, 3.153 |
| The default `allowedversions.txt` lets in 2.00 to 2.31 and 3.04 to 3.153 on the newmain generation (stock: 2.22 to 2.31). | `bin/servers/default/config/allowedversions.txt` | 2.00 to 2.17, 3.x |
| `-DANTLR4_JAR_LOCATION=<jar>` uses a local antlr jar that matches a system antlr4 runtime. | `server/CMakeLists.txt` | build only |
| A baddy index is cast to `uint8_t` explicitly; MSVC refused the narrowing conversion. | `Level.cpp` | build only (Windows) |
| The death-drop displacement uses `uniform_int_distribution<int>` instead of `<int8_t>`; MSVC rejects 8-bit types there, as the standard allows it to. | `PlayerClient.cpp` | build only (Windows) |
| `toSystemClock` casts the `file_clock::to_sys` result to the system clock's duration and loses a stray `)`. Only the libc++ path (no `clock_cast`) used it. | `CommonTypes.h` | build only (macOS) |
| The bundled `std::generator` stand-in names its helper `__compat_elements_of`; libc++ declares its own `std::ranges::elements_of` and the two clashed. | `std/generator.h` | build only (macOS) |
| `toFloat` uses `strtof` with libc++ on macOS, where the floating-point `from_chars` is marked unavailable before macOS 26. | `StringUtils.h` | build only (macOS) |
| The thrown-NPC move delta is built from two `int16_t` casts; `-moveDistance` is an `int` and MSVC refused the narrowing inside braces. | `PlayerProps.cpp` | build only (Windows) |
| `FileSystem.cpp` includes `<thread>` for `std::this_thread::sleep_for`; libc++ does not pull it in through other headers. | `FileSystem.cpp` | build only (macOS) |
| macOS gets its own file watcher, a no-op for now: it built the Linux inotify watcher, which does not exist there. Files changed on disk while the server runs are not reloaded on macOS until it restarts. | `FileWatchOS_OSX.cpp`, `FileWatchOS_Linux.cpp` | build only (macOS); known gap |
| `main.cpp` includes `<mach-o/dyld.h>` for `_NSGetExecutablePath`, and on macOS the home path is the executable's folder (as on Windows and Linux) instead of the whole buffer, which held the file name and trailing bytes. | `main.cpp` | build and start-up (macOS) |
| `docs/SEIHAI-BUILD.md`: how the four targets are built, the compiler fixes so far, and what to do when one goes red. | `docs/` | docs only |
| Release workflow for Linux, Windows and macOS. | `.github/workflows/` | build only |

## Source for every release

Each release on this repository attaches the full source at its tag. The
dependencies are fetched at the commits pinned in `server/CMakeLists.txt` and
`vcpkg.json`. gs2lib comes from our mirror
[seihai-gs2lib](https://github.com/squirelworks/seihai-gs2lib) at the pinned commit;
its own `CHANGES-SEIHAI.md` lists our changes there.
