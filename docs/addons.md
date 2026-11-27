# Server add-ons (Seihai)

<!-- Seihai project, 2026-11-25, 2026-11-27: see CHANGES-SEIHAI.md. -->

This file is part of the Seihai fork (GPL-3.0, see `LICENSE.md` and `CHANGES-SEIHAI.md`).

An add-on is an optional subsystem that sits beside the gserver instead of inside
it. It is compiled in with its own CMake option, switched on by its own server
option, and reached by the core only through a small set of hooks. With no add-on
compiled in, or none switched on, every hook is a no-op and the server behaves as
upstream GServer-v2 does.

The Graal 3D side (the Torque UDP server for the 3.151 to 5.x clients, see
`bin/docs/graal3d.md`) is the first add-on. It is built by default in this fork
(`-DGRAAL3D=ON`) and runs only with `graal3d = true` in `serveroptions.txt`.
Build with `-DGRAAL3D=OFF` for a server with no 3D code at all. Since 2026-11-27
it can also be built on its own, as the module `seihai-graal3d`, and loaded by a
server built without it: see "Add-on modules" below.

## The hooks

`server/include/addons/ServerAddon.h` is the whole surface. The core calls it
through `Server::getAddons()` (`server/include/addons/ServerAddons.h`).

| Hook | Called from | Default | What the Graal 3D add-on does |
|---|---|---|---|
| `start(interfaceIp)` | `Server::init`, after the game port listens | (required) | Reads `config/graal3dworld.txt`, opens the UDP port (`graal3dport`, else the game port + 1) |
| `update()` | `Server::doMain`, each frame | nothing | Reads datagrams and runs every 3D link |
| `onPlayerLeft(id)` | `Server::deletePlayer` | nothing | Closes that player's 3D link |
| `onPacketToPlayer(id, packet)` | `Player::sendPacket`, only for a player with `setAddonLinked(true)` | nothing | Sends flag packets on the 3D link too (a client in 3D mode ignores them on TCP) |
| `listHqLevel()` | `ServerList::serverHQLevel` | `hq_level` from `adminconfig.txt` | 4 (Graal3D), which the listserver tags `3 ` |
| `clientNeeds(versionId)` | `Player::addonNeeds()` | the core's behaviour | For 3.151 to 5.x clients, see below |

`clientNeeds` returns an `AddonClientNeeds`:

| Field | Used in | Effect |
|---|---|---|
| `answerMissingChecksumFiles` | `PLI_UPDATEFILE` handler | A missing file gets `PLO_FILESENDFAILED` instead of no answer (a 3D login waits on every answer) |
| `alwaysServerListConnected` | login (`PlayerClient`) | `PLO_SERVERLISTCONNECTED` is sent even with no listserver (5.x keeps its "Connecting..." window over the 3D view until it gets it) |
| `bareNameExtensions` | `Player::sendFile` | Extensions tried for a request with no extension (5.x asks for Torque textures that way) |
| `refusedFiles` | `Player::findClientFile` | Files answered as missing (ones the add-on found would crash the client) |
| `loginFiles` | login (`PlayerClient`) | Files sent unasked after the `gpaks` packages (the Graal 3D grass textures, which 5.12 never asks for) |

The other direction, what an add-on may ask of the core, is
`server/include/addons/AddonHost.h`: settings, the log, file lookup, a snapshot
of each player (account, nickname, chat, version, address, level, tile position),
moving a player's TCP position, its chat, a flag or a serverside triggeraction from
it, giving it a weapon or a file, the flags its login sends, and compiling
clientside GS2 through the NPC-server. Only standard types cross it. The Graal 3D
add-on uses nothing else of the core, which is what lets it be a module.

## Adding an add-on

1. Put it in its own folder (`server/include/<name>/`, `server/src/<name>/`).
2. Subclass `preagonal::ServerAddon`, with a static `create(AddonHost&)` that
   returns `nullptr` unless its server option is on. Reach the core only through
   the `AddonHost` you are given, so the add-on can also be a module.
3. In `server/CMakeLists.txt`, add an `option(<NAME> ...)` that appends its sources
   and defines `SEIHAI_ADDON_<NAME>`.
4. List it in `ServerAddons::start` (`server/src/addons/ServerAddons.cpp`) inside
   `#ifdef SEIHAI_ADDON_<NAME>`.
5. If it needs the core to do something the hooks above do not cover, add one hook
   with a default that keeps the core's behaviour (or one `AddonHost` call), bump
   `kAddonApiVersion` in `addons/AddonModule.h`, and document it here.
6. To ship it as a module too, add its name to `kModuleAddons` in
   `ServerAddons.cpp`, a `<NAME>_MODULE` CMake option like `GRAAL3D_MODULE`, and a
   file like `server/src/graal3d/Graal3DModule.cpp` with the three entry points.

## Add-on modules

An add-on can be compiled on its own, as a shared library, and loaded by a
server that was built without it. The Graal 3D add-on is the first: the module
`seihai-graal3d`. This is the same split as the Seihai gserver and its
NPC-server (`seihai-npcserver`): two programs built and released apart, which
meet through a versioned interface, so either can be rebuilt or upgraded without
the other. Here the two are a program and a library in one process, and the
interface is the add-on API (`ServerAddon.h`, `AddonHost.h`, `AddonModule.h`)
rather than a network link.

### Building them apart

```sh
# The server, with no 3D code in it:
cmake -S . -B build-server -DGRAAL3D=OFF
cmake --build build-server --target gs2emu

# The module, from the same tree (the server need not be rebuilt):
cmake -S . -B build-module -DGRAAL3D=OFF -DGRAAL3D_MODULE=ON
cmake --build build-module --target seihai-graal3d
#   -> bin/addons/libseihai-graal3d.so (Linux), seihai-graal3d.dll (Windows),
#      libseihai-graal3d.dylib (macOS)
```

`GRAAL3D=ON` (built in) and `GRAAL3D_MODULE=ON` can be on together; a server
with the add-on built in never loads the module.

The module is compiled from the same sources as the built-in add-on
(`server/src/graal3d/`), plus `Graal3DModule.cpp`, and links its own copy of
gs2lib and zlib. It exports three C functions and nothing else; its own symbols
stay hidden.

### Turning it on

Put the module in the server's folder or its `addons/` folder (or the program's
folder, or its `addons/`), and set `graal3d = true` in `serveroptions.txt`, as for
the built-in add-on. Every `graal3d*` option means the same.

| Option | Default | Meaning |
|---|---|---|
| `graal3dmodule` | `auto` | `auto`: look for the module in the places above. A path: load that file. `off`: never load one |

At start the log says which module was loaded:

```text
Add-on graal3d: module libseihai-graal3d.so 0.1.0 (add-on API 1, gcc/libstdc++/linux/x86_64; Torque UDP protocols 19, 21 and 24 (3.151 to 5.x); seihai3d: none; built from 1a2b3c4d).
  Graal3D: listening on UDP port 14803.
```

### When a module is refused

The hooks are C++ classes, so the server and the module must agree on more than
the function names. Before it makes the add-on, the server reads the module's
info and refuses it, with a log line saying why, and runs without 3D, when:

| Check | Refused when |
|---|---|
| Name | the file is another add-on's module |
| Add-on API | the module was built for another `kAddonApiVersion` |
| Toolchain | another compiler family, standard library, platform or CPU (`addonToolchain()`, for example `gcc/libstdc++/linux/x86_64` or `msvc/msvcstl/windows/x86_64`) |
| Client versions | its gs2lib numbers the 3D client versions differently (`addonVersionTableFingerprint()`) |

Within one add-on API version, a module works with any server build, older or
newer, made with the same kind of toolchain. A server release and a module
release made by the same CI always match.

### Versions and tags

| What | Version | Tag | Notes |
|---|---|---|---|
| The server | `v<upstream>-seihai.N` | `v4.0.0-seihai.6`, ... | `VERSIONING.md` |
| The Graal 3D module | `addons/graal3d/VERSION` | `graal3d-v<version>` | `addons/graal3d/README.md`, `.github/workflows/graal3d-module.yml` |
| The add-on API | `kAddonApiVersion` | none | bumped with any change to the three headers |
| The 3D protocol | stock Graal3D (Torque 19, 21, 24) | none | the module does not speak `seihai3d` yet (seihai-core `three_d_ext`) |

The module's version moves with 3D changes only; a server release with no 3D
change does not need a new module, and a 3D fix does not need a server release.

## Why an add-on and not a separate server

Three ways were weighed for 3D on GServer-v2 (2026-11-25), against upstream
`xtjoeytx/GServer-v2` `beta4` and this fork:

- **In-process add-on (this).** The 3D side needs the gserver's own state: which TCP
  player a UDP connect request belongs to (account and address), the player's
  flags as they change, weapons, `triggeraction`, the NPC server's variables.
  In-process it reads them directly. Upstream would need the eleven call sites in
  the table above (about 30 changed lines in existing files, see
  `CHANGES-SEIHAI.md`) plus the new `addons/` files and the CMake option.
- **A companion 3D server next to GServer-v2.** It needs fewer core changes (the
  missing-file answer at least; `hq_level = 4` in `adminconfig.txt` already lists
  it as 3D), but it cannot tell which logged-in player a UDP connection is except
  by address, does not see flag changes, and cannot run `triggeraction` or give
  weapons without a new server-to-server protocol, which would be a larger change
  than the hooks.
- **Point 3D players at the Seihai gserver.** No GServer-v2 change at all, but the
  3D players are on a different server from the GServer-v2 players.

The add-on is kept in this fork; it is not offered upstream for now (2026-11-25).
