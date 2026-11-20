# Seihai GServer-v2

This is the Seihai project's fork of [GServer-v2](https://github.com/xtjoeytx/GServer-v2)
`beta4`, the open-source Graal game server, with fixes so that real 1.33 to 2.31
clients play on it correctly. It uses the
Seihai fork of its support library,
[seihai-gs2lib](https://github.com/squirelworks/seihai-gs2lib).

- What changed and why: [CHANGES-SEIHAI.md](CHANGES-SEIHAI.md).
- Known bugs, how to reproduce them and where to start looking:
  [KNOWN-ISSUES.md](KNOWN-ISSUES.md).
- Prebuilt Linux, Windows and macOS builds, each with its source, are on the
  [Releases page](https://github.com/squirelworks/seihai-gserver-v2/releases).

The licence is GPL-3.0, as upstream.

## Lab world

The `bin` folder (and every release archive) holds a lab world that listens on
`127.0.0.1` only, with UPnP off. It has three servers:

| Server | Clients | Port |
| --- | --- | --- |
| `servers/classic` | 1.33 to 1.41r1 | 14803 |
| `servers/default` | 2.00 to 3.153 | 14802 |
| `servers/modern` | 4.04r2 to 6.037, and the `.gpak` packages of 4.20r8 and later | 14805 |

Start one from the `bin` folder with `gs2emu --server default` (or `classic`,
or `modern`). Any account logs in without a listserver. The details, and how to
open a server to other machines, are in
[bin/docs/lab-world.md](bin/docs/lab-world.md).
[seihai-servertools](https://github.com/squirelworks/seihai-servertools) can
start a whole lab world in one step (`seihai-servertools lab start`).

To try the built-in NPC-server, run `./gs2emu --server npcdemo` from `bin/`. It
is a loopback sample world described in `bin/servers/npcdemo/README.md`. The GS1
it does not support yet is listed in `docs/lab/2026-10-31-npcserver-gs1.md`.

## Getting started

Download the archive for your system from the
[Releases page](https://github.com/squirelworks/seihai-gserver-v2/releases) and
unpack it, or build from source (see "Building" in the upstream README below;
the program is then in `bin/`). From the `bin` folder:

```
./gs2emu --server default      # gs2emu.exe on Windows
```

That starts the lab world's `default` server on `127.0.0.1:14802`. To set up a
server of your own, follow [bin/docs/start-here.md](bin/docs/start-here.md):
make your account, add it to `staff=` in `config/serveroptions.txt`, and choose
the server's generation.

## Command line

| Option | What it does |
| --- | --- |
| `-s DIR`, `--server DIR` | Runs the server in `servers/DIR` |
| `-p PORT`, `--port PORT` | The port to listen on, instead of the one in the server's settings |
| `--interface IP` | The address to listen on |
| `--serverip IP` | The address the listserver gives players for this server |
| `--localip IP` | The address given to players on the same network as the server |
| `--staff ACCOUNT` | Adds `ACCOUNT` to the staff list, and makes the account from `YOURACCOUNT` if it does not exist yet |
| `-h`, `--help` | Prints the usage |

These options come from upstream and have two quirks. The long forms `--port`,
`--interface`, `--serverip`, `--localip` and `--staff` are only read when they
come before `--server`, while the short `-p` is only read after `-s`. And
`--name NAME` (or `SERVERNAME`) is accepted but has no effect yet.

Without `--server`, gs2emu picks the server this way: the current folder, if it
has `config/serveroptions.txt`; else the folder named in `startupserver.txt`
(`default` in the lab world); else the only folder in `servers/`.

With the environment variable `USE_ENV` set, the command line is ignored and the
same settings come from `SERVER`, `PORT`, `INTERFACE`, `SERVERIP`, `LOCALIP`,
`SERVERNAME` and `STAFFACCOUNT` instead, as the Docker image uses them
([Docker.md](Docker.md)).

The server's own settings are text files in its `config/` folder, chiefly
`serveroptions.txt`; [bin/docs/server.md](bin/docs/server.md) describes them.

## Fuzzing

A standalone Clang libFuzzer project under `fuzz/` exercises packet framing,
decryption and decompression through `processBuffer`. See `fuzz/README.md`; a
weekly workflow (`.github/workflows/fuzz.yml`) runs it.

## The rest of Seihai

Seihai also writes a new server stack in Rust, separate from this fork:
[seihai-gserver](https://github.com/squirelworks/seihai-gserver),
[seihai-listserver](https://github.com/squirelworks/seihai-listserver),
[seihai-rc](https://github.com/squirelworks/seihai-rc) and
[seihai-npcserver](https://github.com/squirelworks/seihai-npcserver). The client is
[seihai](https://github.com/squirelworks/seihai), and the research notes behind
the fixes are in [Graal_analysis](https://github.com/squirelworks/Graal_analysis).

The upstream README follows.

---

# GServer-v2
[![Build Status](https://jenkins.amigadev.com/job/GServer-v2/job/master/badge/icon)](https://jenkins.amigadev.com/job/GServer-v2/job/master/)

Created by: Joey, Nalin, dufresnep, Codr, Marlon.
Based off the original work by 39ster.
For their additional work on the old gserver, special thanks go to:
	Agret, Beholder, Joey, Marlon, Nalin, and Pac.
	
## Building

### Required dependencies
- C++23 compiler (min supported: GCC 14, Clang 18, MSVC 2022 17.7)
- Java JRE
- CMake 3.28
- Ninja build system
- vcpkg (https://vcpkg.io/en/getting-started)

Linux users can install `openjdk-21-jre` or similar via their package manager, Windows users can install from [Microsoft](https://learn.microsoft.com/en-us/java/openjdk/download) or via `winget`.

vcpkg needs to be installed and the `VCPKG_ROOT` environment variable needs to be set to the location where it exists.
The directory should be writable by the user running the build (unless you want to spend extra time reading documentation and configuring the software).

### Build with CMake

If using command-line cmake, you would start a build like so:
```
cmake --preset "Windows Release" -G Ninja -B build
cmake --build build
```

On a Linux build, you would use `Linux Release` as the preset.

IDEs such as Visual Studio and CLion have CMake support built-in and can be used to easily build and configure the projects.

### macOS builds

macOS is built and tested on the lab's Sequoia (macOS 15) VM with the same build
and test commands the Linux job runs, using the `macOS Release` preset; GitHub CI
covers Linux and Windows. The release workflow still builds macOS archives for a
release draft (a `v*` tag or a manual run with a version), so a release keeps its macOS downloads.

## Quick Start Instructions

How-to setup a server:

1. Under the accounts folder, rename the text file `YOURACCOUNT.txt` to your account name.  For example: `KuJi.txt`
2. Modify `(defaultaccount).txt` to your liking.  This is the default settings new players will start with.  It can also be modified via RC.
3. Open `config/serveroptions.txt` and edit it to your liking (like selecting a server generation).  Be sure to modify the settings under "Private server options".
4. Find the line that starts with `staff=` in `config/serveroptions.txt`.  Replace `YOURACCOUNT` with your account name.  Anybody who needs RC access must be added to this line with their account names separated by commas.  Additionally, RC users must have their IP range changed to at least `*.*.*.*` in their account to connect.
5. Run `gs2emu.exe` -- enjoy.

Next steps:

[Start here](bin/docs/start-here.md) for more detailed instructions on how to set up and configure your server.

## Documentation

Documentation is available in the `/docs/` folder.
