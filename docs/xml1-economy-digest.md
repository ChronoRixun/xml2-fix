# Economy observation and scripted shops

Enable `[Test] InputPipe=1` with a different `PipeName` on each peer. With
`Xml1Economy=1`, successful native shop/stash opens and every routed action-bit-4
attempt produce one log line after the handler finishes when the snapshot is complete:

```text
econ seq=1 player=0 gate=0 tab=0 list=buy row=3:definition_key owner=-1 result=buy money=100 defs=0123456789abcdef stock=0123456789abcdef rng=42
```

`economy events [after]` returns a **single JSON reply line** containing `first`,
`last`, `lost`, and the retained `lines` after the exclusive cursor. History holds
256 events. A cursor older than the retained range reports `lost:true`; never
accept that as complete evidence. Full lines also go to `xml2-fix.log`.
`economy shop` returns a copied `active`/`sample` view with vendor, tab, focused
list, selected global row ID, error and visible rows in display order. Each row
has its global ID, type, definition key, normalized owner, detail and listed
price. A view older than one second is inactive. The pipe thread never follows
game pointers. `economy status` and `economy rolls` remain available.

The observer uses guarded native boundaries:

| Address | Observation |
| --- | --- |
| `0x5d39c0` | Successful open, after native initialization; preserves `ret 8` |
| `0x5d3ba0` | Entire update, including early returns; preserves `ret` |
| `0x5d3c31` | Actual menu-state gate result, before its early-out |
| `0x5d3c81`, `0x5d3c97` | Actual routed input and out-player from `0x5d4b20` / `0x5d4a00` |
| `0x5d3dd7` | Selected global row and owner, before equipped removal |
| `0x5d4135` | Common completion before native UI rebuild |
| `0x5d3fda` | Empty non-equipment sale refusal |

`player` is the native out-player index. `gate` is the native boolean, where 1
blocks input. Opens have `player=-1 gate=-1`: no input gate ran during opening.
A blocked update cannot invent a transaction event; a driver waiting for that
action consequently fails for a missing event. Idle updates do not advance seq.
`tab`, `list`, `row`, and `owner` describe the **pre-mutation action**, even if a
bag changes the selected tab afterward. Null owner is -1; an equipped owner is
the matching native roster index (registry slots +0x48/+0x4c/+0x74), captured
before removal. Unresolvable identities skip the observation; they never become
wire pointers or partly valid events. The copied shop view reports the failure.
`list` is a canonical tab identity: `buy`, `sell`, `training`, or stash
`withdraw`/`deposit`. It is never derived from a process address.

Results are `open`, `buy`, `sell`, `deposit`, `withdraw`, `refused`,
`refused-empty`, `refused-training`, `bag-equipment`, `bag-course`, `bag-empty`,
or `bag-capacity-refused`. Buy/sell/deposit/withdraw mean the native mutation
block reached common completion. Bag results are supplied by the actual reward
replacement inside that same outer transaction; they do not add a second event
or charge. `bag-course` records script submission, not proof that the queued
notification/unlock has already run. G9's course-unlock check still applies.

Hashes are FNV-1a/64, lowercase 16 hex digits, over explicit little-endian u32
words and length-prefixed strings. They contain no struct padding, native
addresses, allocation handles, or pool pointers:

- `defs`: domain `defs-v1`, count, every native definition in index order:
  index, full interned key, native quantity (+0x40 getter), obtained (+0x54).
- `stock`: base domain `stock-v1`; vendor instances in state 3/4 sorted by
  definition/state, then all current native global rows in array order. A row
  contributes type, definition index/key, normalized owner, detail, quantity,
  instance state, and listed price (sale price on the sell tab, zero in stash).
  Course/Training details use only the native byte, excluding stack padding.
  The final `shop-v1` envelope contains vendor, focused list, the base hash's
  low/high words, visible count and the **entire** native visible-to-global row
  mapping. Thus differing hidden rows, visible count, or sorting are detectable.
- `rng`: the **native lockstep draw counter** at `0x7ad8b4`, also returned by
  `0x560180`. Observation never draws. This counter can reset when native game
  code reseeds; it is compared as recorded, not replaced with a DLL counter.

Descriptor, name, row-count, visible-map and stock-cycle failures skip the
frame without advancing the event sequence or publishing partial rows/hashes.
Null shop, null focused widget and failed native probes have the same policy.
Every diagnostic native-memory path (capture and per-action annotations) has
SEH and C++ exception containment. The diagnostic translation unit uses `/EHa`
so strings/vectors unwind even when a native probe faults. Original native calls remain outside that
boundary and retain their arguments, return values and exception behavior.

The first-open crash was a type error, not unfinished shop initialization:
`shop+0x324` is a focused `CUIObject*`, assigned at `0x5aafa1` and dereferenced
at `0x5aafda`. The native shop passes it to `dynamic_cast` at `0x5d3da3`.
The old digest instead passed it to `name(unsigned)`, indexing an intern table
with a pointer's low 24 bits. Its test used the integer 1 instead of a widget.
The corrected observer uses the pointer only for the native list cast.
Capture used to resolve that list before reading currency or definition hashes,
so a fault there left the default `money=0 defs=0000000000000000`. Those partial
values did not establish that the native balance or definition table was empty.
Definition names still resolve through the real table: index < 8192 hash slots,
nonempty string and offset within the used arena (maximum 0x14400 bytes), and a terminating
NUL within that bound and 128 bytes. The word at table+0 is a generation, not a
dense string count (`0x41a320`); comparing IDs with that word would reject valid
hashed names. A hash collision is possible; this is a diagnostic gate,
not a cryptographic proof. Preserve full logs and seeded inputs for review.

All eight guards contribute to the same W2/economy startup transaction and any
failure leaves rule 52 unverified and host/join closed. OFF contributes no
observer hooks. Hashes/history are collected only with the test pipe enabled.
The gameplay rule remains revision 1, parts **127** for plan A / **95** for the
fallback. Status is now explicit even with no tripwire hits:
`econ on; plan A; defs N; digest PREFIX; forge n/Gg; gen 0; refused n`.
Forge count/group are the most recent reconcile (0/G0 before the first one);
generator totals additionally list individual callers when nonzero.

## Driver

`tools/economy_driver.py` talks directly to the two already-running pipes. It
does not launch processes, drive the desktop, edit memory, force RNG, mirror
transactions or repair synchronization. Only `--drive host|client` gets input.
Opens use the existing script queue; tabs, rows and transactions use normal
keyboard or virtual-pad pipe input through the game's normal input route.
If a one-sided scripted opener does not reach the passive peer, **stop**: the
missing open is a failed observation, not permission to open it on both peers.

```powershell
python -B tools/economy_driver.py --host-pipe loot-host --client-pipe loot-client --drive host --plan plan.json --out evidence/host
```

A JSON plan is an array, for example:

```json
[
  {"op":"open","vendor":"forge"},
  {"op":"buy","definition":"lc_x1_black_toms_cane","owner":-1},
  {"op":"sell","definition":"lc_x1_black_toms_cane","owner":-1},
  {"op":"close"},
  {"op":"open","vendor":"healer"},
  {"op":"bag","expect":"bag-equipment"}
]
```

Operations: open (forge/healer/stash), close, tab (buy/0 or sell/1), row, buy,
sell, bag, deposit and withdraw. A row selector uses `definition`, optional
`owner`, and/or explicit global `row`; ambiguous or missing matches fail.
Bag automatically selects the native type-1 row. `expect` asserts an exact
result; omitted for bags it accepts equipment/course/empty but never capacity
failure. Other transactions default to their own result name.

Default controls are RIGHT for next tab, UP/DOWN for row, RETURN for accept,
ESCAPE for back. Override `--next-tab`, `--accept`, `--back` for the installed
bindings, or use `--pad 1..4` with virtual pads (A/B accept/back). Use
`--expect-player N` to require the known native controller index for the driving
peer. Row selection scans native visible ordering rather than assuming global
indices are adjacent.

After each transaction, the driver requires exactly one event on each peer,
compares **every field**, and checks tab/list/row/definition/owner against the
intended selection, plus result and optional player. Equal unintended actions
cannot pass. Missing events, gaps, overwritten history, errors, repeats or a
differing field stop the run immediately. It writes requests/replies to
`requests.jsonl`, pairs to `equal-events.jsonl`, and `result.json` or
`failure.json` with both retained histories. A PASS covers only the supplied
plan: it does not certify the ten-minute fight or coverage of all required rows.

## Code-only verification

Build Win32 Release with CMake. `scripts/run_native.py` includes digest rules,
the mapped-retail digest mode, eight new guard mutations and preparation veto.
Run `python -B test/economy_driver_test.py` for driver/comparator regressions.
Run `python -B scripts/check_xml1_economy_digest_text.py <owned-retail-exe>` for
the guard/ABI/direct-entry census. Startup additionally checks all eight hook
states. Run the existing W2 absent/zero OFF comparisons and content guard.
These gates use the retail image read-only and never launch the game.

G9-online remains a **live** acceptance gate. Execute at least 20 transactions
per vendor from each driving peer, including unique buy/sell, equipped sale,
stash deposit and bag outcomes; preserve exact matching lines for every seq.
Roll/pickup lines use
`roll n=<serial> cat=.. lead=.. G=.. bucket=.. def=.. amt=.. draws=..`.
The unsigned 64-bit decimal serial starts at1 and advances once per enabled
roller entry at `0x46e5e2`; every pickup of that roll repeats it. The selection
bridge `0x46e760` and read-only `economy rolls` queries do not advance it.
Separate identical money rolls therefore remain separate when grouped by `n`.
The pipe returns the same serial-bearing lines as the log. See
`xml1-economy.md` for no-drop, nesting, history and baseline details.

Then compare roll logs and native checksums through a ten-minute drop-heavy
fight and reject a peer differing only in rule-52 parts. Any difference returns
the shop rows to design. Ordinary production pools always contain non-unique
equipment at every G; an empty-bag outcome therefore needs a separately labeled
diagnostic sidecar fixture, not a claimed random production observation.
