<!-- Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md. -->
# seihai-gserver-v2 roadmap

The live checklist is issue #35; the suite-wide plan is squirelworks/seihai#353.
This page groups that work into three phases. Reconciled 2026-11-22 against
`seihai` at `bbeec424`.

This fork is the server real 1.33 to 6.x clients use in the lab today, and the
reference the Rust seihai-gserver is diffed against. It is never a code source
for the clean-room repos.

## Phase 1: to functional (current priority: bugs and crashes)

- [x] Merge proven on real clients (#34), built-in NPC-server (#33), 5.x/6.x (#23, #24), lab milestone (#27).
- [x] Crash-hunting tooling issue #87 was closed as completed on 2026-11-20. Sanitizer CI, replay and scripted-session checks remain in the existing workflow. This status reconciliation performs no new run and makes no new hour-run or real-client claim.
- [ ] Every crash or hang seen in a lab run gets a regression case in the replay corpus and a `CHANGES-SEIHAI.md` row.
- [ ] A real 6.1.1.3 client logging in and playing on the `modern` server (target build for squirelworks/graal-client-repro#373).
- [ ] 3.153 real-client run if a Windows 3.x client turns up.

## Phase 2: polish and release

- [x] Release `v4.0.0-seihai.1` from the draft (notes from #32); published 2026-11-11, with `v4.0.0-seihai.2` through `.5` following.
- [x] macOS build at release only, through `seihai-release.yml`, with the pinned gs2lib built for macOS and its unit tests run there (squirelworks/seihai-gs2lib#28).
- [ ] Keep up with upstream `beta4`, keeping every Seihai row. Current documented upstream base: `bc54973` (`CHANGES-SEIHAI.md`).
- [x] Offer the crash fixes upstream if the owner agrees (squirelworks/seihai-gs2lib#10).
- [ ] Clear `docs/tobedone.txt` into issues or remove what is done.

## Phase 3: after it works

- **Retire to reference** (squirelworks/seihai#353 phase 7): once the Rust gserver reaches parity, this fork takes bug fixes only and runs as the "known good" side of differential lab runs.
- **Reference lab image**: a container or VM recipe that starts this fork with the `classic`, `default`, `modern` and `npcdemo` folders, so anyone can reproduce a lab run.
- **Behaviour oracle**: scripted sessions recorded against this fork become the expected frames for seihai's `tools/difftest` and the Rust gserver's tests.

## Merged correctness fixes (2026-11-22)

- [#142](https://github.com/squirelworks/seihai-gserver-v2/pull/142): HEART rollback updates health instead of bombs; helper-unit
  coverage retains health-cap/AP policy. Normal pickup and packet-path
  rollback/wire behavior are not proved by that fixture.
- [#144](https://github.com/squirelworks/seihai-gserver-v2/pull/144): level saves report output open/stream failure. The regression
  executes open obstruction; later write/close handling is source-reviewed,
  without atomicity/fsync/durability claims.
- [#145](https://github.com/squirelworks/seihai-gserver-v2/pull/145): a single filesystem path binds as one directory rather than
  as a component range; genuine path ranges remain supported.
- [#146](https://github.com/squirelworks/seihai-gserver-v2/pull/146): scalar duplicate settings consistently use the last entry,
  including the final key group, with typed/cache consumers covered.

The PRs retain observed baselines and successful Linux regression/sanitizer
evidence. Windows build/package/smoke passed; those release jobs do not
execute these unit tests. macOS compilation/runtime was not run for these
fixes. They do not constitute a new release, retirement or whole-world
parity. #143 remains unresolved and parked.
