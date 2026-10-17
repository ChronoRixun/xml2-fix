# HAARP exterior follow-up: location and interruption limits

The exterior complaint must be localized before extending party StunRule. The original
`haarp/ext/haarp_ext01` through `haarp_ext04` spawn HAARP soldiers, security and
flamethrowers. The Brotherhood encounters in `haarp/int/icetunnel1` and `icetunnel2`
are separate maps. Their historical solo protected-hero measurements do not establish
exterior encounter difficulty or level-five party behavior.

The source/output audit on the candidate base found identical spawn counts and character
types in the four exterior maps: 18, 28, 18 and 33 authored spawn instances, respectively.
World levels remain 3, 5, 6 and 7. These are map totals, including deferred spawns;
they are not simultaneous attacker counts. Both ice tunnels retain world level 7 and
their original 27 and 20 authored spawns. No spawn reduction is justified by this audit.

A staged four-hero, level-five exterior observation engaged two level-three soldiers and
two level-three security enemies. Companions attacked and killed enemies; no stun move
was observed. The idle protected leader and different party composition do not reproduce
the reported vulnerable fight. Other games ran throughout, so its rates cannot establish
timing parity. A tutorial-paused preliminary observation is excluded entirely. No matched
original exterior observation or fixed-build before/after pair was obtained.

## Separate weapon-interruption difference

The builder's weapon conversion adds priority 5 to gun moves whose original priority is
omitted. This affects the soldier's three firing moves and the flamethrower's two firing
moves. The original move constructor defaults priority to zero; the authored Brotherhood
power priorities and security baton moves are unchanged by this conversion.

XML1's comparison at `0xeabf0` reads current move priority at `+0xb0`; PC's `0x500650`
reads `+0xc0`. Both accept a request when its override is at least that priority. Execution
of the supplied retail fragments with synthetic moves confirms that pain override 4 and
stun override 0 pass against priority 0 and fail against priority 5. This proves a difference
at the priority gate, not that every real hit reaches that gate or that it caused the death.

The original weapon handler is registered at `0xe8307` with vtable `0x3d7bb4`. Its entry
`0xe14d0` initializes the actor's weapon timer; it does not raise the move priority.
The effective emitted weapon damage agrees with the original weapon draw in the audited
MP5 and flamethrower styles. Comparing the unbound gun style's placeholder event damage
is not a valid weapon-damage comparison.

No gameplay patch is included in this investigation. StunRule remains rule 18 revision 2.
Weapon interruption and gun AI scheduling need a separately scoped restoration and
agreement decision; an engine implementation would require a coordinated new rule ID,
which is not allocated here. Before applying a conversion-wide priority change, capture
real hit acceptance against the same firing move in both engines, audit every affected
variant, and complete the required affected-room runtime coverage. The priority finding
does not establish the remaining Brotherhood power-rate cause.
