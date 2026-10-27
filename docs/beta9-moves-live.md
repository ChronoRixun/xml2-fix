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
