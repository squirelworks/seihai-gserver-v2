# Starting a Graal 3D server: GServer-v2 plus the 3D module

<!-- Seihai project, 2026-11-27: see CHANGES-SEIHAI.md. -->

This file is part of the Seihai fork of GServer-v2 (GPL-3.0, see `LICENSE.md` and
`CHANGES-SEIHAI.md`).

This guide takes you from two downloads to a 5.12 client standing in a 3D world. It
needs nothing but the two releases below, a 5.12 client and a listserver the client
can see. Every option named here is read by the server as written; the full
reference is `bin/docs/graal3d.md` (shipped as `docs/graal3d.md` in the server
archive and as `graal3d-reference.md` in the module archive).

## What you download

From `v4.0.0-seihai.6` the server and its 3D side are two separate releases,
updated at their own times. A 3D fix ships as a new module; you swap one file and
restart.

| Download | Release | What it is |
|---|---|---|
| The server | `v4.0.0-seihai.6` (later: any `v*-seihai.N`) | `gs2emu` with no 3D code, the four lab worlds, the docs, and an `addons/` folder for the module |
| The 3D module | `graal3d-v0.1.1` (later: any `graal3d-v*`) | the module, its README, the 3D reference and this guide |

Both are at <https://github.com/squirelworks/seihai-gserver-v2/releases>. Take the
server and the module for the same system:

| System | Server archive | Module archive | Module file |
|---|---|---|---|
| Linux x86_64 | `seihai-gserver-v2-linux-x86_64.tar.gz` | `seihai-graal3d-0.1.1-linux-x86_64.tar.gz` | `libseihai-graal3d.so` |
| Windows x86_64 | `seihai-gserver-v2-windows-x86_64.zip` | `seihai-graal3d-0.1.1-windows-x86_64.zip` | `seihai-graal3d.dll` |
| macOS Intel | `seihai-gserver-v2-macos-x86_64.tar.gz` | `seihai-graal3d-0.1.1-macos-x86_64.tar.gz` | `libseihai-graal3d.dylib` |
| macOS Apple Silicon | `seihai-gserver-v2-macos-aarch64.tar.gz` | `seihai-graal3d-0.1.1-macos-aarch64.tar.gz` | `libseihai-graal3d.dylib` |

The Linux module has run with a real 5.12; the Windows and macOS modules are built
the same way but have not been run with a client yet. The examples below use Linux;
on Windows, unpack the `.zip` files and use `gs2emu.exe`.

**Linux:** x86_64 with glibc 2.38 or newer (Ubuntu 24.04, Debian 13 or later), with
`libz.so.1` and `libbz2.so.1.0` installed (`zlib1g` and `libbz2-1.0` on Debian and
Ubuntu). Both are built on Ubuntu 24.04 with GCC 14.
**Windows:** 64-bit Windows 10 or later. The server archive carries Microsoft's C++
runtime DLLs (`msvcp140*.dll`, `vcruntime140*.dll`) beside `gs2emu.exe`; leave them
there, or install the Visual C++ 2015-2022 x64 redistributable.
**macOS:** the builds are not signed, so clear the download flag once in each
unpacked folder: `xattr -dr com.apple.quarantine .`

**The module is optional.** The server needs no module to run: with no module in
`addons/`, or with `graal3d = false` (the default in every shipped world), it starts
and runs as a 2D server and looks for no module at all. Only `graal3d = true` makes it
look for one; if none is found it logs one line saying so and runs on in 2D. A server
release never needs a new module, and a module release never needs a new server, while
both use the same add-on API (1 today).

## 1. Download and check

Both releases name their checksum file `SHA256SUMS`, so keep each release's two
files in its own folder:

```sh
mkdir -p g3d/server g3d/module
# g3d/server: seihai-gserver-v2-linux-x86_64.tar.gz and its SHA256SUMS
# g3d/module: seihai-graal3d-0.1.1-linux-x86_64.tar.gz and its SHA256SUMS
(cd g3d/server && sha256sum -c --ignore-missing SHA256SUMS)
(cd g3d/module && sha256sum -c --ignore-missing SHA256SUMS)
```

Each line must say `OK` (on macOS use `shasum -a 256 -c`, on Windows compare
`certutil -hashfile <file> SHA256` with the line in `SHA256SUMS`). The module archive
also has a `SHA256SUMS` inside it for the module file itself.

## 2. Unpack and install the module

```sh
cd g3d
tar xzf server/seihai-gserver-v2-linux-x86_64.tar.gz
tar xzf module/seihai-graal3d-0.1.1-linux-x86_64.tar.gz
cp seihai-graal3d-0.1.1-linux-x86_64/libseihai-graal3d.so seihai-gserver-v2-linux-x86_64/addons/
```

On Windows, copy `seihai-graal3d.dll` into the server folder's `addons\`; on macOS,
`libseihai-graal3d.dylib`.

The server looks for the module, in this order: the server folder, its `addons/`,
the folder `gs2emu` is in, and that folder's `addons/`. The last one, used above,
serves every world in `servers/`. To load a file from anywhere else, set
`graal3dmodule = /path/to/libseihai-graal3d.so`.

## 3. Pick the world folder

5.12 logs in with the token `G3D14097`, which only the `modern` world admits
(`servers/modern/config/allowedversions.txt`, `modern = G3D14097:GNP1905C`).

| Folder | TCP port | 3D UDP port | 3D clients it admits |
|---|---|---|---|
| `servers/modern` | 14805 | 14806 | 4.04r2, 4.20r6, 4.20r8, 5.00r7, 5.12 |
| `servers/default` | 14802 | 14803 | 3.151, 3.153 |

The rest of this guide uses `servers/modern`. The UDP port is always the TCP port
+ 1: the client sends its 3D traffic there whatever the server says.

## 4. Turn 3D on

In `servers/modern/config/serveroptions.txt`, change `graal3d = false` to:

```
graal3d = true
```

That is the only line you must change. The 3D options, all optional:

| Option | Default | What it does |
|---|---|---|
| `graal3dmodule` | `auto` | `auto` searches the folders in step 2; a path loads that file; `off` never loads one |
| `graal3dport` | `0` | 0 means TCP port + 1. Set it only if something in between maps that port elsewhere |
| `graal3dscope` | `off` | `on` or `auto`: send each player only the world objects near it (see "Limits") |
| `graal3dscopeauto` | `2000` | with `auto`: scope only worlds with more objects than this |
| `graal3dscoperadius` | `1100` | world units round the player's own object |
| `graal3dscopehysteresis` | `200` | an object goes once it is farther than radius + this |
| `graal3dsendms` | `50` | least milliseconds between two data packets to a client (10 to 1000) |
| `graal3dpushterrain` | `true` | send `heights.info` and its textures unasked at login, so a first visit is textured |
| `graal3dpushgrass` | `true` | send the grass textures named by `replicator` lines at login and link |
| `graal3dshapecheck` | `skip` | at start, check every shape and texture; `skip` leaves out files known to crash a client, `warn` logs and sends them, `off` does not check |
| `graal3dstringprefix` | `true` | Torque string-prefix flag in game packets; leave it on for 4.x and 5.x |
| `graal3droute` | `mission` | `sharp3d` is a comparison route for 5.00r7 and 5.12; leave it on `mission` |
| `graal3dtrace` | empty | a file name: one line per 3D link, event and ghost, for debugging |

Lab-only options (`graal3dmutate`, `graal3dterrainall`, `graal3dgrassdelay`) are for
client testing; leave them unset on a server people play on.

## 5. Write the 3D world

The 3D clients are sent what `servers/modern/config/graal3dworld.txt` lists. The
release has no 3D content, so you bring your own shapes and textures. Put them in
`servers/modern/world/3d/`: the modern world's `config/foldersconfig.txt` already
has `file 3d/*`. A folder covers only its own files, so each streamed terrain
folder needs its own line, such as `file 3d/island_terrain/*`.

A small world:

```
# config/graal3dworld.txt
# the Sun: direction, colour, ambient. Every 3D world needs one (see "Limits").
sun 0.57 0.57 -0.57  0.8 0.8 0.8 1  0.3 0.3 0.35 1

# each player's own object, and where the first player starts.
# Keep the spawn out of the origin cell if the world has grass (see "Limits").
playershape player.dts
spawn 12 12 1

# the Camera, from the spawn point: behind and above, tilted down a little
camera 0 -5 2 1 0 0 0.38

# object <name> <shape> <x> <y> <z> [<axis x y z> <angle> [<scale x y z>]]
object Ground ground.dts 0 0 0
object Hall hall.dif 20 5 0  0 0 1 1.5708
object BigTree tree.3ds 8 -4 0  0 0 1 0  2 2 3

# optional: a sky box (front, right, back, left, top, bottom), distance, fog colour
sky sky_fr.png,sky_rt.png,sky_bk.png,sky_lf.png,sky_up.png,sky_dn.png 500 0.5 0.6 0.8

# optional: streamed terrain and water
# terrain world/3d/island_terrain
# water -1024 -1024 -19.5 2048 2048 20 water_surface 0

# optional: grass round the player
# replicator DynamicGrass 0 0 0 8 30 50 grass.png
```

What 5.12 draws:

- **Shapes:** `.dts` version 24, `.dif` version 44, and `.3ds` with at least one
  material (a `.3ds` with no material loads but is not drawn).
- **Textures:** 8-bit PNG, JPEG or GIF, at most 1024 a side. BMP, TGA, DDS and
  16-bit PNG draw white. Use lower-case file names.
- **Names:** write files by their real names, with extensions. The server answers the
  client's bare-name requests (`sky_fr`) with the file it has (`sky_fr.png`).
- **Terrain:** a folder of `heights<x>-<y>.dat` chunks (x and y 0 to 15 for the
  plain 256 x 256-square terrain) and a `heights.info` with one 256 x 256 24-bit
  texture name per line, with `.png`. Only one `heights.info` may be in the file
  folders. One `terrain` line per world.

At start the server checks every file the world names and logs any that are missing
or unsafe. Read those lines before you connect a client.

## 6. Start the server

```sh
cd seihai-gserver-v2-linux-x86_64
./gs2emu --server modern            # Windows: gs2emu.exe --server modern
```

Look for these two lines near the top of the log (the toolchain part reads
`msvcstl/windows/x86_64` on Windows and `libc++1/macos/...` on macOS):

```text
Add-on graal3d: module libseihai-graal3d.so 0.1.1 (add-on API 1, libstdc++-cxx11/linux/x86_64; Torque UDP protocols 19, 21 and 24 (3.151 to 5.x); seihai3d: none; built from <commit>).
  Graal3D: listening on UDP port 14806.
```

If the first line is missing or says `** [Error]`, see "Troubleshooting". Stop the
server with Ctrl+C.

## 7. Let a 5.12 client in

A 5.12 goes into 3D mode for a server whose name on its server list starts with
`3 `, and then opens two connections to the listed address: TCP on the listed port
for the normal login, and UDP on that port + 1.

1. **List it as a 3D server.** With the module loaded, the server reports HQ level 4
   to the listserver by itself. A listserver that shows HQ level 4 as `3 ` in front
   of the name (seihai-listserver does, on its gen 4 and gen 5 lists) lists it as a
   3D server. Point the server at it with `listip` and `listport`.
2. **Register with the listserver.** The lab worlds ship with `localhost = true`,
   which accepts any account and does not dial the listserver. To be listed while
   in that mode, set `localhostlist = true`. For a server other people use, set
   `localhost = false` instead, so the listserver checks every login.
3. **Open it to the client's machine.** The lab worlds listen on 127.0.0.1 only. If
   the client runs on another machine (or in a VM), set `serverinterface`,
   `serverip` and `localip` to `AUTO` (or your addresses), and open **TCP 14805 and
   UDP 14806** in the firewall. `localhost = true` on an open interface lets anyone
   log in as anyone: use it only on a private network.
4. **Connect.** Start the 5.12, log in, and pick the `3 ` server from the list.

When the client links, the log says:

```text
Graal3D: <account> linked from <address> (protocol 24, 5.x ...).
Graal3D: <account> is a 5.12 client: ...
```

then the mission start, the control object and, with a terrain, its chunks. On
Windows without a GPU driver, the client has only Microsoft's GDI OpenGL 1.1, which
draws some 3D builds badly; install a driver (or, under Wine, use Mesa).

## Limits

| Limit | What happens | What to do |
|---|---|---|
| **4,096 ghosts a client** | A 4.20r6, 4.20r8 or 5.x client holds at most 4,096 objects. The server sends the player's own object and Camera first, so a big world no longer leaves the view black, but objects past the table are not drawn. | Over about 3,500 objects, set `graal3dscope = auto` (or `on`). A scoped client gets at most 3,584 world objects, nearest first. A town at that cap ran at 11 to 12 FPS on software rendering. |
| **Terrain: 256 x 256 chunks** | A plain terrain is 16 x 16 chunks (256 x 256 squares, 8 units a square by default, centred on the origin). `terrain <folder> <size> repeat <w>x<h>` makes a larger one, each side even and 16 to 256 chunks, streamed as a 16 x 16 window round the player. | Keep each side at most 256. Heights must stay within 0 to 16383, or they wrap. Square size 1 to 64. One `terrain` line per world. |
| **Grass: the origin cell** | Ground cover grows on a grid of 8-unit cells. No grass or ground cover is drawn while the player stands in the origin cell, where x and y are both from 0 to 8. A player at (9, 4) or (4, 20) gets grass. | Put `spawn` outside that cell (for example `spawn 12 12 1`), and keep the play area out of it. Cover also needs real ground under it (terrain or solid objects). |
| **Every world needs a Sun** | A 5.12 switching into a world with no terrain, no water and no `sun` freezes on a sky-only frame. | Give every 3D world a `sun` line. |
| **Grass textures** | 5.12 never asks for a grass texture; it must already have it. | Leave `graal3dpushgrass` on; the server sends them at login. |
| **First visit and terrain textures** | Without the push, a first visit draws the terrain white. | Leave `graal3dpushterrain` on. |

## Troubleshooting

| You see | Cause | Fix |
|---|---|---|
| No `Add-on graal3d` line, no UDP port | `graal3d` is still `false`, or you edited another world's `serveroptions.txt` | Set `graal3d = true` in the folder you started (`--server modern`) |
| `graal3d = true, but this server has no graal3d built in and no libseihai-graal3d.so was found` | The module is not in any searched folder | Copy it into `addons/` beside `gs2emu`, or set `graal3dmodule` to its path |
| `cannot load ... (... GLIBC_2.38 not found ...)` or a missing `libz`/`libbz2` | The system is older than the build, or a library is missing | Use Ubuntu 24.04 / Debian 13 or later; install `zlib1g` and `libbz2-1.0` |
| Windows: `cannot load ... (error 126)` or a missing `MSVCP140.dll` | The C++ runtime DLLs are not beside `gs2emu.exe` | Keep the DLLs from the server archive in its folder, or install the VC++ 2015-2022 x64 redistributable |
| Windows: `not loaded: ... msvcstl-mt ...` | A server or module built with its own static C++ runtime (an older or self-built one) | Use the server and module from the releases, or build both with the `x64-windows-static-md` triplet |
| macOS: the module or `gs2emu` is blocked | The download flag on unsigned files | `xattr -dr com.apple.quarantine .` in the server folder |
| `... not loaded: <reason>. Rebuild the module against this server` | The module was built for another add-on API, standard-library ABI or gs2lib client table | Take a module whose release notes list this server's add-on API (both releases here are API 1), or build both from one source tree |
| `graal3d = true, but graal3dmodule = off` | The module is switched off | Remove the `graal3dmodule = off` line |
| The server is not shown as `3 ` on the list | Not registered, or the listserver does not tag HQ level 4 | `localhostlist = true` (in localhost mode), check `listip` and `listport`, and use a listserver that shows HQ 4 as `3 ` |
| The 5.12 logs in but stays on "Connecting..." or drops after about 30 s | The UDP port is blocked, or the 3D request came from an address with no TCP login | Open UDP TCP-port + 1; make sure TCP and UDP reach the server from the same address |
| `no TCP client matched` in the log | The 3D connect came before the TCP login, or from another address | It retries by itself; behind NAT, both connections must come from one address |
| The view is black | No Camera reached the client, or an older server filled the ghost table | Check the start-up log for world errors; for big worlds use `graal3dscope` |
| Objects or the terrain are white | Wrong texture format or size, or a `.3ds` with no material | 8-bit PNG/JPEG/GIF up to 1024 a side; terrain textures 256 x 256 24-bit with `.png` in `heights.info` |
| An object is missing and the log says it was left out | `graal3dshapecheck = skip` refused a shape that would crash the client | Fix the file (`.dts` 24, `.dif` 44, `.3ds` with materials) |
| No grass | The player is in the origin cell, there is no ground under the cell, or the line's count is too low | Move the spawn, add terrain, raise the per-cell count or radius |
| 5.12 freezes when entering a world | That world has no terrain, water or Sun | Add a `sun` line |
| Something else | | Set `graal3dtrace = g3dtrace.txt`, reproduce, and read the trace |

## Updating

- **A new module:** stop the server, replace `addons/libseihai-graal3d.so` with the
  one from the new `graal3d-v*` release, start it again. The log line names the new
  version. The module's README lists which server builds (add-on API) it loads into.
- **A new server:** unpack it beside the old one, copy your `servers/<name>/`
  folders and the module's `addons/` file across. A module keeps working with a
  newer server of the same add-on API.
- **Building both yourself:** `docs/addons.md`, "Add-on modules".
