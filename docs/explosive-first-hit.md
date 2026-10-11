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


## Multiplayer agreement

The release identity (for example `LC24`) is unchanged. LC games, and non-LC games that explicitly
set `ExplosiveFirstHit`, require an independently guarded gameplay agreement. Ordinary XML2 with
no opt-in retains its native protocol. The installer captures the effective rule only after the
branch patch succeeds and verifies; a requested but failed installation cannot join, even as an
apparently matching off peer. Once patched, a later INI change cannot relabel the running code.

The native host-info preflight (opcode 0x1a, 48-byte body), client join request (0x18, 116 bytes),
and host participant reply (0x1f, 80 bytes) carry a tagged revision-1 installed-rule word in their
reserved compatibility field. The release string and existing fields stay native.
The request uses body +0x64; both replies use +0x20. Preflight sender 0x60778e stamps the session
descriptor word +0x408 before sending it; request/reply writers are 0x607cf1 / 0x6083b9.
The preflight follow-up (opcode 0x1b) retains its original eight-byte body and appends the rule
word at +8, making its body 12 bytes. The CALL at 0x609a5d wraps native send 0x610f30 with
owned header/body storage, which the native queue copies synchronously. The receiver registration
at 0x609dec validates decoded length 12 before reading the tag. Older eight-byte replies cannot
bypass agreement through this alternate admission path. The native parser exposes the decoded
body length at event +8; reliable envelopes normalize that length before dispatch.

Registered receivers at 0x6096a3, 0x609cc3, 0x609dec and 0x609ee2 use wrappers that validate
before calling the original handler. Missing/unknown tags and different effective rules refuse
admission and log a clear reason. This includes older DLL peers with the same release identity.

All thirteen retail guards must match and every code page must become writable before any handshake
write. Branch targets, padding, registered callback pointers and cache flushes are verified.
The existing socket exclusion gate stays closed until both this agreement and the explosion
installation status verify. Failure preserves offline use when the exclusion hook itself works;
if exclusion cannot be installed, startup refuses rather than permitting unsafe online access.

`xml2_gameplay_agreement_test` links the production installers/callbacks to isolated providers.
It covers both mismatch directions, failure on either peer, both on/off, actual effective-state
reporting, later INI drift, legacy/unknown peers, invalid bodies, guarded refusal, real writer ABI
and unchanged release/register values. It executes the patched follow-up CALL and refuses legacy
eight-byte follow-ups even with a matching word beyond that length. Production version/socket
gate tests use a fake IAT provider and fake socket, verifying pre-bind closure, failed binding,
successful binding, explicit non-LC off configuration, vanilla no-key behavior and exclusion
installation failure. No game or network traffic is needed. Live direct-IP/LAN
and lobby join checks, including native UI refusal behavior, remain an integrator acceptance item.
