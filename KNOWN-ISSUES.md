<!-- Seihai project, 2026-11-19: see CHANGES-SEIHAI.md. -->
# Known issues in Seihai GServer-v2

This is a plain-language list of the bugs we know about in this fork, written
for anyone who wants to investigate or fix one. Each entry says what goes wrong,
how to see it for yourself, what we already know about the cause, and where in
the code to start. Most entries stop short of a fix on purpose: we would rather
you look at the evidence and decide.

Last checked against branch `seihai` at the #118 merge (2026-11-19). The
tracking issue for each bug is linked; comment there if you find something, or
open a pull request against `seihai`.

Bugs in the support library underneath this server (sockets, packet ciphers,
compression, client version tokens) are listed separately in
[seihai-gs2lib's KNOWN-ISSUES.md](https://github.com/squirelworks/seihai-gs2lib/blob/seihai/KNOWN-ISSUES.md).

## Contents

- [Before you start](#before-you-start)
- [Open bugs](#open-bugs)
  1. [GS1 `triggeraction` on the NPC-server sends the action name instead of its params](#1-gs1-triggeraction-on-the-npc-server-sends-the-action-name-instead-of-its-params) (#112)
  2. [A GS2 weapon with a string literal never compiles](#2-a-gs2-weapon-with-a-string-literal-never-compiles) (#110)
  3. [`//#GS2` on a weapon's client half is ignored; 4.x and 5.x get text they cannot run](#3-gs2-on-a-weapons-client-half-is-ignored-4x-and-5x-get-text-they-cannot-run) (#113)
  4. [A server name starting with "3 " makes 5.12 think the server is 3D](#4-a-server-name-starting-with-3--makes-512-think-the-server-is-3d) (#114)
  5. [The shipped worlds turn away 4.x and 5.x clients on newmain](#5-the-shipped-worlds-turn-away-4x-and-5x-clients-on-newmain) (#115)
  6. [Classic generation runs no server-side level NPC scripts](#6-classic-generation-runs-no-server-side-level-npc-scripts) (#106)
  7. [Bare `shootball` aims the wrong way after the first shot, and 1.39 draws no ball](#7-bare-shootball-aims-the-wrong-way-after-the-first-shot-and-139-draws-no-ball) (#106)
  8. [Classic generation: a 2.x client's prop 10 is read as a 1.x bow, and the client is kicked](#8-classic-generation-a-2x-clients-prop-10-is-read-as-a-1x-bow-and-the-client-is-kicked) (#107)
  9. [An unknown prop id in a client's props packet ends the session](#9-an-unknown-prop-id-in-a-clients-props-packet-ends-the-session) (#107)
- [Open areas, not single bugs](#open-areas-not-single-bugs)
- [Recently fixed](#recently-fixed)

## Before you start

**Building and running.** Build steps are in [README.md](README.md) and
[docs/SEIHAI-BUILD.md](docs/SEIHAI-BUILD.md). The `bin` folder holds ready-made
lab worlds that listen on `127.0.0.1` only. Start one from `bin/` with
`gs2emu --server <name>`:

| World | Generation | Port | Notes |
|---|---|---|---|
| `classic` | classic | 14803 | 1.33 to 1.41r1 |
| `default` | newmain | 14802 | 2.00 to 3.153 |
| `modern` | modern | 14805 | 4.04r2 to 6.037 |
| `npcdemo` | newmain | 14804 | built-in NPC-server, sample GS1 NPCs |

Any account name logs in without a listserver. More in
[bin/docs/lab-world.md](bin/docs/lab-world.md).

**Words used below.**

- **Generation**: the `generation` key in a world's `config/serveroptions.txt`.
  `classic` is the 1.x protocol, `newmain` is 2.x/3.x, `modern` is 4.x and later.
  It changes how props, scripts and packets are sent, and which line of
  `config/allowedversions.txt` is used to admit clients.
- **Client token**: the 8-character version string a client sends at login, such
  as `GNW03014` (2.22) or `G3D14097` (5.12). `allowedversions.txt` lists tokens
  or token ranges. The token table lives in seihai-gs2lib (`src/IUtil.cpp`).
- **GS1 / GS2**: the two Graal scripting languages. GS1 is the older one. Clients
  up to 5.07 run GS1 as text. GS2 is sent to clients as compiled bytecode.
- **Client half**: everything after the `//#CLIENTSIDE` line in an NPC or weapon
  script. That part is sent to the client; the part before it runs on the server.
- **NPC-server**: the server-side script runner. This fork has one built in
  (`serverside = true` in `serveroptions.txt`).
- **PLO / PLI**: server-to-client and client-to-server packet ids. Names are in
  seihai-gs2lib's `include/IEnums.h`.

**Tests.** The Catch2 tests are under `Catch_tests/` (one folder per area, for
example `NPCServerGS1`, `GS1ToGS2`, `LegacyProps`, `ScriptPackets`). A new test
next to the bug you are fixing is the fastest way to prove it. A packet log
(`DO_PACKETLOG`) shows exactly what the server sent.

**Real clients.** Most of these bugs were seen with real Graal clients in a lab.
We cannot hand out client builds. If you have your own, the "To reproduce"
steps say which version showed the bug.

## Open bugs

### 1. GS1 `triggeraction` on the NPC-server sends the action name instead of its params

Issue: [#112](https://github.com/squirelworks/seihai-gserver-v2/issues/112).
Area: built-in NPC-server, GS1.

**What you see.** A server-side GS1 script runs
`triggeraction x,y,ping,p1,p2;`. The trigger that goes out carries `ping,ping`
instead of `ping,p1,p2`. Whatever receives the trigger gets the action name
twice and never sees its parameters.

**To reproduce.** In the `npcdemo` world, give a level NPC a server-side
`triggeraction x,y,ping,p1,p2;` (for example on `playerenters`), turn on the
packet log, and look at the outgoing trigger packet. Seen with real 2.22, 4.20r8
and 5.12 in the 2026-11-19 lab (squirelworks/seihai-core#112).

**What we know.** `fn_triggeraction` in
`server/src/scripting/gs1/GS1Commands.cpp` reads its four arguments as `x`, `y`,
`action` and `params`. Both `action` and `params` are read from
`arguments[2]`. Nothing ever reads `arguments[3]`.

Worth checking while you are there: the function insists on exactly four
arguments. Find out how the GS1 parser groups `ping,p1,p2` (one argument, or
three) before deciding what the fourth argument should hold. The `clientside`
branch (`action == "clientside"`) goes through the same two variables.

**Where to start.** `fn_triggeraction` (around line 3677 of `GS1Commands.cpp`)
and `Server::sendTriggerAction`. `Catch_tests/NPCServerGS1` is the place for a
test that runs the NPC and checks the outgoing text.

---

### 2. A GS2 weapon with a string literal never compiles

Issue: [#110](https://github.com/squirelworks/seihai-gserver-v2/issues/110).
Area: script compiling, weapons.

**What you see.** On a `modern` world, a weapon whose client half is GS2 and
contains a string literal never reaches 5.24r4 or 6.03r7. This one fails:

```
//#CLIENTSIDE
function onCreated() { echo("labchat"); }
```

The server log shows the GS1 lexer giving up:

```
GS1 lexing error ... token recognition error at: '"'
```

The client then gets only the weapon's name and image (`PLO_NPCWEAPONADD`, 33)
and no script (no 197 or 140). Change the body to `this.labchat = 1;`, which
has no string literal, and the weapon goes out normally as 33, 197, then 140.

**To reproduce.** Add a default-account weapon with the script above to the
`modern` world, log in with a 5.24r4 or 6.x client and watch the packet log and
server log.

**What we know.** The server tries the GS1 compiler first and the GS2 compiler
only as a fallback, in `ScriptSystem::getCompiledClientScript`
(`server/src/scripting/ScriptSystem.cpp`). That function returns early, without
trying GS2, when the first attempt fails with
`ScriptCompilationFailureType::REJECTED`.

`ScriptEngineGS1::compileScript` (`server/src/scripting/gs1/ScriptEngineGS1.cpp`)
returns exactly `REJECTED` when a client half fails to translate from GS1, on
purpose. Its comment says the GS2 engine "must not then read the GS1 text as
GS2", because broken GS1 can sometimes parse as valid but wrong GS2. So the two
goals pull against each other: GS1 that is merely broken should not run as GS2,
but GS2 that GS1 cannot even lex should. Any fix has to tell those two cases
apart, or rely on the `//#GS2` marker (see bug 3).

The same lab run noticed that a `newmain` world sends no GS2 client bytecode at
all. That may be intended (the shipped `serveroptions.txt` says "GS2 is only
usable on modern"), but keep it in mind when testing.

**Where to start.** `ScriptSystem::getCompiledClientScript` and
`ScriptEngineGS1::compileScript`. Test bugs 2 and 3 together:
each may hide the other.

---

### 3. `//#GS2` on a weapon's client half is ignored; 4.x and 5.x get text they cannot run

Issue: [#113](https://github.com/squirelworks/seihai-gserver-v2/issues/113).
Area: script compiling and which clients get bytecode.

**What you see.**

- **Weapons.** A `//#GS2` marker placed after `//#CLIENTSIDE` is ignored. 4.x
  and 5.x clients are sent the GS2 source as text. They cannot run GS2 text, so
  the weapon does nothing.
- **Level NPCs.** Bytecode only goes to clients newer than 5.07. Earlier builds
  that can read GS2 bytecode (4.x on) get text instead.

**To reproduce.** On a world that admits 4.20r8 or 5.12 (see bug 5), add a
weapon such as:

```
//#CLIENTSIDE
//#GS2
function onCreated() { this.x = 1; }
```

Log in with 4.20r8 or 5.12 and check in the packet log whether the weapon came
as bytecode (140 / 197) or as text inside `PLO_NPCWEAPONADD`. Seen in the
2026-11-19 lab (squirelworks/seihai-core#112).

**What we know.** Several pieces decide this, and they disagree:

- `ScriptSystem::getCompiledClientScript` picks an engine only when the
  *trimmed whole script* starts with `//#`. On a weapon the `//#GS2` line comes
  after `//#CLIENTSIDE`, so it may never be seen there.
- `gs1::compileClientScript` (`server/src/scripting/gs1/GS1ToGS2.cpp`) does
  check for `//#GS2` at the start of the client half, but it is only reached
  through `Weapon::getClientByteCode` when the main compile produced nothing.
- `Weapon::registerWeaponWithPlayer` (`server/src/object/Weapon.cpp`) sends text
  whenever there is no compiled bytecode and
  `scriptPackets::usesModernScriptCache(version)` is false. That is true only
  from 6.00 beta 1 (`server/include/misc/ScriptPacketGuards.h`), so 4.x and 5.x
  get text whenever the main compile failed or was skipped.
- `NPC::sendsGS1ByteCodeTo` (`server/src/object/NPC.cpp`) sends bytecode only to
  `version > CLVER_5_07` outside a modern world.

So the version cut-offs are 6.00 for weapons and 5.07 for level NPCs, while the
clients that read GS2 bytecode start at 4.x. Which 4.x build is the true first
one should be checked against the client notes in Graal_analysis before you
pick a cut-off.

**Where to start.** The four functions above.
`Catch_tests/ScriptPackets` and `Catch_tests/GS1ToGS2` already cover parts of
this path.

---

### 4. A server name starting with "3 " makes 5.12 think the server is 3D

Issue: [#114](https://github.com/squirelworks/seihai-gserver-v2/issues/114).
Area: server warps and server names.

**What you see.** A login or lobby server warps the player on with
`serverwarp <name>`. If the target server's name starts with `3 ` (a "3",
then a space), the 5.12 client treats that server as a Graal 3D server, and
drops the connection after about 15 seconds.

**To reproduce.** Run a lobby world whose level NPC does
`serverwarp 3 Test;` (or list a server named `3 Test` and pick it), log in
with 5.12, and time how long the client stays connected after the warp.
Expected: it stays past 30 seconds. Seen in the 2026-11-19 lab
(squirelworks/seihai-core#112).

**What we know.** The likely reason is that Graal server lists use a short
prefix on the name to mark the server's type, and `3 ` is the 3D prefix. That
is our reading, not something we have traced in the client. On the server side,
a warp goes out to the listserver as `SVO_SERVERINFO` (sent from
`GS1Commands.cpp` and `PlayerClientPackets.cpp`), and the listserver's answer is
forwarded to the client unchanged as `PLO_SERVERWARP` in
`ServerList::msgSVI_SERVERINFO` (`server/src/ServerList.cpp`). Nothing on that
path looks at or changes the name.

Open questions worth answering: which other prefixes the client reacts to, and
which client versions do it (we only saw 5.12). The fix could live in the
gserver, the listserver, or the world's own configuration, so it is worth
talking it through on the issue first.

**Where to start.** `ServerList::msgSVI_SERVERINFO` and wherever the server
list entries are built.

---

### 5. The shipped worlds turn away 4.x and 5.x clients on newmain

Issue: [#115](https://github.com/squirelworks/seihai-gserver-v2/issues/115).
Area: world configuration.

**What you see.** The `npcdemo` sample world refuses 4.x and 5.x clients. So do
`default` and `classic` if you switch them to `generation = newmain`.

**To reproduce.** Start `npcdemo` and log in with any 4.x build or 5.x build
before 5.12. The login is refused, and the "Allowed:" message lists only
2.00 to 3.153.

**What we know.** This is configuration, not code. `Server::loadAllowedVersions`
(`server/src/Server.cpp`) reads only the line of `config/allowedversions.txt`
that matches the world's generation. In the files the fork ships:

| World | `newmain` line | `modern` line |
|---|---|---|
| `classic`, `default`, `npcdemo` | 2.00 to 2.31, 3.04 to 3.153 | 5.12 to 6.037 |
| `modern` | same | 4.04r2 to 6.037 |

So on `npcdemo` (newmain) no 4.x or 5.x token is listed, and even on a `modern`
world the default range in three of the four worlds starts at 5.12, leaving 4.x
to 5.07 out of every range. Only the `modern` world admits them.

`serveroptions.txt` says newmain is for 2.x/3.x and modern for 4.x+, so it is
a fair question whether 4.x/5.x on newmain is supposed to work at all. The open
decision is whether the sample worlds should widen their ranges, or keep them
and say so in a comment.

**Where to start.** `bin/servers/*/config/allowedversions.txt` and
`serveroptions.txt`. If you widen a range, also check bugs 3 and 8: both get
easier to hit once more client versions are let in.

---

### 6. Classic generation runs no server-side level NPC scripts

Issue: [#106](https://github.com/squirelworks/seihai-gserver-v2/issues/106),
part 1. Area: NPC-server, classic generation.

**What you see.** The same world that fires `created`, `timeout` and
`playerenters` on server-side level NPCs under `generation = newmain` fires none
of them under `generation = classic`. The original 1.x-era servers did run
NPC-server scripts, so classic should too.

**To reproduce.** Copy `npcdemo`, set `generation = classic` and add 1.39 or
1.41r1 to the `classic` line of `allowedversions.txt`. Log in and see that the
sample NPCs' server-side events never run. Seen on 2026-11-19 at #104's head
`c268d57`.

**What we know.** Not traced yet. The generation checks in
`server/src/scripting/gs1/ScriptEngineGS1.cpp`, `GS1Commands.cpp` and
`GS1Flags.cpp` are the first places to look. Search for `ServerGeneration::` and
`Generation ==` in `server/src/scripting/` and `server/src/npcserver/`.

---

### 7. Bare `shootball` aims the wrong way after the first shot, and 1.39 draws no ball

Issue: [#106](https://github.com/squirelworks/seihai-gserver-v2/issues/106),
part 2. Area: GS1 commands.

**What you see.** A level NPC runs a bare `shootball` on a 3-second timeout on
a newmain world. The server sends `PLO_ARROWADD` (19). Only the first shot goes
toward the player (direction 3). Every later shot uses the NPC's own direction
(2). Separately, a 1.39 client drew no ball at all over two shots.

**What we know.** This fork's `shootball` (see the 2026-11-18 row of
`CHANGES-SEIHAI.md`) aims at the player and falls back to the NPC's direction
only when there is no player. So the player lookup appears to fail after the
first shot. For 1.39, either 19 is not the packet 1.39 draws for a thrown ball,
or 1.39 is missing the image. Answering that needs a capture of what a 1.x-era
server sent for the same script.

**Where to start.** The `shootball` command in `GS1Commands.cpp` and how it
finds "the player" (compare it with how `fn_triggeraction` uses
`findNearestScriptObjectSourceFromStack`; on a timeout there may be no player
on the script's stack at all).

---

### 8. Classic generation: a 2.x client's prop 10 is read as a 1.x bow, and the client is kicked

Issue: [#107](https://github.com/squirelworks/seihai-gserver-v2/issues/107).
Area: player props, classic generation.

**What you see.** Only when a 2.x client is let onto a classic world (it is not
by default; you have to add its token to the `classic` line of
`allowedversions.txt`):

- **Incoming.** When 2.22 starts walking it sends player prop 10 as a gani name:
  length 4, then `walk`. A classic server reads prop 10 in the 1.x bow form, so
  the length byte becomes "bow power 4, no image", and the bytes of `walk` are
  then read as prop ids. `w` is prop 87, which is past the last prop, and the
  client is disconnected with "Invalid PlayerProp type in constructPropFor".
- **Outgoing.** 2.22 is also sent prop 10 in the bow form (`bow1.png`), for
  itself and for others. 2.22 then draws no players at all. A 1.39 on the same
  server reads the same data correctly.

**To reproduce.** Start `classic`, add `GNW03014` (2.22) to its `classic` range,
log in with 2.22 next to a 1.39, and walk. Seen on 2026-11-19 at #104's head.

**What we know.** `PropertyGaniOrBowGif` in
`server/src/utilities/PropertySerializers.cpp` chooses the form from the server's
generation, not from the client's version. 1.33 to 1.41r1 use the bow form; 2.x
and later use the gani form. Mixed 1.x and 2.x worlds therefore cannot work
until the form follows each client. The other option discussed on the issue is
to refuse 2.x on classic at login with a clear message, instead of a kick in the
middle of a session.

**Where to start.** `PropertyGaniOrBowGif::serialize` / `deserialize`, and where
props are built per receiving player in `server/src/player/`.

---

### 9. An unknown prop id in a client's props packet ends the session

Issue: [#107](https://github.com/squirelworks/seihai-gserver-v2/issues/107).
Area: player props.

**What you see.** If a client's `PLI_PLAYERPROPS` contains a prop id the server
does not know, `Player::constructPropFor` (`server/src/player/PlayerProps.cpp`)
throws `std::invalid_argument`, and the client is disconnected. Bug 8 is one
way to reach it, but any client that sends a newer prop id would hit it too.

**What we know.** Probing a real 6.03r7 (seihai-gserver#161) suggests real
clients skip prop ids they do not know instead of giving up. The hard part is
that a props packet has no per-prop length, so after an unknown id the reader
cannot always know how many bytes to skip. Any change here needs to decide what
happens to the rest of the packet.

**Where to start.** `Player::constructPropFor` and `Player::getProp` in
`PlayerProps.cpp`, and the loop that reads `PLI_PLAYERPROPS`.

## Open areas, not single bugs

These are tracked work where outside help is welcome. They are not one bug each.

- **Crash hunting** ([#87](https://github.com/squirelworks/seihai-gserver-v2/issues/87)).
  There is a sanitizer CI job and a weekly libFuzzer target over
  `processBuffer` (see `fuzz/README.md`), but the fuzzer still uses a stub
  handler. Wanted: a harness that drives the real packet handlers, and a replay
  corpus built from loopback lab sessions. Every crash found should come with a
  regression test.
- **Graal 3D control object** ([#105](https://github.com/squirelworks/seihai-gserver-v2/issues/105)).
  Protocol 19 clients (3.153, 4.04r2) used to get only a stock TGE `Camera` as
  their control object, and real 3.151 Linux and Mac 3.153 crashed right after
  it. #111 now ghosts a `Sky` and a walking `Player` control object, but no real
  3.15x client has been run against it yet. That lab run is the open step.
- **Short client packets.** #103 was fixed in #104, but only for the ids the
  2026-11-18 sweep tested. Any handler that reads a fixed number of bytes from
  a client packet without checking its length is worth a look.

## Recently fixed

Kept here for a while so people running older builds know what they are hitting.
Each is in `CHANGES-SEIHAI.md` with the details.

| Bug | Fixed in | Symptom on older builds |
|---|---|---|
| GR-V1.05 levels load four bytes late (#116) | #118, 2026-11-19 | `File::readPackedIntegral` had its size guards inverted, so `<1>` read five bytes. Every board of a GR-V1.05 level started four bytes late and links, NPCs and signs after it were garbage. Also hit every length-prefixed string read from a file. |
| A malformed level ends the server (#117) | #118, 2026-11-19 | A link to a name the OS cannot check (longer than NAME_MAX) threw `std::filesystem::filesystem_error` inside a `noexcept` lookup, which calls `std::terminate`. Short NPC, chest and sign lines read past their end. |
| Short client packets (#103) | #104, 2026-11-18 | A short `PLI_BADDYADD` made a baddy of type 224 that crashed 6.03r7; empty `PLI_REQUESTUPDATEBOARD` and `PLI_SERVERWARP` were acted on. |
