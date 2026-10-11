# Explosive first contact

`[Game] ExplosiveFirstHit=1` opts a first-game campaign into the original explosion attack-ID rule.
The default is 0, which preserves the PC game's authored `explosionhitsfirstvictim` switch.
The companion builder enables the key for its first-game opening and owns it during INI merges.

An explosion is a new attack, including for a victim already hit by the projectile's contact attack.
The original getter at 0x96960 always sets the explosion ID to the projectile sequence plus one
(the increment is at 0x969d9). The PC getter at 0x49ab10 copies the blast record and selects either
the sequence or sequence plus one, controlled by physical-properties byte +0xab, bit 3.

The fix guards the retail instruction sequence at 0x49ab51 and replaces only the two-byte
conditional branch at 0x49ab58 with NOPs. The native increment, output store at attack +0x28,
record copy, owner selection, calling convention and area-damage dispatcher remain in place.
A victim's duplicate-attack check therefore sees the original's new blast ID. Other victims
receive the same native blast record, and repeat delivery of that blast retains its same ID.
Integer wraparound is the native increment's wraparound. Guided projectiles use the same getter.
No entity records, placements, numbering, serialized flags or save fields change.

The installer refuses an unexpected image base or any mismatch across the guarded branch,
increment, both stores and first epilogue before writing memory. Missing/false settings do not
patch anything. The key uses the common INI switch parser. Installation is reported in the log.

This rule does not make removal or contact trigger an explosion. The original and PC explosion
routines reject inactive projectiles; their die-on-contact paths mark the projectile inactive
before removal. Visual death effects alone do not establish a damaging blast. An original entity
with `dieoncontact` but without `explodeoncontact` must not be silently given the latter flag to
make a test pass. Runtime acceptance needs a real damaging explosion and measured victim state.

## Verification

Build for Win32 and run `xml2_explosive_first_hit_test`. It links the production installer against
isolated settings/logger providers, executes the native attack-ID tail in its own fixture section,
and checks the default, explicit flag, refusals, exact two-byte patch, untouched record fields
and unsigned overflow. The companion builder has synthetic INI ownership/default checks.
Game-dependent damage and activation checks belong in a private acceptance ledger, not this repo.
