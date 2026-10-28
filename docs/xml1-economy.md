# XML1 economy construction, drops, prices, shops, Grab Bag and saved rewards

`[Game] Xml1Economy=1` requests **52 Xml1Economy**, revision **1**. An absent
or false key reads no sidecar, contributes no patches and registers no rule.
Requires verified rule 47 NativeSaveLayout: `[Save] Format=lc`,
`[Limits] EntityCapacity=1`, `SaveLayout=w2`. A failed prerequisite, guard,
sidecar or name resolution leaves rule 52 unverified and online closed.

| Implemented row | Behavior |
| --- | --- |
| Sidecar revision 1 | Strict parser, SHA-256, typed tokens, count, duplicates and native definition-order checks |
| Table resolution | Names resolved through item-manager vt+0x78 after the native record walk; metadata exposed by definition index |
| Plan A constructor | Own records in native slot order, fixed interpolation 0, native reset/generations/linking/value cache, zero draws |
| Capacity preflight | W2 installed instance/attached counts and capacities; refusal returns `{0,0}`, counts attempts, logs once per definition |
| D7 fallback | Native construction behind the same preflight, one draw per own record, bit 32 clear |
| Agreement/status | Finalization held until resolution settles, rule published once, test-pipe economy status |
| XML1 roller | Definition-order reservoir, XML1 thresholds/leader extras/money split, synchronized stream only |
| Native spawn loop | Two selection bridges, four nested frames, native placement and health substitution |
| Treasure and DR | Full category-five treasure rolls; generated DR gear suppressed, other rewards preserved |
| Level adjustment | Regeneration returns before removing any equipped item; the level adjustment remains native |
| Generator census | Per-return-address tripwires retain native generation and the widened W2 picker |
| Sales and prices | 25% ordinary / exact float 33% unique, definition-cost cache, XML1 packs and raw-level courses |
| Obtained transactions | Purchase sets unique obtained; inventory sale clears it; equipped sales and stash transfers preserve it |
| Grab Bag | XML1 pools/reservoir/courses and one ordinary retry; native buy caller charges once, no refund |
| Shared sell list | Carried/equipped equipment including zero-cost rewards, positive-quantity packs and held sidecar-sellable plain items |
| Forge / Healer | Script-selected vendor mode; fixed Forge catalog, native Healer packs/Grab Bag/eligible courses |
| Two shop tabs | Buy and sell only; empty third label, empty Training builder and refusal before Training debit/dispatch |
| Rare styling | Economy unique first (12), non-unique frequency-2 equipment palette 11, all other styling native |
| Saved DR rewards | Catalogue `ITEM_OWN_REBUILT` instances take current own records in native slot order, interpolation bits 0, only with verified rule 52 |

All seven parts are installed together; none remains clear.
Plan A is `xml1_economy_rules::default_plan`. The selected plan controls both
dispatch and the agreement value; there is no independent runtime override.

Rule values: `{parts, 1, digest_prefix_le, entry_count}`. Parts = **127** for
plan A, **95** for fallback: mask 1 covers the complete roller/category/treasure/
generator-disposition install, mask 2 covers all sales/prices/obtained/sell-list
sites, mask 4 covers vendor mode/catalog/buy list/style, mask 8 covers Grab Bag,
mask 16 removes Training, mask 32 selects zero-draw construction, and mask 64
covers the rebuilt saved-instance migration (a loader policy, no retail site).
Each patch part is published only after the whole guarded contribution commits;
table resolution and all existing prerequisites still gate verified rule 52.
The first four
SHA-256 bytes are packed little endian (bytes `01 23 45 67` become
`0x67452301`). Status shows the bytes in their original hex order:

```text
econ on; plan A; defs 132; digest 01234567; forge 18/G1; gen 0; refused 0
```

Tripwire hits append `; gen <return-address>:<count>`, in address order. Each
return address is logged once; calls continue natively. Each roll logs its entry
serial, category, leader, G, bucket, selected definition/name, amount and
selection draw count. The exact pickup/no-drop line format is:

```text
roll n=<serial> cat=<category> lead=<0|1> G=<group> bucket=<bucket> def=<key|-> amt=<amount> draws=<selection-draws>
```

`n` is an unsigned 64-bit decimal counter, starting at 1 for the first enabled
roller entry in each process. Allocate it once at `0x46e5e2`, keep it in that
roll's DLL frame, and repeat it on every pickup produced by the roll (including
all money splits and leader extras). `0x46e760` selection never advances it.
Separate rolls with otherwise identical fields get different serials; group by
`n` before summing split money. Failed chance/no-drop rolls have their own `n`
with `def=- amt=0`. OFF entries and history reads do not advance the counter.
Nested entries get their own serial; a refused depth overflow consumes a serial
and is diagnosed separately. The serial does not reset on native RNG reseeding,
zone changes or save loads. Peers executing the same enabled entry history
therefore emit identical serials without extra RNG draws or synchronization.
Start capture from a matching process/session baseline; late-join serial
alignment is not persisted/transferred by this diagnostic counter.

Spawn placement draws and fallback-construction draws are separate. The read-only
test-pipe command `economy rolls` returns the latest sixteen roll log lines;
each is exactly the emitted serial-bearing line. This ring can start halfway
through a multi-pickup roll; use the complete log for G8/fight comparisons.
`economy status` returns the same economy segment as ordinary `status`.

`Data/x1economy.txt` sits beside the DLL and is generated by the builder,
never checked into git. Header lines are `x1economy 1`, `count <N>` and
`sha256 <64 lowercase hex>`. The digest covers bytes after the third LF to
EOF. Each body line contains `name group frequency flags cost vendor quality
source`, with single ASCII spaces and final LF. No BOM, CRLF, controls,
trailing spaces or extra tokens. Group = -1..5 or `-`; frequency = 1..5 or
`-`; flags = lowercase hex using only bits 0x01..0x40; cost = nonnegative
signed 32-bit integer or `-`; vendor = `forge`, `healer`, `none`; quality =
-2..2 or `-`; source = signed 32-bit integer. Names have no parser length cap.
The row count is bounded to W2's 1024 definition slots. Strictly increasing
resolved PC indices are required; aliases resolving to equal indices refuse.
Errors include the file line number. Resolution failure never publishes a
partial definition set.

The final builder contract has **131** rows: 116 equipment and 15 plain rows,
including eight inert flags-zero rows, two packs with cost `-`, three money
definitions and two pips. These rows are accepted without requiring a fixed
132-row count or deriving membership from nonzero flags. All equipment uses
vendor `forge`, including group -1; vendor classification does not itself make
a group -1 definition eligible for stock. The helmet retains `noaward`.

The LC catalogue's ITEM `attributes` mask **0x1** (bit 0) is `ITEM_OWN_REBUILT`;
it is distinct from sidecar flags mask **0x4** (bit 2). The C++ catalogue reader preserves that
word. Replacing saved instances' own lists using the flag is DESIGN step 8,
which is outside this constructor-foundation step. That migration must use
the current definition's own records with zero interpolation and preserve
affixes, location, quantities and obtained flags. The final builder marks
exactly 19 reward definitions; the builder compatibility test checks the
catalogue/sidecar agreement.

| Patched site | Guarded operation |
| --- | --- |
| `0x47bb50`, 8 bytes | Item-manager vt+0xe0 entry, full displaced instructions retained in executable trampoline |
| `0x4806ad`, 5 bytes | Loader final call: invoke `0x47ce80` before sidecar resolution |
| `0x46e5e2`, 8 bytes | Skip party extras and native selection; publish count, enter `0x46e747` |
| `0x46e760`, 6 bytes | Copy each descriptor and amount, including leader entry zero, enter `0x46e804` |
| `0x46ea43`, 11 bytes | Release only the matching frame, replay native SEH restore |
| `0x421f3f`, 1 byte | Actor load default category 4 becomes 3; authored category writers unchanged |
| `0x469c80`, 5 bytes | Special treasure event invokes game vt+0x25c `(5, source, 0)` |
| `0x448dc0`, 6 bytes | `ret 4` before the regeneration prologue/removal loop/RNG |
| `0x4c908a`, 8 bytes | Skip generated-gear block `[0x4c908a,0x4c911c)` to `0x4c926b` |
| `0x47d4b0`, 8 bytes; `0x47ecb0`, 5 bytes | Generator and special-generator observers; displaced instructions preserved |
| W2 copy of `0x47b590`, 5 bytes | Picker observer at its prepared copied entry, retaining W2's retail redirect |
| `0x481b80`, 5 bytes | Alternate initializer observer, only for economy definition argument |
| `0x5a9cd8`, `0x5a9d2c`, 4 bytes each | Repoint only the sale multipliers to fix-owned `0.25f` / `0.33f`; native ftol stays |
| `0x481170`, 6 bytes | Economy definition cost (absent = 0) into cache `+0x28`, set `+0x30 & 2`; legacy native trampoline |
| `0x480dd8`, 6 bytes | Compute `225G-125`, replay the displaced EBX/ESI pops; epilogue at `0x480dde` unchanged |
| `0x5a9b8d`, 5 bytes | Read raw course level byte; global `0x46eae0` unchanged |
| `0x5d3ee9`, 6 bytes | Capture owner by native transaction frame before equipped removal |
| `0x5d404b`, 6 bytes | Forward shared native state call, then update economy-unique obtained by state/stash/owner |
| `0x5d2639`, 7 bytes | Replace non-stash plain-item appends; both native equipment walks, stash branch and final presentation remain |
| `0x49e64b`, 7 bytes | Capture the native `shopMenu` value's text before its original open queue |
| `0x46a1a0`, 6 bytes | Reconcile stock before any native refill or top-up draw |
| `0x5d2a55`, 7 bytes; `0x5d2b18`, 8 bytes | Forge instance-only rows in definition order; Healer retains synthetic rows and skips stock walks |
| `0x5d35d0`, 9 bytes; `0x5d36c8`, 15 bytes | Clamp direct selection and native navigation to two tabs in a normal shop |
| `0x5d3a34`, 5 bytes | Successful open sets fix-owned shop state, reconciles fresh G, then selects the initial tab |
| `0x5d3b2a`, 8 bytes | Empty third shop label; stash label path unchanged |
| `0x5d2b70`, 5 bytes | Training builder clears the shared row count and adds nothing |
| `0x5d3ff0`, 9 bytes | Refuse Training row types 3–5 before price calculation or debit |
| `0x5d4104`, `0x5d4119`, `0x5d412e`, 5 bytes each | Refuse each Training service dispatch in a normal shop |
| `0x480e20`, 5 bytes | Unique-first economy frequency styling; displaced native entry retained |

All sites join W2's startup transaction with retail guards, verified
readback/protections and rollback. Census: zero direct calls/jumps to
`0x47bb50`, one pointer at `0x6883f4` (vt+0xe0), no branch into displaced
instructions. Legacy/null definitions and argument forms other than
`(def,1,0,0,0,0)` execute the native body.

Prepare this contribution before `entity_capacity::contribute()` consumes
W2's prepared marker. Publish/settle it after `native_save_layout::after_commit()`
has verified rule47. The production-DLL startup gate checks this ordering.

When economy is requested, X1Leader installs its pain gate and leader classifier
before this transaction; its full retail guards still run. Its legacy count
`0x46e735` and pick `0x46e774` bridges are omitted. Both sites are guarded by
the economy owner; a pre-existing legacy bridge vetoes the whole startup union.
Rule 7 keeps revision 2 and its existing values.

G and the highest level are read from the registry at each roll. Actor money
uses the native handle resolver/cast/short-level path; non-actor money uses
G squared. The definition unique and obtained getters are vt+0x50/+0x54.
Ordinary treasure argument 3 is ignored, so equipment and the XML1 money split
remain eligible. The native DR roller branch, nearby-item gates, health
substitution and placement RNG are preserved. Frames are keyed by their native
stack address; the fifth nested roll returns zero without disturbing its parent.

The XBE listing clarifies two details in DESIGN 1.2/3.1: outer-gate and reservoir
comparisons accept equality (`0x710d4`, `0x7125c`), and money splitting replaces
the leader's initial four-drop count (`0x713aa`); only its first pickup retains
the money amount. The implementation follows those instructions. The native RNG
returns 31 state bits in ST0; only the bucket and reservoir reciprocal are
explicitly stored as float. Selection temporarily uses XML1's CRT 53-bit,
round-to-nearest precision and restores the caller's x87/MXCSR controls before
descriptor construction and physical spawning. This prevents PC D3D8's 24-bit
mode from changing gates, money or inclusive integer ranges. Money products
retain full precision until truncation, including tested reachable RNG states.

Plan A uses W2's published 1024-instance/4096-attached layout, with count
offsets from `item_capacity_rules`. After preflight it calls vt+0xe4
(`0x47c990`), resolves with `0x480f30`, sets instance `+4`, reads eight own
slots via definition vt+0x10, skips negative indices, attaches using
`0x481910` (ECX=instance; list=instance+0x10; record; float 0; AL result;
callee pops 12 bytes), then calls `0x481170`. Native reset leaves affix lists
empty and special-name fields untouched; the initializer does not set
obtained. The builder must supply single-valued records for interpolation
parity. The final `0x481170` call now applies the definition-cost cache contract
for every economy definition, including definitions constructed through the
fallback. Legacy definitions retain their native affecter-derived value.

All three cache-calculator callers (`0x481b6e`, `0x481d13`, `0x482bd3`) enter
the same guarded entry. The native getter's sole cache read at `0x480df7`
therefore agrees after normal initialization, alternate initialization and
deserialization. Zero-cost equipment remains in the sell list and sells for 0.
Packs cost 100 / 325 / 550 / 775 / 1000 for G=1..5 and sell for
25 / 81 / 137 / 193 / 250. Held pips sell for 375 from their authored 1500
cost. The native 2000G-squared Grab Bag price is unchanged. Course rows use
10 times the raw level squared, including when game mode is greater than 1.

The owner capture is keyed by the native transaction frame and consumed before
forwarding vt+0x104. After that call, state 2 outside stash sets obtained;
state 4 outside stash clears it only for a captured null owner. State 5 and
stash mode preserve obtained. Every action also requires an economy definition
and the native unique predicate. Ordinary inventory sales can thus make a
unique eligible again; selling equipped gear preserves obtained.

The shared non-stash sell builder retains carried and equipped equipment with
no cost filter. It appends HEALTH_ITEM then ENERGY_ITEM only with positive
quantity, then other sidecar flag-0x10 non-equipment in definition order with
positive quantity. The native append plus an explicit bound preserve the
shared 100-row capacity. Both vendors use this same builder. Stash skips the new block.

`shopMenu` consumes its text getter once and records `equip` as Forge (ASCII
case-insensitive); `item`, empty and other values select Healer. Its native menu
availability check and `openmenu shop` queue remain. A successful non-stash open
reads the registry's current highest roster level (`0x449960`) and computes
`G = clamp(highest / 10 + 1, 1, 5)`. Forge reconciles again when its buy list is
rebuilt, so non-unique purchases immediately regain their catalog row.

The Forge catalog contains sidecar equipment with group 1 through G, excluding
Grab Bag flags and obtained uniques. It retains exactly one state-3 instance
per definition, frees duplicate/out-of-catalog stock and all state-4 sold units,
then constructs missing stock through `x1_make_instance` in definition order.
Individual native unlink/free calls preserve obtained flags: the native bulk
state deletion resets unique obtained flags and is deliberately avoided.
Carried/equipped/stashed units are untouched. The buy rows also follow definition
order, independent of the native linked-list insertion order. Plan A draws zero;
the agreed fallback retains its per-own-record native construction draws.
W2's constructor preflight refuses missing units without partial allocation
when either pool is full. The real catalog uses at most 85 of 1024 instances;
the shared array has capacity 100. No native stock generator runs.

Healer opens clear equipment stock and retain the native pack, Grab Bag and
eligible-course synthetic builders. Grab Bag still uses the native generator
until its separate part 8 is installed; only the refill tripwire is closed here.
The fix-owned no-Training state is set at successful open and checked with the
shop identity and native stash predicate. It never changes `shop+0x18e8` bit 0.
Stash navigation, selection, labels, builders and service routing stay native.
Training refusal covers both stale buy rows before debit and the three direct
dispatch call sites. The actual style entry is `0x480e20`; `0x480e25` in DESIGN
identifies the unique-check call inside it.

`xml2_economy_rules_test` checks parsing/refusals/agreement.
`xml2_economy_test` maps the owned retail executable inside a synthetic host,
commits W2 plus this module, and compares 132 definitions: 116 equipment with
1..5 records and 16 plain items with no own records, at PC indices160..291.
It covers unique/non-unique items, all slot positions and negative slots from
identical complete pool snapshots.
Native allocation/reset/attach/link/cache/RNG and bonus consumer `0x45ef50`
execute; synthetic content/name/property edges and CRT veneers supply the
environment. Comparison covers every instance byte, attached bytes except
interpolation `+8`, indices/link order/count, evaluated bonuses and cached
value/validity (normalized to step 4's definition-cost contract). Both plans must pass; draws are zero for plan A and n for
fallback. Separate modes cover OFF, prerequisites, sidecar, unresolved names,
each guard and rollback. `scripts/run_native.py` runs them;
`scripts/check_xml1_economy_text.py <retail-exe>` checks guards and census.
`scripts/check_xml1_economy_startup.py` checks actual DLL loading, prerequisite
and missing-file refusal, all sales/prices hook states, and closed admission
while table resolution waits.
`xml2_economy_rules_test --builder <build-folder>` also validates the actual
sidecar against the C++ catalogue/bind loader and PC definition-index order.

`xml2_roll_rules_test` compares 100,000 rolls with an independent XBE oracle,
covering all categories, roster groups/levels, unique subsets, Xtreme capacity,
actor/non-actor money, thresholds and truncation boundaries. The fixed corpus
consumes 619,141 draws in each implementation; placement is excluded. Native
regressions exercise incoming 24-bit and 53-bit x87 modes and verify restoration.
`xml2_roll_test` executes the installed bridges and native loop progression,
treasure events, regeneration callers and tripwires on mapped retail code.
`xml2_dr_reward_test` executes the complete native course reward dispatcher
before/after the patch and asserts placement draws separately.
Engine objects and dependency methods in these tests are synthetic; full game
lifecycle reachability and per-zone tripwire acceptance remain live gates.
`scripts/check_xml1_roll_text.py <retail-exe>` checks all guards and the static
generator census. The vendor refill generator caller is now unreachable through
the rule's paths, and the replaced Grab Bag entry bypasses its generator call.

`xml2_economy_test <retail-exe> prices` executes the native getters, sale x87
paths, full course helper, deserializer, buy/sale blocks and sell builder.
It checks price/truncation vectors, definition cost versus affecter cost,
absent/zero costs, legacy ordinary/unique sales, transaction state/owner/stash
cases, post-call obtained order, stale-owner reuse, equipped row ownership,
positive-quantity plain items, shared vendor rows and the final row at capacity.
UI, roster, cached descriptors, stream payload and manager state-list service
are synthetic environment edges; this is not a live shop/co-op acceptance run.
Row comparisons ignore the native row's three unwritten padding bytes.

`xml2_economy_test <retail-exe> shop` runs T-FORGE, T-TAB and T-STYLE on mapped
retail code: synthetic sidecar-shaped 18/37/52/69/85 catalogs, native pools and
list mutation, old/duplicate stock, unique flags, idempotence and exact pool
budgets, non-unique restock, zero selection draws/tripwires, both vendors and
stash, native tab cycle/setup, empty Training builder, pre-debit/direct-dispatch
refusal, pool exhaustion and agreement differences naming rule 52 for either
part 4 or 16. Script values, registry highest level, UI and course eligibility
results are synthetic environment edges. Visual rendering, real gameplay,
online menu lockstep and actual-builder catalog counts remain live/content gates.
`scripts/check_xml1_economy_shop_text.py <retail-exe>` checks all 14 retail
guards, instruction boundaries, refill ABI, stash bypass, service targets and
absence of direct branches into displaced spans. All new guards participate in
the full-image refusal/rollback fixture and production startup gate.
`scripts/check_xml1_economy_prices_text.py <retail-exe>` checks all eight guards,
three cache callers, unchanged pack epilogue, stash bypass and interior branches.


Grab Bag part 8 replaces the complete reward entry at `0x5d3720` (six guarded
bytes), so its old `vt+0x138` call at `0x5d384d` is unreachable. The native buy
block `0x5d3ff0` through `0x5d40a3` remains intact and debits exactly one listed
`2000 * G * G` price, including retry and empty outcomes (owner D6).
`xml1_grab_bag_rules::x1_grab_bag` consumes the lockstep stream in definition
order: strict `< .33f` pool choice, exact group, equipment flag 0x40, matching
Grab Bag flag 0x02, obtained uniques excluded; one draw per eligible row.
As on XBE `0x17c5d9`/`0x17c5e2`, the reservoir accepts equality against a
binary32 reciprocal, matching step 3b. The `.33f` and `.1f` tests are strict;
random results retain 31-bit precision rather than being forced to float.

Every pass draws the course test, including the forced ordinary retry (which
skips the pool draw). It searches on a passing test or an empty flagged pool.
The first ascending native course with `+0x4f & 1` clear, nonzero **word**
`+0x4c`, and **raw byte** `+0x00 < highest roster level - 5` wins.
Its `+0x44` interned identifier is formatted as `loadDangerRoomCourse('%s')`
and submitted to native script-manager `vt+0x0c` then `vt+0x3c`, mode 1,
as the original bag does; the existing handler `0x4a72e0` owns unlock and
notification. Otherwise the selected equipment uses `x1_make_instance`,
native inventory `vt+0xec` with quantity 1/state 2, unique obtained setter
`0x47ac50(1)`, and the native inventory notification/UI refresh route.
After two empty passes it returns false; the native caller ignores the result
and performs no refund. Allocation refusal likewise creates no reward/refund.

Native obtained/course state is snapshotted before scoped 53-bit/RN selection;
x87 and MXCSR are restored before construction, script calls and inventory/UI.
`xml2_grab_bag_rules_test` uses an independently transcribed XBE oracle minus
its debits, with scripted outcomes/boundaries and 100,000 differential bags.
`xml2_economy_test <retail-exe> bag` runs the installed entry from the actual
native buy block for six outcomes at all five groups under both incoming
precision modes. It executes real plan-A construction, inventory insertion,
obtained setter, price/debit dispatch, and course handler `0x4a72e0` (unlock
and native notification call); roster/content/script argument parsing and HUD/UI
are synthetic edges. It verifies exact draws, one charge, script formatting/dispatch and
control restoration, plus agreement with matching/mismatched parts values.
The full fixture also covers a pre-prepare bag guard mutation, a post-prepare
mutation, and rollback of the complete mapped image. The production startup
gate includes this site; `scripts/check_xml1_grab_bag_text.py <retail-exe>`
checks the guard, sole caller, preserved debit and no external entry into the
old reward body. Rendered course notification and peer shop execution remain
live acceptance gates; these tests never launch the game.

## Rebuilt rewards in existing saves (part 64)

The catalogue attribute `ITEM_OWN_REBUILT` is bit **0x1** of an ITEM entry's
attributes word. It is distinct from sidecar flag 0x4. The builder marks exactly
19 Danger Room reward definitions. During LC campaign reverse translation,
`Pos::instance` replaces each marked instance's own list with the current
build's `ITEM_own` records in native slot order, with interpolation bits 0.
This also removes obsolete own records from special-name instances and handles
zero-record definitions. The saved own list is discarded before resolving any
of its IDs: shortened definitions can remove old records entirely. Prefixes and
suffixes still resolve normally. More than eight current own records or an
unmapped current/affix record refuses without exposing a partial stream.

Prefixes, suffixes, special name, DEFSTATE quantity and state (including
the definition's obtained bit), and all other data follow the existing loader.
Location is preserved except when worn gear is recovered to carried inventory.
Carried, equipped, world and restore equipment share this translation. No LC
format or catalogue identity changes. The legacy importer's {375, 512} item-pool
inference is unchanged.

Twelve rewards change from belt/armor to gloves. An LC equipped list records
membership, not a slot index. For a pre-economy catalogue (revision below the
existing revision **8** that introduced the rebuilt classes), the loader uses
the historical ITEM class to identify the worn slot. A marked reward whose new
class differs is removed from HERO/REST equipment and appended to carried
inventory with location **2**, even if twenty items are already carried. It keeps
its affixes, special name and definition quantity/obtained/state. Each recovered
instance adds one load-ledger note naming the owner and old slot; the engine
forwards the ledger to its log. Fitting gloves and unmarked gear stay equipped.
A retail import still describes historical equipment even when imported against
catalogue8, so META origin 1 also enables this recovery. No importer change.
The ordinary native resave stamps current catalogue/native origin; newly equipped
rebuilt gloves remain equipped on reload and later catalogue growth. Recovered
instances are already carried, so no relocation or note repeats after resave.

The installer binds the loader to its live verified rule-52 state after agreement
registration. The library defaults to disabled; attributes alone, a requested
but unresolved install, guard refusal and rollback never enable migration. A
runtime loss of verification disables it for subsequent loads. No additional
retail patch site is needed. Rule values now include part 64, so a peer with
the previous parts value is refused naming `52 Xml1Economy`.

`lcsave_test` includes preservation, removed-record, class-recovery, OFF,
eight-slot/refusal and fixed-point cases (including an already-full inventory
and current-catalogue re-equipping). `--rebuilt CASES --data BUILD --out OUT --enabled 0|1` runs the
read-only corpus driver; `test/lcsave_rebuilt_reference.py` prepares real and
seeded cases using the corrected Python reference and compares native instances,
saved bytes and fixed points with both the current loader and committed baseline.
`--seeds DIR` includes the coordinator's manifest-pinned SaveLC seeds. The
Python reference supplies own-list behavior; a separate literal twelve-identity
expectation checks the owner's equipment recovery policy and its one-time notes.
Generated save data belongs in an ignored workspace output folder.
