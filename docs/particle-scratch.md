# Particle scratch

`[Limits] ParticleScratch=40` bounds all eleven particle operations to the first
40 records. The manager keeps 40 shared `igParticle` scratch records of 0x90 bytes
at retail +0x470 (ModelPools relocates that offset). These are shared working
records, independent of the number of emitters.
Retail trusts each array's virtual `getParticleCount` and can overrun them.

The clamp processes `min(count,40)` particles for each operation. Higher particles
skip that operation. Scratch capacity and ModelPools layout stay unchanged.
Absent, `0`, or `off` contributes no patch and no agreement rule. Other settings,
including `256`, refuse with an unverified rule; raising scratch is deferred until
runtime measurements justify it. A successful transaction registers rule 60,
revision 1, `{40,0,0,0}`. A failed install advertises no effective capacity and closes
online admission. Startup refuses if rollback cannot be certified.

The generated ledger hooks the virtual count call and the following complete
instruction. Each bridge saves the array address, calls the original count getter
once, preserves registers, flags, x87 and SSE state around telemetry, and replays
the displaced instruction. One transaction guards and installs all eleven hooks.
The spans avoid every ModelPools patch and source guard. Installation follows
ModelPools; scratch addressing remains owned by ModelPools.
The key requests the shared gameplay agreement before the startup transaction,
including when it is the only opt-in key.

`PARTICLE_SCRATCH` on the test pipe returns installed/requested state, capacity,
number of logged arrays, and each operation's VA, maximum observed array count,
and clamp count. Counters are atomic for the pipe reader. An overflowing array
address logs once across all operations and frames during a process session.
Address reuse shares that suppression entry. These counters can be consumed by
the separate pool diagnostics row without installing another observer at the hooks.

Verification (no game entry point is called):

```text
python -B tools/generate_particle_scratch_sites.py <retail-exe> --check --verify
python -B test/particle_scratch_sites_test.py <retail-exe>
cmake --build build --config Release --target xml2_particle_scratch_test
build/bin/Release/xml2_particle_scratch_test.exe <retail-exe>
```

Appending `--remove-guard` to the fixture command deliberately restores the first
retail count call. That negative control must exit 1 at scratch offset +0x1af0.

The fixture executes every retail operation with invented particle, math and
collision services at -1, 0, 1, 39, 40, 41 and 300 particles, with ModelPools stock,
x2 and x4 plans. It compares scratch and array output against retail operating on
the first 40, checks canaries, and removes each bridge separately to prove the
overflow is detected. It also checks every guard byte, OFF image identity,
rollback, invalid configuration, agreement and pipe parsing. Actual runtime peaks
and visual impact above 40 remain unmeasured; no game launches are required here.
