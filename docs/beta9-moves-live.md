# Beta 9 movement acceptance

These changes require a live acceptance round. No game or emulator was launched
during implementation. Keep the beta 8 INI and enable all three keys:

```ini
[Game]
TelekinesisRule=xml1
Xml1FlyingCarry=1
GuardDecideRule=xml1

[Test]
InputPipe=1
PipeName=beta9-moves
VirtualPads=1
BossTrace=1
```

`InputPipe=1` is the existing test pipe. VirtualPads replaces the test copy's real
controller enumeration, so configure pad 1 for its player before testing. Check
the installed-rule log, dependency refusals, and the test copy's control binding:
X must be Guard on the pad; E must be Guard on the keyboard. Run keyboard and
pad scenarios with the appropriate player input selected.

The existing `pad_input.cpp::on_read` merges raw XInput button state before the
DirectInput profile maps the pad, exactly where real pad state enters. This
exercises more of the pad path than writing actor button bits. Keyboard `hold`
merges into the DirectInput keyboard state. Both timed commands wait for game
reads while held and after release; an error/unread input is a failed trial.
Neither requires F12, mouse input, OS key events, focus changes, or Frida.

First inspect the plan; add `--execute` only against an already running copy:

```text
python -B scripts/guard_input_live.py --pipe beta9-moves --device pad --ms 500
python -B scripts/guard_input_live.py --pipe beta9-moves --device pad --ms 500 --execute
python -B scripts/guard_input_live.py --pipe beta9-moves --device keyboard --ms 500 --execute
```

Repeat at 300, 500, and 800 ms. Record every command/reply and before/after
`status`; pad read counts must advance. Do the first complete round without
Frida. Direct handler fixtures cover normalized actor input and scheduling;
they do **not** certify hardware routing, world acquisition, or visible poses.

1. **Guard:** Beast Acrobatics 2 in empty air: each press returns to Idle by
   itself; observe 30 s without a stick/wake tap. Colossus: liftable wooden
   crate, a verified too-heavy object, eligible ally, untouched enemy negative,
   then an enemy after stun/hits. Acrobatics 3 at a confirmed wall: one lunge;
   rank 2 remains a negative. Repeat each positive with pad and keyboard.
2. **TK:** ordinary enemy HP 100, hold >=2 s, neutral release and moving-stick
   release, five each. Capture body +2B8, +2F0, +2F4, +2B4, +770 and impact
   42BF70 -> 499100. Native release 42E923 must admit private selector 2 even
   at normalized speed 1; 42E99B then writes evaluated L2. Landing inside its
   window reduces HP according to defenses;
   verify cooldown, expired throw and teammate policy. Repeat on an object,
   interrupted hold, immediate re-lift and AI Phoenix for a minute.
3. **Carry 2a/2b:** Phoenix Flight 3/full energy, eligible actor AI off. Hold
   Guard through pickup and >=4 s hover/movement/turns, then release. A new
   Guard press must descend through empty space. Repeat releasing at contact.
   Do >=120 frames of movement/node changes, ascent, fresh descent, landing,
   interrupted flight, throw and re-pickup. Flight 2 must never pick up.
4. **Carry damage:** enemy HP 100: Attack, then Smash on a fresh carry. At the
   authored victim_throw trigger verify all constraint bits 1/2/0 clear, one
   detach, nonzero evaluated L2 at +2B8, +2F0=now+2, attacker +2B4/+770,
   authored forward/downward velocity, and HP loss at impact. Verify same-team
   policy with friendly damage off/on and neutral NPCs separately.
5. **Pose:** ally and enemy low/high hover, moving and turning; measure both
   actors' animated bone5/6 midpoint. Their hand midpoints must align without
   floor sinking. Missing-bone fallback must remain finite. Ally hand alignment
   is an adaptation requested in the brief: XML1 30300's ally enum cases use
   DC040's root offset; enemy cases use 301E0 hands. Static tests cannot prove
   the requested visible likeness or animation asset correctness.
6. **Blob:** retain BossTrace output through a stalled opcode 12. Approach logs
   show raw setter_424A90_eax (void function; not a success boolean), meaningful
   target_4A7DA0_ok, destination actor+1D0, nav pointer +3E8 and evaluated state,
   brain+4/+1C0, prop distance and prior arrival/facing/action fields. `-1`
   means the interpreter short-circuited that predicate. No repeat gameplay
   predicate calls were added just for logging.

For sustained carry acquisition use `paddown 1 X 6000` / `padup 1 X`
or `down E 6000` / `up E`; this covers the 2–3 s pickup and established hover.
After release use a fresh 500 ms Guard press to test intentional descent.
A pipe executes one blocking command at a time; use nonblocking
`paddown`/`down` while sampling the pair. Never add a wake key. Real device hand
checks after the pipe checks are still required for the reported Guard defect.

## 082d follow-up B acceptance (not run by the code agent)

Use an isolated test copy under the launch/slot/registry rules, with
`[Test] NoActivate=1`, `InputPipe=1`, `VirtualPads=1`, `CarryTrace=1`, and
`TKTrace=1`. Keep the full 0.4.2 INI and the brief's three move options.
Capture fresh `state`/`status`, replies, pad read counters, current energy,
actor/victim move names, full handles and HP. Use registered `restoreEnergy`
and confirm a newer state sample actually shows enough current energy.
Do not count a timeout, unread input, invalid target or insufficient energy as
a successful trial. No wake keys, direct actor button writes or desktop input.

| Check | Input path and procedure | Acceptance |
|---|---|---|
| Untouched enemy | VirtualPads raw XInput -> pad mapping: `padhold 1 X 500`; repeat 300/800 ms. Separately DirectInput keyboard buffer: `hold E 500`. Five fresh unstunned enemies per device, then visibly stunned enemies. | Untouched: no `grabstart`, no held handle and no HP loss. Stunned: pickup still works. |
| Crate / heavy container / wall | Same pad-X and keyboard-E paths. Fresh liftable crate, verified heavy green container, then Beast at a clear wall with Acrobatics 2 and 3. | Crate lifts; heavy object gives cannot-lift feedback without claiming it; rank2 no lunge, rank3 one lunge. |
| TK 5 still + 5 moving | Pad path only: `trigger 1 R 1`, `paddown 1 A 2600`; sample during the hold, then `padup 1 A`, `trigger 1 R 0`. For moving trials add `stick 1 L 0.5 0` after 0.8 s, and center it at release. Use one living eligible enemy about150 units ahead, adequate measured energy, clear line and safe floor. | 10/10 valid holds lift. Save TKTrace attempt/selected/auxiliary/energy lines, especially if attempt1 misses. A trial with no TK entry is an input/dispatch failure to retain separately. |
| Held descent and acquisition | Phoenix Flight3 high above an ally, then enemy: `paddown 1 X 6500` / `padup 1 X`; separately `down E 6500` / `up E`. Repeat with a fresh500 ms timed press and release at contact. | Carrier descends before pickup; target stays at its starting floor height until pickup. No indefinite hover at the character cylinder. Repeat with an intervening solid ledge: no pickup through it. |
| Ally/enemy pose | Same acquisition paths; zoom at entry, hover, turn and movement. Retain move names/enums, hand-target trace and roots. | Known carry nodes with enum-1 still use actual hand alignment. Check Wolverine and soldier separately; no curled ally under the feet. This is a rendered-pose acceptance check, not certified by host matrices. |
| Attack / Smash throw | Fresh enemy carry and HP sample for each: pad `padhold 1 A 300` / `padhold 1 B 300`; keyboard `hold NUMPAD4 300` / `hold NUMPAD6 300`. | One native event and release; descriptor amount is trunc(9 * caster multiplier), then normal impact defenses apply. Record HP at release and landing separately. Both inspected parsers ignore source Smash's `velocityup` spelling; do not demand an invented-200 upward field. |
| Ten-second moving carry | After valid pickup, center/hold direction with `stick 1 L 1 0`, renew the same stick before its10 s hard release, and center after10.5 s. For the held-Guard variant renew `paddown 1 X 6000` at5 s without an intervening release; keyboard variant renews `down E 6000`. Refill every500 ms and measure energy for this retention trial; separately test normal exhaustion with no refill. | Pair survives movement and node changes for at least10 s with sufficient energy. Exhaustion still releases it normally. |
| Storm Flight2 /3 | Repeat the same pad-X and keyboard-E descent paths using Storm, with separately verified effective ranks2 and3. | Rank2 never picks up; rank3 picks up a valid target. The host fixture covers the actor-independent rank gate; Storm's content and visible animation remain live checks. |

Record failures without replacing the DLL with a diagnostic build mid-trial.
The retained 082c logs cannot identify each historical TK miss; the new trace
is for making the next result attributable.

## After the 082d live follow-up

Save complete `state` replies during each trial, not only position/move
summaries. Keep physical/actor/status flags, selected and owner handles,
flying state and current energy alongside the log and screenshots.

Verify learned/effective Flight rank before counting a carry trial. The
shortcut grants unspent points; that is not a rank purchase. Several 082d
captures show about40-point debit steps and become Idle around1.5 s; those
records do not establish effective Flight3 (native cost modifiers also exist).
Require a fresh `carry trace flight-rank-3 ... actor=<same full token>` line.
Do not remove the rank<=2 negative to compensate for an uncalibrated setup.

The measured carry probe now requires that evidence for `--execute`:

```text
python -B scripts/carry_live_probe.py --device pad --actor ID --rank 3 --trace-log PATH_TO_RUNNING_COPY/xml2-fix.log
python -B scripts/carry_live_probe.py --device pad --actor ID --rank 3 --trace-log PATH_TO_RUNNING_COPY/xml2-fix.log --execute
```

The first command is plan only. Keyboard uses `--device keyboard`. The rank
line must match the full actor generation and be within1 s of sampled state;
the carrier must already be airborne. Energy still requires a measured fresh
refill, never only the queued script acknowledgement. `restoreEnergy` did
not refill these 082d sessions, so retain a failed refill as a harness failure
and use verified natural regeneration for manual trials.

For a failed Guard press, retain `guard eligibility` with `native_grab`,
reaction, attachment and computed `stunned`. For TK misses, retain every
`tk acquisition`, `tk reject` and `tk control` line; candidate acceptance
followed by a native post-filter miss differs from an eligibility rejection.
For hover, retain `flight-input`, `flight-rank-N` and `descent-*` lines.
No descent line plus a flight-input line distinguishes an earlier input gate
from `descent-rank`, `descent-solid-block` or a probe miss.

## After the 082e live round (round 4 corrections)

**10.1c heavy container.** The ice tunnel's big green (tarp-covered) container is
`crate_tarp_ha`: XML1 `icetunnel1.eng` heaviness **2**, written as byte 1. XML1's
lift rule (`0x38400`, `docs/lift-rule.md`) is might rank + 1 >= heaviness, never 5;
its Might levels read Heavy / Massive / Gigantic Objects (classes 2 / 3 / 4).
XML1 Colossus (and Beast, Rogue) start at Might 1, so he lifts it at every rank, in
the first game as here. No liftable object in `icetunnel1` is heavier than 2. The
too-heavy check belongs to a hero **without** Might (Wolverine, Phoenix, Storm):
rank 0 lifts only class 1 (barrels, wooden boxes), so the green container and the
metal crates answer "Too heavy". The port's byte rule (might_heaviness >= byte)
gives the same answer for classes 0..3 at every rank.

**10.4 throw damage.** The carry throw arms the victim (`+2B8` = trunc(9 x caster
multiplier), window now+2) and only a **contact impact** consumes it (`499100`).
In 082e only the pad Attack throw reached a wall (the tunnel's end wall, x 334):
100 -> 96 on a Physical Resistant Revolutionary: consistent with 9 through the
receive stages (for example defense 3-4 and `physical_resistant`'s 20% reduction;
the soldier's defense value was not captured). The keyboard Attack
landed on open floor, and the authored Smash has no velocity (both parsers ignore
`velocityup`): 0. TK is the same: every TK HP loss in 082e happened during the
hold, at the ceiling (z 107) or against a teammate, never at the floor landing.
XML1 adds no landing damage either: `ch_thrown` (`E0E20/E0E50/E0EC0`),
`ch_thrown_land` (`E0EE0/E0F20/E0FE0/E1090`) and `ch_thrown_hit_wall` apply none,
and its `victim_throw` (`CEA90`) arms none. Whether XML1's receiver counts a floor
landing as an impact is not provable statically. Test throws **into a wall or
crate**; record open-floor landings separately. The device is not the variable.

**TK "picked, never raised" (tkA).** 082e's case is not 082d's: selection, owner,
flags and contact byte match the lifted holds, but the soldier had been placed
18.6 units from Beast's centre (inside his body) by `setPos`. He took the armed
TK amount at 0.1 s with no displacement (a contact impact) and never moved; no
other selected hold had a character within 60 units. XML1's TK entry (`DFC80`)
sets the same flags (+4 bit 4, psy bit) and no collision exclusion. Place TK
targets at least 60 units from every other character and verify it in `state`.
`TKTrace` now logs `tk blocked ... frames=15 contact=.. z=.. impact_age=..` once
per hold when a held target has not moved for 15 control frames.

**TK target choice** is XML1's: `DF740` and PC `4F2ED0` take the locked target
first, then the first candidate in query order (300 range, |dz| <= 120); an
enemy is taken at once, a physical object at once only within 100 units in front,
otherwise kept as the fallback. A prop next to a soldier 150 away wins only when
the soldier failed the filters; that is original behaviour, not a bug.

**Moving carry bumps.** Every 082e separation was the **carrier** landing: her
move went to `idle`/`run` on top of a crate or on a raised floor (z -59 in
haarp2_1), then the carried one dropped. XML1 `DC720` releases the pair exactly
on the carrier's ground handle or +4 bit 2, never on the carried one's contacts;
the host fixture now pins that. Carry at height (Jump) past props. CarryTrace
adds `carry trace release reason=...` (no-carrier / not-selected / not-flying /
carrier-ground / carrier-bit2) with both roots and ground handles.

**Stun sources.** Every stun in the converted content - the hero stun combo
(`fightstyle_hero` `attackstun3`, `dmgmod_stun`), Rogue's ability drain and
Xtreme, Storm's Xtreme, and the XML2-only power triggers `shared_tag=
shared_stunned` - applies the engine's built-in `shared_stunned` powerup
(`0x436171`/`0x54F060`), whose entry sets `+765` bit 0 before the reaction
(`0x54A0F7`) and whose expiry clears it with the attachment (`0x54A215`). On an
enemy GuardTrace therefore shows `reaction=01` with a nonzero attachment; the
attachment-only path is reachable only where StunRule clears a party hero's
flag, which Guard excludes. Do not wait for it on enemies. 50 npcstat types are
stun-immune through `def_stun` scale 0 (bosses, Sentinels, Danger Room robots);
every Brotherhood and HAARP soldier type is stunnable.
