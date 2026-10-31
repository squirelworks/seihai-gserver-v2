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
| A weapon with no joined classes no longer crashes the server when it is registered with a client that gets the GS2 path (5.12 and up on the modern generation). `getJoinedClassesList` dropped the last character of an empty string, and the weapon packet was then written with a bogus length. The stock `-gr_movement` (a GS1 weapon with no bytecode) crashed every such login. | `Weapon.cpp` | 5.12 to 6.x (crash at login) |

## Source for every release

Each release on this repository attaches the full source at its tag. The
dependencies are fetched at the commits pinned in `server/CMakeLists.txt` and
`vcpkg.json`. gs2lib comes from our mirror
[seihai-gs2lib](https://github.com/squirelworks/seihai-gs2lib) at the pinned commit;
its own `CHANGES-SEIHAI.md` lists our changes there.
