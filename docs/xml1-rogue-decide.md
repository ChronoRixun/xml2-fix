# Rogue power decision (candidate, rule 43)

`[Game] Xml1RogueDecide=xml1` registers `ch_roguedecide`, absent from retail
XMen2.exe. Missing, empty, `xml2`, `off` or `0` leaves the image and gameplay
agreement record unchanged. Invalid settings and refused installs register an
unverified request and close host/join. Rule **43**, revision **1**, advertises
`{installed ? 1 : 0, 0, 0, 0}` only when requested.

Use the companion Legends Classic builder with `--rogue-decide xml1`. It emits
the matching INI key and restores two collapsed nodes: public `power2` with
`ch_roguedecide`, internal `ability_drain`. Special selects `ability_drain`;
Special2 selects `drain_notarget`. Both source chains have eleven ranks. Energy
stays on the decide move's tag 1; it is never transplanted onto drain. The
builder option defaults to `xml2` for this experimental candidate. Changing
only an existing build's INI cannot restore its dropped decide node. Rebuild.

The callback follows default.xbe entry 0xdbd30. It sweeps from the attacker's
bounds center along facing times 60, using a `{12,12,12}` box and examines only
the first contact. A noncharacter or rejected first contact ends selection;
it never searches for a later victim. With attacker `+0x3d8 & 1` clear,
same-team victims require the global or session friendly-hit override, and
team-none victims (XML2 team 28) pass the AI manager's exclusion check. An AI
attacker bypasses both gates.

For a victim, Special is resolved and `validTarget(victim)` controls only the
target lock. Invalid targets still select drain. If Special is missing, or no
victim was accepted, Special2 is resolved. Only this no-target move asks
`mayStart`. On refusal, the context byte at `+0x6c` is ORed with 3, actor
feedback and the error sound run, and the chosen move is cleared. Source tag 1
dispatches even after failure. A remaining move's animation enum is written
to context `+0`, then `start(actor, move, 1)` runs. The pooled query is released
after the entry's work. Pool exhaustion safely takes the no-target branch.

The persistent one-word handler clones eight slots from 0x692f4c; entry slot 1
and the freeing destructor are replaced. Generic update slot 6 remains
0x4fce50. It shares cling's existing 0x4fd860 lookup hook, with independent
settings: Rogue alone, cling alone, and both are supported. Unknown names
execute the five displaced bytes and continue at 0x4fd865. A shared hook is
accepted only while its jump still points at this owner.

Entry chain keys come from actor `+0x3c0` via move slot `+0x118`, actions 24/25.
The owning style comes from context's source move (`+4`) via slot `+0xe0`;
style slot `+0x14` resolves the key. Actor `+0x378`'s update resolver is not
used. Animation enum is move slot `+0xe8`, not the handler getter.

Queries use the acquired object's virtual slots `+0x40` (filter 1,1), `+0x80`
(mode 0), `+0x74` (three-argument sweep, XML2 stack cleanup 12 bytes), `+0x20`
(first result). Acquire/release, free-query count, pool getter and contact
entity accessor accept retail bytes or the exact published EntityCapacity
bridge. Raised queries never call retail query methods with their new layout.

Two deliberate fidelity differences remain:

- Native `validTarget` at 0x429210 retains XML2's extra stats `+0x2ae` high-bit
  rejection, absent from XML1. This suppresses locking only; it does not reject
  the drain branch. The port does not temporarily edit victim stats.
- Native lock at 0x42d8e0 retains XML2's targeting-history update through
  0x41d1d0, in addition to the shared victim timestamp, attacker handle, word 3,
  and attacker target-handle writes. No XML1-only lock implementation is used.

These are explicit native-service choices, not demonstrated runtime parity
under those conditions. All addresses/signatures target the owned retail
executable whose SHA-256 is
`146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b`.

Verification from a Win32 Release build:

```
xml2_xml1_rogue_decide_test.exe <owned-XMen2.exe>
xml2_xml1_rogue_decide_install_test.exe
xml2_xml1_rogue_decide_install_test.exe both
xml2_xml1_rogue_decide_install_test.exe raised
xml2_xml1_cling_test.exe <owned-XMen2.exe>
xml2_gameplay_agreement_test.exe
```

The installation fixture executes our production code against invented
services in its own reserved image. It proves a byte-identical 4 MiB OFF
image, individual guard refusals, lookup coexistence, real calling conventions,
first-hit filtering, both friendly overrides, neutral/AI gates, lock-only
validity, failure order, tag count, animation/start order, and safe query
release/exhaustion. Its raised mode poisons retail query methods and uses
published synthetic bridges; it does not replace live EntityCapacity testing.

`[Test] RogueDecideTrace=1` logs entry actor, context flags and final animation.
Pipe `status` reports `rogue decide xml1`, `game's own`, or `unavailable`.

Live checks for the game agent, side by side with owned XML1 in xemu:

1. Same room, Rogue rank and energy: a valid enemy within 60 units selects
   drain, locks the victim, animates and completes. Repeat across all eleven
   ranks, including the final differently named source rung.
2. No victim selects `drain_notarget`. Put a rejected character/prop first with
   another enemy behind it: only the first result affects the choice. Compare
   allies with friendly hit off/on and a neutral excluded actor.
3. Count energy charges: exactly one source tag-1 charge per entry, for drain,
   no-target, and a forced no-target `mayStart` failure. Confirm failure feedback
   and sound; no second time-zero/tag-90 charge on drain.
4. Repeat with EntityCapacity enabled and with cling enabled together. Check
   status and logs, no stuck moves, and no leaked query slots after many uses.
5. Co-op: on/on joins; on/off refuses on both peers naming **43
   Xml1RogueDecide**. An invalid key/refused install cannot host or join. Rebuilt
   OFF content and absent DLL key retain the former direct-drain behavior.

No game, xemu, energy-count or multiplayer run was performed by the code agent.
