# Ordinary hazard startup delay

`[Game] HarmLoopStartup=xml1` is an opt-in candidate that preserves the original
creation-relative `firstact` delay and start-on visual loop. Absent or `xml2`
keeps PC behavior. It is separate from BoxHarmOrigin, rule 21.

**Not registered: no gameplay rule (rule 23 covers only `HarmSingleChain`).** A request invokes
the existing unregistered-rule refusal even if installation fails, so this
candidate cannot negotiate online as an unchanged build. Do not enable it in
release defaults until rule assignment and acceptance are complete.

The builder's developer option `--harm-loop-startup xml1` preserves the source
delay and writes this key. The default keeps the earlier compatibility rewrite.
Both halves are needed: this engine adapter cannot recover delays already
converted to zero in existing output. Saves and protocol are unchanged.

## Proven difference

The original base action parser schedules `firstact` when constructing the
entity, through 0x277e0 and callback 0x27550. PC does the equivalent at 0x41bf24
and callback 0x41aab0. The PC harm parser additionally disables a positive-delay
hazard at 0x4399c5, clears its loop-on flag, and posts an on/off event. Ordinary
enable does not restore that flag. The earlier builder workaround therefore
changed positive `firstact` to zero, making the flame visible but advancing its
initial activation.

The ten-byte hook at 0x4399c5 bypasses that additional path only when the current
hazard is ordinary (not smartfire), its loop-on flag is set, and both on/off
timers are zero. It resumes at 0x439a2f. Other configurations execute the original
disable call and continue at 0x4399cf. Signed zero is accepted for timer fields.
Base activation scheduling, repeat intervals, damage and touch behavior remain
native. Short guards cover the base first-act schedule, timer layout and the
patched branch; installation uses preflight, readback and rollback. Unsafe
rollback refuses startup.

## This does not impose a single chain

`firstact=1` is not a one-second grace period on each entry. A player entering
after startup may be hit by the already-running chain or by touch activation.
XBE 0x28420 schedules the next repeat; the enclosing harm tail at 0x46dbf clears
pending again. XBE touch callback 0x27310 can therefore request another chain.
PC 0x41b5b0 / 0x439c00 / 0x41a7c0 have the same relevant sequence. Both delayed
callbacks clear pending before dispatch. Neither code path establishes a global
one-chain limit or a damage-rate ceiling equal to damage divided by repeat delay.

The current PC trace reproduces extra chains with one hero and one hazard.
Original-console damage rates are not measured. Enforcing one chain, or a
one-second per-entry grace period, needs an explicit behavioral decision; it
must not be presented as already-proven original behavior.

## Validation

The focused native test checks the production installer and retail dependency
guards, guard corruption, absent/invalid requests, exact write footprint,
partial-write rollback, and ordinary/smart/on/off combinations through the
production bridge. Run:

```
cmake --build <native-build> --config Release --target xml2_harm_loop_startup_test
<native-build>/bin/Release/xml2_harm_loop_startup_test.exe <retail-executable>
```

Runtime acceptance and the complete affected-definition audit are recorded in
the private task report. This candidate is not a completed fire damage-rate fix.
