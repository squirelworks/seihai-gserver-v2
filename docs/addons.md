# Server add-ons (Seihai)

<!-- Seihai project, 2026-11-25: see CHANGES-SEIHAI.md. -->

This file is part of the Seihai fork (GPL-3.0, see `LICENSE.md` and `CHANGES-SEIHAI.md`).

An add-on is an optional subsystem that sits beside the gserver instead of inside
it. It is compiled in with its own CMake option, switched on by its own server
option, and reached by the core only through a small set of hooks. With no add-on
compiled in, or none switched on, every hook is a no-op and the server behaves as
upstream GServer-v2 does.

The Graal 3D side (the Torque UDP server for the 3.151 to 5.x clients, see
`bin/docs/graal3d.md`) is the first add-on. It is built by default in this fork
(`-DGRAAL3D=ON`) and runs only with `graal3d = true` in `serveroptions.txt`.
Build with `-DGRAAL3D=OFF` for a server with no 3D code at all.

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

An add-on may also call into the server as any subsystem does (players, flags,
weapons, the NPC server). The hooks are only what the core must call.

## Adding an add-on

1. Put it in its own folder (`server/include/<name>/`, `server/src/<name>/`).
2. Subclass `preagonal::ServerAddon`, with a static `create(Server&)` that returns
   `nullptr` unless its server option is on.
3. In `server/CMakeLists.txt`, add an `option(<NAME> ...)` that appends its sources
   and defines `SEIHAI_ADDON_<NAME>`.
4. List it in `ServerAddons::start` (`server/src/addons/ServerAddons.cpp`) inside
   `#ifdef SEIHAI_ADDON_<NAME>`.
5. If it needs the core to do something the hooks above do not cover, add one hook
   with a default that keeps the core's behaviour, and document it here.

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
