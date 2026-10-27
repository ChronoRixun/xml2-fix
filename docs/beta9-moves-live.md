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
