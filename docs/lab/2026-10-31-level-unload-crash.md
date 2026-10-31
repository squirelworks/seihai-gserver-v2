# Level unload crash, 2026-10-31

## The bug

When the 5-minute maintenance tick unloads a level that nobody has been in for
`unloadinactiveleveltime` seconds, the level is destroyed. `Level::~Level`
removed its ground items with `removeItem(inform_client, ...)`, which builds an
item-delete packet and passes `shared_from_this()` to the send call. Inside a
destructor there is no owning shared pointer left, so `shared_from_this()`
throws `bad_weak_ptr`. Destructors are `noexcept`, so the server terminates.

It only happens if the level still has items at that moment. Items time out after
8.2 s, so it needs something that keeps dropping items while players are on
the server but not in that level: an NPC that lays items on a timer, or baddy drops.
Found during the classic-level sweep in `2026-10-31-npcserver-gs1.md`.

## Fix

`Level::~Level` clears `m_items` without telling clients. A level is only
unloaded after no player has been in it for `unloadinactiveleveltime`, so there
is no one to tell.

## Lab run (loopback, Linux release build)

World: a lab copy of `bin/servers/npcdemo` (not committed) on port 14806 with
`unloadinactiveleveltime = 1` and one extra NPC in `npcdemo_room.nw`:

```
NPC - 20 20
if (created || timeout) { lay greenrupee; timeout = 2; }
NPCEND
```

Client: `tools/lab/npcprobe` as 2.31, steps:

```
wait 2
warp npcdemo_room.nw 30 40
wait 3
warp npcdemo_start.nw 30 40
wait 400
```

The player stays online in the start level, so the room's NPC keeps laying
items, and the room is unloaded at the first maintenance tick (5 minutes after
start).

| Build | Result |
|---|---|
| `seihai` before the fix (`083306a`) | at the 5-minute tick: `Unhandled exception occurred: bad_weak_ptr`, stack through `Level::removeItem(inform_client_t, ...)` from the level's destructor; the server shut down and the probe got `closed: unexpected end of file` |
| this branch | at the 5-minute tick the room was destroyed with 5 items on the ground (checked with a temporary log line, not committed); the server kept running and the probe stayed connected |

## Related, not fixed here

The function above the destructor that clears one sub-level's objects (it handles
bombs, explosions, horses, then items) reads `m_items[i]` in a loop that runs
`i` from `m_items.size()` down to 1, so the first read is one past the end. It
should read `m_items[i - 1]`. No lab case triggers it yet; fix it with a test.
