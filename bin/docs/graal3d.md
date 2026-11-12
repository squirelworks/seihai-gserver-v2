# Graal 3D (Seihai)

This file is part of the Seihai fork (GPL-3.0, see `LICENSE.md` and `CHANGES-SEIHAI.md`).

The Graal clients from 3.153 to 5.x can play on a 3D server. That is not the
February 2000 Graal3D program: it is a Torque engine 3D world built into the normal
client (`Graal3DEngine.dll` in 4.x and 5.x). Graal 6 removed it.

## How a client joins a 3D server

1. The server list marks a 3D server with a `3 ` in front of its name.
2. The client opens two connections to the same host at once:
   - the normal Graal TCP login on the listed port `P`, exactly as for a 2D server;
   - a Torque UDP connection on port `P + 1`.
3. While in 3D mode the client ignores the TCP level, warp and flag packets. Flags,
   weapon pick-ups and serverside triggers go over the UDP side instead. Chat, PMs,
   the player list and file downloads stay on TCP.
4. If the TCP session ends, the client drops the 3D side too.

## Turning it on

In `config/serveroptions.txt`:

```
graal3d = true
# graal3dport = 0      (0 or unset: serverport + 1, the port the clients use)
# graal3dstringprefix = true
```

`graal3dstringprefix` says whether strings inside game packets start with Torque's
"shares a prefix with the last string" flag. The 5.00r7 `Graal3DEngine.dll` gives
every game packet a string buffer, as stock TGE 1.x does, so the default is `true`.
If a lab client's events log `does not fill its length with packet strings on; it
does with them off`, set it to `false`.

To list the server as a 3D server, set `hq_level = 4` in `config/adminconfig.txt`.
seihai-listserver tags HQ level 4 as `3 ` on the gen 4 and gen 5 lists.

Open UDP `serverport + 1` in the firewall as well as the TCP port.

## The world

`config/graal3dworld.txt` lists what every 3D client is sent. One entry per line,
`#` starts a comment:

```
# object <name> <shape> <x> <y> <z> [<axis x> <axis y> <axis z> <angle in radians>]
object Ground ground.dts 0 0 0
object Tower tower.dif 20 5 0  0 0 1 1.5708

# sun <direction x y z> <colour r g b a> <ambient r g b a>
sun 0.57 0.57 -0.57  0.8 0.8 0.8 1  0.3 0.3 0.35 1

# the shape of every player's own object, and where it starts
playershape player.dts
spawn 0 0 1

# optional: give each client a Camera, this far from the spawn point
camera 0 -3 2
```

With a `camera` line each client also gets its own `Camera` ghost
(`G3DCamera_<account>`), and the server names it as the client's camera
(`ServerConnection.camera` in the client's scripts) once the client has it. The
server sets no camera mode and no followed object: the client's scripts set
`cameramode`, `position` and `eulerrotation` themselves. Without the line no camera
is sent, which login steps 5 and 6 do not need.

A shape is a file name the client loads: `.dts`, `.dif`, `.3ds` or `.ft`. A client
asks the gserver over TCP for any file it does not have, and stays at login step 5
until every one of those requests has been answered (with the file or with "file not
found"), so put the shapes and their textures in the server's file folders.

Each linked player also gets a `GameObject` named `G3DPlayer_<account>`, with the
player's nickname, on every 3D client. A player's own object is that client's control
object. Each client reports its position in every packet, and the server moves the
player's object on the other clients. Chat said on the object (`chat`) is passed on
too. The server never sends a player's position back to that player.

## What the server does

| Step | State |
|---|---|
| Torque connect challenge and connect request | done. The request must name `GameConnection` and `Graal3D`, with protocol 19 (3.153, 4.04r2), 21 (4.20r6) or 24 (5.x). |
| Who may connect | The first argument of the request is the account. It must be logged in over TCP, from the same IP address, with a 3.153 to 5.x client. A 5.x guest sends `guest`, while the gserver names guests `pc:<number>`, so `guest` matches the guest client logged in from that address. Until the TCP login is done the request is not answered (the client repeats it), and the log says once per address that no TCP client matched. |
| Connect accept | done, with Graal's class-name table for that protocol. |
| Keep-alive, acks, time-out (30 s) | done for every protocol. |
| Mission start (`MissionStartPhase1`, `2`) | 5.x and 4.20r6. The client answers each phase and raises `onGraal3DLoginStep`. |
| Flags | sent at link time as one `events` text (`s name=value` lines), then every flag change as `set` / `unset`. |
| From the client: `events` text | `s name=value` sets a flag or client variable (as `PLI_FLAGSET`), `w name` gives a weapon, `t weapon,name,params` runs a serverside triggeraction on that weapon. |
| Ghosting (4.20r6, 5.x) | done. Objects are created, updated (only what changed) and deleted in the ghost section of each packet, and what a lost packet carried is sent again. `GameObject` (name, shape, skin, position and rotation, velocity, chat, `attr[1..8]`, nickname and colour, the solid/shadow/collision flags) and `Sun`. |
| Control object (4.20r6, 5.x) | done. Once the client has its player object, the server makes it the control object, which is login step 5. Step 6 follows once the client's TCP file downloads are done. |
| From the client: its own object | read in every packet: the position, and the client's changes to the object (moves, chat, shape). Other players see them. |
| Camera | with a `camera` line in the world file: one `Camera` per client, named as its camera in every packet once its ghost is in |
| `Sky`, `TerrainBlock`, `movetype` | **not sent.** None is needed for login steps 5 and 6. Terrain and sky layouts are only partly known. |
| 3.153 and 4.04r2 game packets | **not done.** They use Torque's older game packet (moves, datablocks). Those clients get the handshake and keep-alive only. |

The server log shows each link: `Graal3D: <account> linked from <ip> (protocol 24, 5.x ...)`.

## Which client each finding comes from

The 3D clients are not one protocol. Each layout here was read from one build, and
nothing says the others match.

| Client | Protocol | What is known |
| --- | --- | --- |
| 3.153, 4.04r2 (the first Graal 3D) | 19 | Handshake and keep-alive only. It is stock TGE underneath: datablocks, moves and TGE object classes, with no `GameObject` and no mission phases. Its events were read from the 4.04r2 `Graal3DEngine.dll`. Nothing in the ghosting, control-object or camera work applies to it. |
| 4.20r6 | 21 | Its class table (read from 4.20r6) has `GameObject`, `Camera` and `Sun`, so the server ghosts to it. The layouts of those classes were not read from 4.20r6: they are assumed to match 5.00r7. |
| 5.00r7, 5.12 | 24 | The `GameObject`, `Camera`, `Sun`, control-object and ghost layouts were read from the 5.00r7 dll. The handshake was checked against a real 5.12 client. |

The camera is off unless the world file has a `camera` line, so a client whose
`Camera` layout differs is only affected when it is turned on.

## Not checked with a real client yet

The handshake bytes are tested against the packets a real 5.12 client sent in a
2026-10-30 lab (`Catch_tests/Graal3D`). The mission phases, the text channel, the
flag bridge and the ghosting were written from static reads of the 4.04r2 and 5.00r7
`Graal3DEngine.dll` (the ghost, control-object and `GameObject` layouts from 5.00r7,
which also uses protocol 24) and have not been run against a real client.
