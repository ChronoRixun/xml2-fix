# X-Men Legends 1's lift rule

`[Game] LiftRule=xml1` lifts the objects that carry their X-Men Legends 1 heaviness (`xml1heaviness`, written by
the X-Men Legends 1 port's builder for XML1 heaviness 4 and 5) by the first game's rule, so Might rank 3 lifts the
first game's heaviness-4 objects again. An absent key, empty, `xml2` or `off` leaves the game untouched; any other
value is logged and refused (and closes host/join). Gameplay rule 38, revision 2, `{installed,0,0,0}`, registered
only when requested. Candidate: static analysis only, not yet played. `[Debug] LogLiftRule=1` logs every remembered
attribute and every decision.

## The two rules

| | X-Men Legends 1 (default.xbe `0x38400`) | X-Men Legends II (`0x427f60`) |
|---|---|---|
| Heaviness | physent +0x2c3, 0..5 | physent +0x31d, clamped to 0..3 where read (`0x498995`) |
| Lifts when | not carried, then heaviness < 5 and might rank + 1 >= heaviness | not carried, then heaviness < 3 and might_heaviness >= heaviness |

The builder writes XML1 heaviness h as max(h - 1, 0) and 4 and 5 both as 3, and gives might rank r the affecter
might_heaviness r. For heaviness 0-3 that is XML1's table at every rank; XML1's 4 (Might 3 lifts it) and 5 (never)
share XMen2.exe's never-lifted 3. The rule tells them apart by the attribute, never by a wider byte, so every other
reader keeps its existing scale except thrown impact and telekinesis, explicitly bridged below. The legacy
revision2 movement-hit bridge changes the native formula; the complete source parameter pairing is supplied
by rule46 `TelekinesisRule`.

## The patch

- `0x498991` (the heaviness clamp after the physent reader asked for `heaviness`): replays the clamp, then asks the
  same reader for `xml1heaviness` and keeps it by the object's handle (as `[Game] BreakRule` keeps `xml1structure`
  from its own bridge at `0x498970`, which this one does not touch).
- `0x427fa3` (the lift test's `cmp heaviness, 3; jae`): for an object that carries the attribute and whose byte is
  still what the builder wrote for it, the answer is XML1's rule with the actor's might_heaviness (`0x427dc0`);
  everything else, and an object whose byte a script changed, runs the game's compare. The carried test and the
  lift-anything overrides before it are the game's (they are XML1's too).

## Every reader of the heaviness byte

The byte is unchanged by the rule. What changes is that a Might-3 hero can now pick up a byte-3 object, so the paths
after a lift (carry, throw, impact) now see byte 3 for an XML1 heaviness-4 object; they are listed with their XML1
counterpart. "Same" means the same formula on the port's byte (XML1 h -> byte h - 1). Revision 2 corrects the
rescale's offset at thrown impact and the telekinesis filter; other readers retain the byte.

| XMen2.exe | What it does | XML1 | With the rule |
|---|---|---|---|
| `0x498991` / `0x4989a2` | physent parse: clamp 0..3, store | `0x93880` (store); `0x90fe0` tests heaviness >= 5 (role not verified) | bridged: also keeps `xml1heaviness` |
| `0x422c27` | actor init: heaviness from stats +0x28c, clamp 3 | `0x32a20` (stats; tests 5 and 4, stores 4) | unchanged (characters carry no attribute) |
| `0x427f60` (`0x427fa3`) | the lift test | `0x38400` | XML1's rule for attribute carriers |
| `0x4ec4df` | pickup handler calls the lift test (AI and players) | `0xd99d0` calls `0x38400` | AI with Might 3 picks such objects too, as XML1 |
| `0x4306c7` | throw: impact level (h + 1) x 2, flag 8 when power > 5 and h > 1 | `0x3f980` (h x 2 + 2, same flag) | revision 2: use XML1 heaviness, levels 6/8/10/12 for XML1 classes 2/3/4/5 |
| `0x4f3f3b` | handler throw: level 1, flag 8 when h > 1 | `0xe0848` (`0xdff10`/`0xe0008`/`0xe000d`) | same (3 > 1, XML1 4 > 1) |
| `0x42e9e7` | drop/launch: flag 8 when h > level | `0x3db50` | same |
| `0x4ef3bd` | handler: carried heaviness against 1.0 (heavy carry branch) | `0xdff10` family (`cmp h, 1`) | same branch (heavy) |
| `0x4f3e85` | TK movement hit: damage x max(h, 1) | `0xdff10`, damage at `0xe07ef` | legacy revision2: signed cache+B / 3; source DamageLevel pairing requires rule46 |
| `0x4f2e16` | telekinesis / suspend trigger target filter: byte <= trigger `Heaviness` | `0xdf426` (heaviness > limit -> skip) | revision 2: XML1 heaviness <= signed trigger limit (Phoenix 2/3/4); class 5 fails all three limits |
| `0x497376` / `0x497383` | collision: mass x heaviness of both | `0x91560` | unchanged byte |
| `0x497907`, `0x49797c` | impact damage by heaviness difference (clamped 0..3) | `0x912b0` | unchanged byte |
| `0x498094`, `0x4981ca` | object damage `0x497e70`: knock below 2, impulse / h above 2 | `0x92220` | unchanged byte |
| `0x49901d` / `0x499042` | push test: other <= 2 x own; above level | `0x92970` | unchanged byte |
| `0x4995fa` / `0x499601` | push test (second shape) | `0x92b60` | unchanged byte |
| `0x498908` | physent re-read (`0x498900`) | `0x93320` | unchanged byte |
| `0x505688`, `0x5127ec` | AI object weighting 1 - k x h | `0xfd7e0`, `0xfe6d0`, `0x10a5a0` | unchanged byte |
| `0x54baac` | heaviness against 1.0 (`0x54ba90` area) | `0x984f0` (pairing not verified) | unchanged byte |

## Tests

`xml2_lift_rule_test`: the decision for every might rank 0-4 and XML1 heaviness 0-5 against XML1's table written out,
that the native pickup differs only at Might >= 3 and heaviness 4, the store, stale-byte fallback, every class x rank for the three revision 2 consumers, and every guard and displaced byte against `XML2_FIX_TEST_EXE`. The test also seeds a private image from those bytes, proves OFF leaves all 4 MiB unchanged, rejects each foreign guard without writing, and executes the installed bridges with synthetic objects/readers/cache entries. It never launches the game. The builder's side is legends-classic `test_object_physics`.

Revision 2 local validation: `xml2_lift_rule_test` **376 checks, zero failures** with the retail executable,
including all 12 guards; `xml2_gameplay_agreement_test` **zero failures**, including revision 2/revision 1
refusal both ways and unchanged OFF records; `xml2_xml1_cling_test` **54 checks, zero failures**. The DLL builds
in Release/Win32. The unchanged companion builder's focused `test_object_physics` passes **17 tests**.


## Revision 2: verified contracts and scale choice

No builder change is required. Use the existing `xml1heaviness` for classes 4/5 (clamped at 5); without it,
byte 1/2 becomes XML1 class 2/3 by the builder's inverse. Byte 3 without metadata becomes 4; a legacy build that
omits the 4/5 metadata must be rebuilt with the existing companion builder for exact class-5 behavior.
Byte 0 remains 0 because the inverse cannot distinguish XML1 0/1. Restoring class-1 throw impact needs authored
metadata and is outside this revision's requested classes 2 and up. An inconsistent stored attribute (script-changed
byte) retains the native byte in these two consumers, as the pickup gate retains its native decision.

- **Thrown impact:** bridge `0x4306c7..0x4306d0`, continue `0x4306d1`. ESI is the object; replace only AL's
  heaviness input, replay `(h + 1) * 2`, and retain the native byte store at physent +0x2e2. XML1
  `0x3fd7a..0x3fd9e` inside `0x3f980` stores `2*h + 2` at physent +0x298. No Might multiplier.
- **TK movement-hit damage (legacy bridge):** `0xdf3b0` is the TK filter, not the counterpart of `0x4f3e85`;
  `0x39a30` divides impulse for heavier victims. The corresponding XML1 control update
  is `0xdff10`, whose damage calculation at `0xe07ef..0xe083d`
  reads signed actor +0x53b DamageLevel, divides by 3 toward zero and stores the result at thrown physent +0x294.
  It has no heaviness multiplier. XML1's parser `0xd40fe..0xd4124` resolves `damagelevel`; `0xd421b` copies it
  to actor +0x53b. XML2 `0x4f3674..0x4f3696` caches Heaviness at entry +0xa, ImpactDamage at +8 and **tklevel**
  at +0xb; `0x4f3948..0x4f3952` keeps that entry at stack +0x10. The legacy revision2 bridge replaces
  `0x4f3e85..0x4f3ebc` with signed entry +0xb / 3 in EAX, then resumes at `0x4f3ebd` for the game's existing
  float conversion and damage store at physent +0x2b8. Thus cache tklevel 9 produces damage 3 there,
  instead of XML2's `max(ImpactDamage / 10, 3) * max(byte, 1)`. This legacy bridge mispairs the source
  parameter. Rule46 replaces the complete control callback and reads the distinct signed DamageLevel from
  the attack record; LiftRule supplies decoded heaviness for its filter and hit flags.
- **Telekinesis:** bridge `0x4f2e16..0x4f2e1c` supplies XML1 heaviness in EDX, then resumes at `0x4f2e1d`.
  Keep the signed trigger limit, comparison, skip target `0x4f2e76` and all other eligibility tests. This is
  XML1 `0xdf426` inside the correctly paired `0xdf3b0`. Phoenix limits 2/3/4 select at most classes 2/3/4;
  class 5 fails each. Other signed limits retain their authored meaning (there is no universal TK cap at 4).

All five writes (parse, pickup and the three new bridges) share one guarded transaction. Invalid settings or any
changed guard refuse the entire request. Key absent/empty/xml2/off returns before reading or patching game memory.
Only requested rule 38 changes to agreement revision 2; revision 1 peers are refused in both directions, naming
LiftRule. OFF retains the released agreement record.

## Revision 2 live checks (pending; game agent only)

Use objects built with the existing companion builder's `xml1heaviness` (4/5) and this DLL. These checks have
**not** been run here; code-only work never launches the game or xemu.

1. Confirm log `lift rule: xml1 ... revision 2` and pipe `lift rule xml1`; enable `[Debug] LogLiftRule=1`.
2. Throw XML1 classes 2/3/4 at fixed targets with Might ranks 0..3 wherever pickup is allowed (use a controlled
   throw setup for refused combinations and class 5). Compare impact levels 6/8/10/12 and resulting breaks against
   XML1, three trials each. Rank 3 can pick up class 4; no normal pickup can lift class 5.
3. For the carried throw path, hold trigger DamageLevel at 9, then 15: launch damage must be 3, then 5 for every
   object class 2..5 and every Might rank 0..3 before downstream hit modifiers. Repeat with a DamageLevel not
   divisible by 3 (5 -> 1), comparing original XML1. Capture LogLiftRule's DamageLevel/damage pair. This replaces
   the earlier assumption that XML1 scaled this damage by heaviness.
4. Phoenix's trigger limits 2/3/4: try every class 2..5 at each limit. Accept exactly classes <= the limit,
   subject to the native carried/team/type checks; class 5 is refused at all three limits. Repeat three trials.
5. With the key absent, compare pickup, throw and TK behavior against beta 7; run coordinator off-wire and
   combined-install proofs when the PC is free. No game registry or installed files were touched here.
6. Co-op: revision 2/revision 2 joins; revision 2/revision 1 refuses both ways naming `38 LiftRule`; revision 2/OFF
   refuses both ways. Confirm matching thrown damage/impact and Phoenix target choices during play.

### Telekinesis parameter correction

The revision-2 bridge at `4F3E85` reads cache byte+B. Further owner-parser inspection identifies that byte as **tklevel**, not XML1 damagelevel. The legacy bridge alone does not establish the source TK damage contract. `TelekinesisRule=xml1` (rule46, [coupled contract](tk-guard.md)) replaces the entire control callback, obtains the distinct signed damagelevel from the native attack record, and uses LiftRule only for candidate heaviness. Revision2 behavior stays unchanged when the new key is absent.
