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
```

To list the server as a 3D server, set `hq_level = 4` in `config/adminconfig.txt`.
seihai-listserver tags HQ level 4 as `3 ` on the gen 4 and gen 5 lists.

Open UDP `serverport + 1` in the firewall as well as the TCP port.

## What the server does

| Step | State |
|---|---|
| Torque connect challenge and connect request | done. The request must name `GameConnection` and `Graal3D`, with protocol 19 (3.153, 4.04r2), 21 (4.20r6) or 24 (5.x). |
| Who may connect | The first argument of the request is the account. It must be logged in over TCP, from the same IP address, with a 3.153 to 5.x client. Until the TCP login is done the request is not answered; the client repeats it. |
| Connect accept | done, with Graal's class-name table for that protocol. |
| Keep-alive, acks, time-out (30 s) | done for every protocol. |
| Mission start (`MissionStartPhase1`, `2`) | 5.x and 4.20r6. The client answers each phase and raises `onGraal3DLoginStep`. |
| Flags | sent at link time as one `events` text (`s name=value` lines), then every flag change as `set` / `unset`. |
| From the client: `events` text | `s name=value` sets a flag or client variable (as `PLI_FLAGSET`), `w name` gives a weapon, `t weapon,name,params` runs a serverside triggeraction on that weapon. |
| The 3D world (objects, terrain, the player's own object) | **not done.** No objects are ghosted, so a 5.x client stops at login step 4 ("mission lighting done") and does not switch to the 3D view. |
| 3.153 and 4.04r2 game packets | **not done.** They use Torque's older game packet (moves, datablocks). Those clients get the handshake and keep-alive only. |

The server log shows each link: `Graal3D: <account> linked from <ip> (protocol 24, 5.x ...)`.

## Not checked with a real client yet

The handshake bytes are tested against the packets a real 5.12 client sent in a
2026-10-30 lab (`Catch_tests/Graal3D`). The mission phases, the text channel and
the flag bridge were written from static reads of the 4.04r2 and 5.0.0.7
`Graal3DEngine.dll` and have not been run against a real client.
