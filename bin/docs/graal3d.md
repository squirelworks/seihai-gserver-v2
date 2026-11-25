# Graal 3D (Seihai)

<!-- Modified by the Seihai project, 2026-11-14: reconcile recorded lab coverage; 2026-11-19: protocol 19 Sky and Player; 2026-11-20: default Camera facing the spawn; 2026-11-25: lab raw objects and events. -->

This file is part of the Seihai fork (GPL-3.0, see `LICENSE.md` and `CHANGES-SEIHAI.md`).

The Graal clients from 3.151 (Linux) and 3.153 to 5.x include a Torque engine
3D mode (`Graal3DEngine.dll` in 4.x and 5.x), separate from the February 2000
Graal3D program. Graal 6 removed it. Server support and real-client results vary
by protocol: the 2026-11-14 lab reached the control object on 3.151 and 3.153,
but both crashed on their first frame; 4.20r8 played a full 3D session. See
"Protocol 19" and "Lab coverage and remaining checks" below.

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

# optional: where each client's Camera sits from the spawn point (default
# 0 -5 2), and an axis and angle to turn it; without one it faces the spawn
camera 0 -5 2

# optional, protocol 19 (3.153, 4.04r2) only: the control object, a stock TGE
# Player that walks (the default) or the Camera of the 2026-11-14 labs
tgecontrol player
# the TGE .dts the client loads for that Player; without it the Camera is used,
# since 4.04r2 drops a Player whose PlayerData has no loadable shape
tgeplayershape player.dts

# optional: a Sky: six sky-box textures (front, right, back, left, top, bottom),
# comma separated ("-" for none), then the visible distance and fog colour
sky sky_fr,sky_rt,sky_bk,sky_lf,sky_up,sky_dn 500 0.5 0.6 0.8
# the Sky's three unnamed bools and its solid colour (see below)
skyoptions 1 0 0 0.4 0.6 0.9

# optional: scripts the client runs in 3D mode
weapon3d -Move3D config/move3d.gs2
weapon3d -System
```

`weapon3d` sends a script over the 3D text channel (`addweapon`, then
`weaponscript`) when the mission starts: the text of the named file, or, without
a file, the server weapon of that name (its client bytecode, or its clientside
source when it has none). A file is compiled to GS2 bytecode when the NPC-server
is on (`serverside = true`), as GS2 unless the file starts with its own `//#`
engine line: in the lab the 5.12 client took source text this way but never ran it.
A server weapon (`weapon3d <name>` with no file) needs a `//#GS2` line after
`//#CLIENTSIDE` to get bytecode. The 5.x client does not move its player by itself: in 3D mode
its key bindings call `moveForward2`, `moveBackward2`, `moveLeft2`, `moveRight2`
and `jump`, which only such a script defines.

In the lab, a 5.12 client script read the arrow keys with `keydown2` but could not
reach its own 3D object (`findobject` returns nothing for it), so the gserver moves
the object instead. In 3D mode the client sends a script's `triggerserver` and
`client.` flag writes over the 3D text channel, as `t <args>` and `s <flag>=<value>`
lines of an "events" text. `triggerserver(<kind>, <weapon>, "g3dpos", x, y, z)` moves
the player's object there and ghosts it to every 3D client, its own included. For
two seconds after that, the client's own reports of its position are ignored, so
an older report does not put the object back. At login the gserver sets
`clientr.g3dpos` to the spawn point, so a script can start from there. Other
`triggerserver` calls from a 3D script go to the NPC-server as weapon triggers
(kind `weapon`) or are logged as ignored.

The `Sky` layout is read from the 5.12 client (`Sky::packUpdate`). The client's
GS2 property names for Sky include `useskytextures`, `renderbottomtexture`,
`norenderbans` and `skysolidcolor`, but the read does not tie the three bools and
the colour in the update to those names, so `skyoptions` sets them by position.
The 5.12 client splits the Sky's texture field on commas and loads one texture
per entry, the first six as the sky box. It asks the gserver for each one without
an extension (`sky_fr`), so for these clients the gserver also tries `.png`,
`.jpg`, `.jpeg`, `.bmp`, `.gif` and `.dds` when a requested name has none. The
client stores them under `levels3d/images/sky/`, and loads the textures only when
it unpacks the Sky, so the server sends the Sky whole again once the mission
start is done, by when the downloads are in. A real 5.12 client drew the sky box
this way in the lab. `skyoptions 1 0 0` and `1 1 1` gave a light box fading into
the fog; `0 0 0` a more saturated one.

Each 4.20r6 and 5.x client also gets its own `Camera` ghost
(`G3DCamera_<account>`), and the server names it as the client's camera
(`ServerConnection.camera` in the client's scripts) once the client has it. The
5.x client draws its 3D view only through a Camera: with just the control object
the window stays black (lab 2026-11-20, real 5.12 on Windows 10). It sits 0 -5 2
from the spawn point, or where the `camera` line says, and faces the spawn point
unless that line gives a rotation. The server sets no camera mode and no followed
object: the client's scripts may set `cameramode`, `position` and `eulerrotation`.

Rotations are an axis and an angle in radians, and the clients turn the opposite
way from the right-hand rule: a positive angle about +x tilts the view down, one
about +z turns it right. For `spawn 0 0 1` the default camera is
`camera 0 -5 2 1 0 0 0.38`. A `.3ds` shape needs a material: one with only
vertices and faces loads without an error but is not drawn (5.12 and 4.20r6,
lab 2026-11-20).

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
| Control object (4.20r6, 5.x) | done. Once the client has its player object and has acked both mission phases, the server makes that object the control object, which is login step 5. It must come after the mission start, because `MissionStartPhase2` sets the 5.x client's login step back to 3 and step 5 is only set by the first control object. Step 6 follows once every TCP file request is answered. With `graal3d` on, a missing file gets `PLO_FILESENDFAILED` for these clients, so it does not hold the login up. |
| From the client: its own object | read in every packet: the position, and the client's changes to the object (moves, chat, shape). Other players see them. |
| "Connecting..." window | The 5.x client builds its 3D view itself (`GUIContainer`, `GraalControl3D`, from GS2 built into the client) and shows it at login step 6. Its "Connecting..." window stays on top until the TCP side sends `PLO_SERVERLISTCONNECTED`, so with `graal3d` on, every 3.153 to 5.x client gets that packet at login, with or without a listserver. |
| Camera | one `Camera` per client, facing the spawn point unless the `camera` line says otherwise, named as its camera in every packet once its ghost is in |
| `Sky` | with a `sky` line: init and material list, the later sections (storm, wind, fog changes) left clear. Layout from the 5.12 client; a real 5.12 client drew the sky box in the lab (see "The world"). |
| Scripts in 3D mode | with `weapon3d` lines: `addweapon` and `weaponscript` over the 3D text channel. A 5.12 lab client compiled and ran one, and its `keydown2` saw the arrow keys. |
| Walking | a script's `triggerserver(..., "g3dpos", x, y, z)` and the gserver moves the player's object. Lab 15 showed the trigger reaching the gserver; the move is not yet run with a real client. |
| `TerrainBlock`, `WaterBlock` | **no layout yet.** The lab's `rawobject` and `rawevent` lines (see "Lab: raw objects and events") send them with hand-written fields. |
| `movetype` | **not sent.** |
| From the client: `TerrainRequestUpdatesEvent` | logged: its length, how many of its 256 stamps are set, the first eight `(x,y)=stamp`, and the payload as hex. Other events the server does not decode are logged with their length and first 64 bytes. |
| 3.15x and 4.04r2 game packets | done up to a walking `Player` control object and a `Sky`. A real 4.04r2 runs a `Camera` session with the `Sky`, and a `Player` session once `tgeplayershape` names a loadable TGE `.dts` (lab 2026-11-19). See "Protocol 19" below. |

## Lab: raw objects and events

For classes whose update layout has not been read (`TerrainBlock`, `WaterBlock`,
the replicators) and events such as `TerrainChunkEvent`, the world file can
describe the bits by hand, so a lab can try a layout by editing the file and
restarting, without a new build:

```
# rawobject <class> <fields...>: a ghost of <class> whose update is the fields
rawobject TerrainBlock f1 str:lab.ter u32:256 f32:8.0
# rawevent [late] <class> <fields...>: one guaranteed event; `late` waits until
# the client has finished the mission start
rawevent late TerrainChunkEvent i4:0 i4:0 u32:1 zfile@10:world3d/chunk_0_0.bin
```

| Field | Writes |
|---|---|
| `f0`, `f1` | a flag |
| `i<N>:<v>` | an N-bit int (1 to 32); decimal, `0x` hex, or negative (two's complement) |
| `u8:`, `u16:`, `u32:` | `i8`, `i16`, `i32` |
| `f32:<v>` | a raw 32-bit float |
| `str:<text>` | a string, in the packet's string mode (`graal3dstringprefix`); `%20` for a space, `%23` for `#`, `-` for an empty string |
| `hex:<bytes>` | bytes given as hex |
| `file:<path>` | a file's bytes (the path from the server folder) |
| `zfile:<path>` | a file's bytes, zlib-compressed with the zlib header |

`hex`, `file` and `zfile` take a length prefix: `file@10:x.bin` writes the byte
count as an int10 first. A file too long for its prefix, a missing file, or an
update or payload over 16383 bits (an int14 length) is logged at start-up.

The client reads every update and event with its length, so a wrong guess is
skipped rather than misreading the rest of the packet. A raw object goes to every
client whose class table has its class: 4.20r6 and 5.x list `TerrainBlock` and
`WaterBlock`, and protocol 19 (3.153, 4.04r2) now lists them too, after `Camera`,
`Player` and `Sky` so those keep their ids. Raw objects on protocol 19 go out with
the other ghosts, after the datablocks. Nothing here has been run with a real
client yet; the 2026-11-25 terrain lab is the first user.

## Protocol 19 (3.151 Linux, 3.153, 4.04r2)

These clients run the stock Torque game layer: datablocks, `Camera` / `Player`
ghosts and moves, with a three-phase mission start. The layouts come from a static
read of the 4.04r2 `Graal3DEngine.dll` (Graal_analysis `torque/protocol19-server.md`);
3.153 is assumed to read the same. What the server does with them:

| Step | What goes out |
|---|---|
| First client packet | `MissionStartPhase1` with the mission sequence (`1`) and the mission name (`Graal3D`). 3.153's script sends the sequence back in its ack, 4.04r2 sends the ack bare; both are taken. |
| Phase 1 ack | One datablock, a `PlayerData` with the `tgeplayershape` shape and the 4.04r2 constructor's values (without a shape, or with `tgecontrol camera`, a `CameraData` with no shape and every optional field clear), as a `SimDataBlockEvent`, then the connection message `DataBlocksDone` (5). |
| `DataBlocksDownloadDone` (6) from the client | `MissionStartPhase2`, the connection message `GhostAlwaysStarting` (3) with the ghost count, and from now on ghosts: the world's `Sky` (or, without a `sky` line, a plain one in its solid colour) and one stock `Player` (or `Camera`) per client, at the world's `spawn` point. No ghost goes out before this, since a ghost naming a datablock the client has not loaded drops the connection. |
| Phase 2 ack, and every create acked | `GhostAlwaysDone` (0), then `MissionStartPhase3`. The client lights the scene and acks. |
| Phase 3 ack | The `Player` (or the `Camera`, in fly mode) becomes the control object, with its packet data. 4.04r2 then raises login step 5. |
| Every client packet after that | The client's moves are read and acknowledged in the server's move ack. A `Player`'s are applied as a plain walk (turn with the move's yaw and pitch, walk at 10 units a second, no gravity or collision, since the server has no terrain), and its packet data (position, velocity, head and rotation) goes out again in the next packet. A `Camera`'s are not applied: the client moves it by prediction. |

The `Sky` update is the 5.12 one: 4.04r2's `Sky::unpackUpdate` reads the same
layout. 4.04r2 takes the material list's seventh entry as its environment map and
crashes without one, so a protocol 19 `Sky`'s list is padded to seven entries
with its first name (`g3d_sky` for the plain one). The `Player`, `PlayerData` and `Player` packet data layouts were read from
the 4.04r2 `Graal3DEngine.dll` on 2026-11-19; they are stock TGE 1.x, with Graal's
md3 model name in `PlayerData`. 4.04r2's `Player::onNewDataBlock` fails without a
shape it can load, so the `Player` is only sent with `tgeplayershape`. 4.04r2 loads
`.dts` version 24 and older; a shape with no sequences is accepted (the lab used a
version 24 `octahedron.dts`). The shape can sit next to the client, or in the
server's world folder for the client to download over TCP. No `.dts` with the real
player sequences is held yet, so the ghost has no walk animation.

Not done for these clients: other players' `Player`s, the world's objects (they are
`GameObject`s, a 4.20r6 and later class), `Sun`, terrain, and damage flash or
white-out. Flags and `weapon3d` scripts go over the 3D text channel as for 5.x.

Labs 2026-11-14 and 2026-11-19, with real clients:

| Client | How far it gets |
|---|---|
| Linux 3.151, Mac 3.153 | Every step above with a `Camera` and no `Sky`, up to the control object, and their moves are acked. Then each crashes on its first frame (a null read while drawing). Likely the missing `Sky`, which 4.04r2 also needs (2026-11-19); not rerun yet. |
| Windows 4.04r2 | 2026-11-14: crashes while starting its own 3D engine, before it opens the UDP port (on Windows 10, and under Wine, where a 2D server works). 2026-11-19, under Wine with no memory patch: with a `Camera` and the `Sky` (padded to seven materials by the server) it draws the sky and fog and holds the link; with `tgeplayershape` naming a loadable version 24 `.dts`, the `Player` ghost is accepted and walks (193 move packets, the position tracked from spawn), whether the shape sits next to the client or is downloaded from the server's world folder. A `Player` with no reachable shape drops the link in under a second. |
| Seihai's Ato client, as 4.04r2 and 3.153 | Every step, and the server reads its moves. |
| 4.20r8 | Not protocol 19: it speaks protocol 24 and plays a full 3D session. |

3.15x clients join on a `newmain` world (`default` or `classic`), whose allowed versions
already cover them; `modern` starts at 4.04r2.

The server log shows each link: `Graal3D: <account> linked from <ip> (protocol 24, 5.x ...)`.

## Which client each finding comes from

The 3D clients are not one protocol. Each layout here was read from one build, and
nothing says the others match.

| Client | Protocol | What is known |
| --- | --- | --- |
| 3.151 (Linux), 3.153, 4.04r2 (the first Graal 3D) | 19 | Stock TGE underneath: datablocks, moves, TGE object classes and a three-phase mission start, with no `GameObject`. Its layouts were read from the 4.04r2 `Graal3DEngine.dll`. The real 3.151 and 3.153 clients follow them up to the control object (see "Protocol 19"). The `GameObject` ghosting, control-object and camera work above does not apply to it. |
| 4.20r6 | 21 | Its class table (read from 4.20r6) has `GameObject`, `Camera` and `Sun`, so the server ghosts to it. The layouts of those classes were not read from 4.20r6: they are assumed to match 5.00r7. |
| 5.00r7, 5.12 | 24 | The `GameObject`, `Camera`, `Sun`, control-object and ghost layouts were read from the 5.00r7 dll. The handshake was checked against a real 5.12 client. |

The camera is off unless the world file has a `camera` line, so a client whose
`Camera` layout differs is only affected when it is turned on.

## Lab coverage and remaining checks

The handshake bytes are tested against the packets a real 5.12 client sent in a
2026-10-30 lab (`Catch_tests/Graal3D`). Later lab results recorded above include a
5.12 client running a 3D weapon script and drawing the sky box, and the
2026-11-14 protocol 19 run reaching the control object on real Linux 3.151 and
Mac 3.153 clients before their first-frame crashes. That run also records a
full 3D session on 4.20r8 (protocol 24). These results supersede the earlier
blanket statement that the game layer had never run against a real client.

They do not establish parity across all builds. The protocol 21 object layouts
are still assumed to match 5.00r7, the Walking row above still lacks a real-client
move check, and protocol 19 still lacks the objects listed in its section.
The first-frame crash cause remains unknown; Windows 4.04r2 crashed before UDP
connection. Keep those limits separate from the successful individual checks.
