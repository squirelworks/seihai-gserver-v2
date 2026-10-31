# Lab run: 6.00 login crash while registering `-gr_movement`

Date: 2026-10-31. Cloud container, loopback only (`127.0.0.1`). Server: this fork,
branch `seihai` at `083306a`, RelWithDebInfo build on Linux (GCC 14). Client: Seihai
(`squirelworks/seihai` at `bfc912a`), `seihai-mae` release build under Xvfb.
The lab listserver from the handoff notes ran on loopback.

## Setup

A copy of `bin/servers/default` named `modern`, with `generation = modern`,
`serverip = 127.0.0.1`, `upnp = false`, the listserver on `127.0.0.1`, and its own lab
port. Everything else is the stock default: `serverside = false`,
`flaghack_movement = true`, and the stock `-gr_movement` weapon on the default account.

## Before the fix

Seihai `--version 6.00r2` (`G3D26090`, gs2lib names it 5.31x). The server logged
`New login: ... [G3D26090 (5.31x)]` and died with SIGSEGV (exit 139). The client
printed `connection closed: unexpected end of file`. Under gdb the stack was
overwritten with the bytes of the `-gr_movement` weapon packet (its name and
`wbomb1.png`).

## Cause

For a client newer than 5.07 (and for any weapon with GS2 bytecode),
`Weapon::registerWeaponWithPlayer` takes the GS2 branch on the modern generation and
asks `getJoinedClassesList()` for the weapon's classes. That function removes the
trailing comma with `pop_back()` without checking that the string is empty. A GS1
weapon such as `-gr_movement` has no joined classes, so it called `pop_back()` on an
empty string (undefined behaviour) and the following write of the class list used a
garbage length. `NPC::getJoinedClassesList()` already has the empty check; the weapon
copy did not.

## After the fix

Same server folder, same accounts:

| Client | Before | After |
|---|---|---|
| Seihai 6.00r2 (`G3D26090`) | server SIGSEGV at login | logged in, level from the board, server stays up |
| Seihai 6.03r7 (`G3D0311C`) | (same path) | logged in, level from the board |
| Seihai 6.015 (`G3D3007A`) | (same path) | logged in, level from the board |

Regression check on a loopback copy of the default (newmain) folder: Seihai 2.05,
2.22 and 3.153 logged in and loaded the level.

## Known gap (unchanged)

A client newer than 5.07 still gets nothing for a GS1 weapon with no bytecode: the
stock server only sends the GS1 text up to 5.07, which is the last client that runs
clientside GS1. So `-gr_movement` does nothing on 5.12 to 6.x, as before. Real 6.00r2
was not rerun in this lab (it needs a real Windows PC with the date faked).
