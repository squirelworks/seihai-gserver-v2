# seihai-gserver-v2 roadmap

The live checklist is issue #35; the suite-wide plan is squirelworks/seihai#353.
This page groups that work into three phases. Last checked 2026-11-16 against
branch `seihai`.

This fork is the server real 1.33 to 6.x clients use in the lab today, and the
reference the Rust seihai-gserver is diffed against. It is never a code source
for the clean-room repos.

## Phase 1: to functional (current priority: bugs and crashes)

- [x] Merge proven on real clients (#34), built-in NPC-server (#33), 5.x/6.x (#23, #24), lab milestone (#27).
- [ ] Crash hunting: sanitizer CI job, packet replay harness and a fuzz target (#87), together with squirelworks/seihai-gs2lib#27. Standalone `fuzz_process_buffer` + weekly smoke are in; full-server ASan CI, real-handler replay, and hour runs remain.
- [ ] Every crash or hang seen in a lab run gets a regression case in the replay corpus and a `CHANGES-SEIHAI.md` row.
- [ ] A real 6.1.1.3 client logging in and playing on the `modern` server (target build for squirelworks/graal-client-repro#373).
- [ ] 3.153 real-client run if a Windows 3.x client turns up.

## Phase 2: polish and release

- [x] Release `v4.0.0-seihai.1` from the draft (notes from #32); published 2026-11-11, with `v4.0.0-seihai.2` and `v4.0.0-seihai.3` following.
- [ ] macOS build at release only, through `seihai-release.yml`, with the pinned gs2lib built for macOS (squirelworks/seihai-gs2lib#28).
- [ ] Keep up with upstream `beta4`, keeping every Seihai row. Last merge: `2de276b` (#55).
- [x] Offer the crash fixes upstream if the owner agrees (squirelworks/seihai-gs2lib#10).
- [ ] Clear `docs/tobedone.txt` into issues or remove what is done.

## Phase 3: after it works

- **Retire to reference** (squirelworks/seihai#353 phase 7): once the Rust gserver reaches parity, this fork takes bug fixes only and runs as the "known good" side of differential lab runs.
- **Reference lab image**: a container or VM recipe that starts this fork with the `classic`, `default`, `modern` and `npcdemo` folders, so anyone can reproduce a lab run.
- **Behaviour oracle**: scripted sessions recorded against this fork become the expected frames for seihai's `tools/difftest` and the Rust gserver's tests.
