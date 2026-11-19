<!-- Seihai project, 2026-11-19: see CHANGES-SEIHAI.md. -->
# Known issues in Seihai GServer-v2

This is a plain-language list of the bugs we know about in this fork, written
for anyone who wants to investigate or fix one. Each entry says what goes wrong,
how to see it for yourself, what we already know about the cause, and where in
the code to start. Most entries stop short of a fix on purpose: we would rather
you look at the evidence and decide.

Last checked against branch `seihai` at the #121 merge (2026-11-19). The
tracking issue for each bug is linked; comment there if you find something, or
open a pull request against `seihai`.

Bugs in the support library underneath this server (sockets, packet ciphers,
compression, client version tokens) are listed separately in
[seihai-gs2lib's KNOWN-ISSUES.md](https://github.com/squirelworks/seihai-gs2lib/blob/seihai/KNOWN-ISSUES.md).
Bugs in stock upstream GServer-v2 `beta4` are a separate question: several
below were found here and are still in upstream.

## Contents

- [Before you start](#before-you-start)
- [Open bugs](#open-bugs)
  1. [Bare `shootball` aims the wrong way after the first shot, and 1.39 draws no ball](#1-bare-shootball-aims-the-wrong-way-after-the-first-shot-and-139-draws-no-ball) (#106)
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

### 1. Bare `shootball` aims the wrong way after the first shot, and 1.39 draws no ball

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
The #120 fixes passed a real-client lab on 2026-11-19; the #121 ones have
Catch2 tests, with a real-client recheck to follow.
Each is in `CHANGES-SEIHAI.md` with the details.

| Bug | Fixed in | Symptom on older builds |
|---|---|---|
| GS1 `triggeraction` sent the action name as its params ([#112](https://github.com/squirelworks/seihai-gserver-v2/issues/112)) | #120 | `fn_triggeraction` read `action` and `params` from the same argument, so `ping,p1,p2` went out as `ping,ping`. |
| A GS2 weapon with a string literal never compiled ([#110](https://github.com/squirelworks/seihai-gserver-v2/issues/110)) | #120 | The GS1 lexer rejected the `"` and that also stopped the GS2 compile; 5.24r4 and 6.03r7 got only PLO 33. |
| `//#GS2` after `//#CLIENTSIDE` was ignored ([#113](https://github.com/squirelworks/seihai-gserver-v2/issues/113)) | #120 | 4.x and 5.x got GS2 source as text and ran nothing. |
| A server name starting with `3 ` made 5.12 go 3D ([#114](https://github.com/squirelworks/seihai-gserver-v2/issues/114)) | #120 | The listserver's `PLO_SERVERWARP` was forwarded unchanged; 5.12 dropped after about 15 s. The gserver now strips a leading `3 `. A lobby that writes 178 itself must do the same. |
| npcdemo refused 4.x and 5.x ([#115](https://github.com/squirelworks/seihai-gserver-v2/issues/115)) | #120 | Its `newmain` line stopped at 3.153. The `classic` and `default` worlds still start their `modern` line at 5.12. |
| Classic generation ran no server-side level NPC scripts ([#106](https://github.com/squirelworks/seihai-gserver-v2/issues/106), part 1) | #120 | `created`, `timeout` and `playerenters` never fired on a classic world. |
| A 2.x client on a classic world was kicked when it walked ([#107](https://github.com/squirelworks/seihai-gserver-v2/issues/107)) | #120 | Prop 10 followed the server's generation, so a 2.x gani was read as a 1.x bow and its bytes as prop ids. An unknown prop id ended the session; now it ends only that packet. |
| 5.12 ignored GS1 weapons | #121 | 5.08 to 5.12 don't run GS1 text, but GS1 weapons still went to them as text. |
| `setcharprop #c,Welcome #n!` greeted the NPC | #121 | The NPC context pushed for the message code also covered the text, so `#n` was the NPC's own name. |
| 2.x drew players head-only on a classic world | #121 | Classic never sends the body (prop 35) and its accounts name `head0.gif`. |
| GR-V1.05 levels load four bytes late (#116) | #118, 2026-11-19 | `File::readPackedIntegral` had its size guards inverted, so `<1>` read five bytes. Every board of a GR-V1.05 level started four bytes late and links, NPCs and signs after it were garbage. Also hit every length-prefixed string read from a file. |
| A malformed level ends the server (#117) | #118, 2026-11-19 | A link to a name the OS cannot check (longer than NAME_MAX) threw `std::filesystem::filesystem_error` inside a `noexcept` lookup, which calls `std::terminate`. Short NPC, chest and sign lines read past their end. |
| Short client packets (#103) | #104, 2026-11-18 | A short `PLI_BADDYADD` made a baddy of type 224 that crashed 6.03r7; empty `PLI_REQUESTUPDATEBOARD` and `PLI_SERVERWARP` were acted on. |
