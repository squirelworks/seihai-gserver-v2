# Real-client lab runbooks

One page per open lab item, so a lab run is "set up, play the steps, run the
checker, paste the table". Each page lists what the run needs, the exact
steps, what the checker should print, and what to write down by hand (what the
checker cannot see: what is on screen).

| Item | Page | Checker |
|---|---|---|
| #25 ping (108/162) and #26 weapon cache (193/158) | [25-26-ping-and-weapon-cache.md](25-26-ping-and-weapon-cache.md) | `labcheck.py ping`, `labcheck.py weaponcache` |
| #27 five clients in one level | [27-five-clients-one-level.md](27-five-clients-one-level.md) | `labcheck.py milestone` |
| #34 post-merge client matrix | [34-post-merge-matrix.md](34-post-merge-matrix.md) | `labcheck.py matrix` |

## Common setup

1. **A packet-logging server.** Build this fork with packet logging, so
   `logs/networkdump.txt` records every message with its bytes:
   ```
   cmake --preset "Linux Debug (Packet Logging)" -B build-pl     # or "Windows Debug (Packet Logging)"
   cmake --build build-pl --target gs2emu
   ```
   (Any build with `-DPACKET_LOGGING=ON` does.) Use it only for lab runs: it
   logs passwords in the clear for classic logins.
2. **A clean server folder.** Copy the world folder you need (see
   [lab-world.md](../../../bin/docs/lab-world.md)) and empty its `logs/`, or
   pass `--since "YYYY-MM-DD HH:MM:SS"` (server time) to the checker.
3. **Save the console.** Start the server with its standard output saved:
   `./gs2emu --server modern | tee servers/modern/console.log`. "Unknown
   Player Packet" lines only go there.
4. **One account per client**, named after the client (`lab420`, `lab603`, ...),
   so the report reads by itself. Lab worlds run with `localhost = true`, so
   any password works.
5. **Afterwards**, run the checker from the repo root:
   `python3 tools/lab/labcheck.py ITEM path/to/servers/<name>`, and paste its
   table and your screen notes into a dated note in `docs/lab/`. Exit code 0
   means every PASS/FAIL row passed. INFO rows record facts.

The checker's own tests: `python3 -m unittest tools/lab/test_labcheck.py`.
`tools/lab/fixtures/modern-3clients` is a real capture with scripted clients
to compare with.
