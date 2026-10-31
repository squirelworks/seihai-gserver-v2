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
| Release workflow for Linux, Windows and macOS. | `.github/workflows/` | build only |
| Lab world: both server folders listen on `127.0.0.1` only, hand out `127.0.0.1`, have UPnP off, accept logins without listserver verification (`localhost = true`), and look for a listserver on `127.0.0.1` instead of a public one. `url` is empty, and the welcome page no longer loads a logo from a public site. | `bin/servers/default/config/serveroptions.txt`, `servermessage.html` | lab setup |
| A second server folder, `bin/servers/classic`: generation classic on port 14803, `flaghack_movement = false`, no `-gr_movement` weapon, 1.33 to 1.41r1 in `allowedversions.txt`, and accounts with `.gif` head, sword and shield. The account files have no comment syntax, so they carry no notice line. | `bin/servers/classic/` | 1.33 to 1.39 |
| Start scripts for the lab world: `start.sh` (Linux, macOS), `start.command` (macOS Finder), `start.bat` (Windows); their install rules, line endings and ignore rules; a CI step that starts the packaged world with them on every OS; `bin/docs/lab-world.md`. | `bin/start.*`, `bin/CMakeLists.txt`, `bin/docs/lab-world.md`, `.gitattributes`, `.gitignore`, `.github/workflows/`, `README.md` | lab setup |

## Source for every release

Each release on this repository attaches the full source at its tag. The
dependencies are fetched at the commits pinned in `server/CMakeLists.txt` and
`vcpkg.json`. gs2lib comes from our mirror
[seihai-gs2lib](https://github.com/squirelworks/seihai-gs2lib) at the pinned commit;
its own `CHANGES-SEIHAI.md` lists our changes there.
