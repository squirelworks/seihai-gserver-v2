# seihai-graal3d: the Graal 3D add-on as a module

<!-- Seihai project, 2026-11-27: see CHANGES-SEIHAI.md. -->

Part of the Seihai fork of GServer-v2 (GPL-3.0, see `LICENSE.md` and
`CHANGES-SEIHAI.md`).

`seihai-graal3d` is the Graal 3D side of this server (the Torque UDP server that
the 3.151 to 5.x clients open on the listed port + 1, see `bin/docs/graal3d.md`)
built as its own shared library. A gserver built without the 3D side
(`-DGRAAL3D=OFF`) loads it at start when `graal3d = true`, the way the Seihai
gserver starts its NPC-server beside it. How it is built, loaded and checked is
in `docs/addons.md`, "Add-on modules".

| File | |
|---|---|
| `VERSION` | the module's version; CMake reads it, and the release tag is `graal3d-v<version>` |
| `README.md` | this file, with the release notes below |

The sources are the add-on's own, `server/src/graal3d/` and
`server/include/graal3d/`, plus `server/src/graal3d/Graal3DModule.cpp`.

## Install

1. Take the module for your system from a `graal3d-v*` release (or build it:
   `cmake -S . -B build -DGRAAL3D=OFF -DGRAAL3D_MODULE=ON && cmake --build build --target seihai-graal3d`).
2. Put `libseihai-graal3d.so` (Linux), `seihai-graal3d.dll` (Windows) or
   `libseihai-graal3d.dylib` (macOS) in the server's folder or its `addons/`.
3. Set `graal3d = true` in `config/serveroptions.txt`, add the world
   (`config/graal3dworld.txt`) and open UDP port serverport + 1.
4. Start the server. The log names the module, its version and what it was built
   from, then `Graal3D: listening on UDP port ...`.

The module and the server must share the add-on API version and toolchain (see
`docs/addons.md`). Modules and servers built by this repo's CI for the same
platform always do.

## Compatibility

| Module | Add-on API | Built from | Server builds it loads into | 3D protocol |
|---|---|---|---|---|
| 0.1.1 | 1 | `seihai` after #186 | any server with add-on API 1 (`kAddonApiVersion = 1`), same toolchain | Torque 19, 21, 24; seihai3d: none |
| 0.1.0 | 1 | `seihai` after #183 | any server with add-on API 1 (`kAddonApiVersion = 1`), same toolchain | Torque 19, 21, 24; seihai3d: none |

## Release notes

### Unreleased

### 0.1.1 (2026-11-27)

- The first module released for the server releases that have no 3D code of their
  own: from `v4.0.0-seihai.6`, every server archive is built with `-DGRAAL3D=OFF`
  and gets its 3D from this module, released at its own times.
- Built from `seihai` after #186: on Linux a linker version script
  (`Graal3DModule.map`) leaves only the three entry points exported (0.1.0 also
  exported 22 standard-library objects GCC's headers mark visible), and
  `graal3dmodule = off` logs one line saying no module is loaded.
- The 3D behaviour is 0.1.0's, which a real 5.12 ran with a server with no 3D code
  (below). Installing, the server options and the limits:
  `docs/GSERVER-V2-3D-STARTUP.md`.

### 0.1.0 (first module, 2026-11-27)

- The Graal 3D add-on as of `seihai` #183, built on its own: the Torque UDP side
  for protocols 19 (3.151, 3.153, 4.04r2), 21 (4.20r6) and 24 (4.20r8, 4.20 Linux,
  5.00r7, 5.12, 5.24; Frankenstein 6.037 links as 5.12), streamed and large
  terrains, water, ground cover, distance scoping, several players, 2D players
  shown in 3D, the start-up file checks and the lab switches. Everything in
  `bin/docs/graal3d.md` applies as written.
- Same behaviour as the built-in add-on: the module is the same code reached
  through `AddonHost` instead of `Server`.
- Checked on a real 5.12 (Windows, under Wine with software OpenGL, in a
  headless lab VM): a server with no 3D code and this module did everything
  the built-in add-on did, through login, terrain streaming, a return visit and
  two players seeing each other, with no crash or error line.
- Exports only its three entry points (a linker version script on Linux).
