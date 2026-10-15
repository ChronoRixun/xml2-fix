# Party-hero stun timing and reaction ownership

`[Game] StunRule=xml1` restores party-hero stun playback and recovery, and prevents a rejected or interrupted stun
from trapping a hero again after getting up.
Missing, empty or `xml2` keeps native behavior. The builder writes `xml1` and rejects an existing companion DLL
that does not recognize the key. This is gameplay rule **18**, revision **2**, enabled values `{1,0,0,0}`.
Rule 17 belongs to ObjectWorldImpact; this installer neither uses it nor registers a placeholder.

## Original behavior

XML1's ordinary stun impact does not create PC's timed `shared_stunned` powerup. The impact routine at `0x44630`
selects the stun move, derives its **animation speed**, and calls `0x40df0` at `0x44cb0`. The speed is
`clamp((authored_speed == 1 ? 2 : authored_speed) / susceptibility, 0, 10)` for positive susceptibility.
The vtable getter at `0xeadb0` reads move `+0x94`, the field parsed as `PlaySpeed`. The default replacement,
inverse susceptibility and clamp are at `0x44b6f..0x44bb5`. This dispatcher asks whether the requested move's
**override** meets the current move's **priority**. The getter is `0x1ec830` (move `+0xb4`), and the comparison is
`0xeabf0` (current move `+0xb0`). A refusal returns without calling cancel, playing a move, entering a handler,
setting a stun flag or saving a pending request. There is no stun duration counting down behind that refusal.

The default move constructor `0xe2220` initializes both priority fields to zero; the parser `0xe2610` reads
`priority` and `priority_override`, mapping pain to four and uninterruptable to seven through `0xe1680`.
The original shared stun move has the zero default override. Thrown/landing moves with priority four or five
therefore reject it. The plain get-up moves have zero priority: a **new hit during get-up may enter stun
immediately**. The original does not grant blanket stun immunity throughout recovery.

The registered `ch_stun` vtable is `0x3d7e3c` (registration at `0xe88fc`). Entry `0xda2b0` records the handler
clock via `0xe6d80` and sets actor `+0x54d` bit zero. Exit `0xda350` clears it. Cancel `0xddcc0` forwards to exit.
Move wrappers `0xe1740`, `0xe1700` and `0xe1720` forward entry, exit and cancel, respectively. An accepted higher
priority reaction calls the old move's cancel through `0x40df0`; consequently an interrupted stun cannot resume
after a later get-up.

Input dispatch `0x43650` first runs the earlier action consumer, checks actor `+0x54c` lock bit zero, and requires
a current move. With no already-pending chain (bit five), it calls that move's input handler. Stun's handler
`0xda2d0` requires a living actor and selects idle on attack/smash press bits four/five (`0xe6f20`) or an elapsed
handler clock. The outer lock still applies: this does not establish unrestricted instant button escape.

## PC differences and hooks

PC impact processing `0x435bf0` creates the stun powerup. Its entry `0x549fc0` sets actor `+0x765` bit zero
**before** requesting the reaction at `0x54a11f`. Dispatch `0x432030` can refuse it, while the flag remains until
powerup cleanup `0x54a1e0`. An interrupted stun also keeps the flag. Idle selection at `0x4ee010` then redirects
the hero into stun after get-up. The PC priority comparison `0x500650` itself is not the lost flag cleanup.

Revision 1 restored rejected/interrupted reaction ownership, while retaining PC's accepted-stun timing. That
was incomplete: both common stun clips are 3.5 seconds, but XML1's default speed 2 makes the move finish in
1.75 seconds. PC requests the default speed 1 and ordinarily forces idle through a two-second powerup expiry.
This explains a bounded duration difference; it does not establish complete encounter aggression parity.

Four guarded sites restore the original behavior for party heroes:

- Wrap the stun powerup's call at `0x54a11f`. Restore XML1's speed argument using the native refreshed
  susceptibility cache at actor `+0x520`, forward the request once, then clear bit zero if the actor is not in
  the stun move. Priority refusal still has no deferred reaction flag.
- At the resolved current-move assignment `0x41d8ac`, clear bit zero when the new move is not stun. The bridge
  preserves the registers and flags and executes the displaced assignment before resuming at `0x41d8b2`.
- Wrap the move-input call at `0x4341de`. Its native caller still checks the input lock, current move and pending
  chain. For the resolved party stun only, reproduce `ch_stun`: require a living actor, then an attack/smash
  press (input `+4`, bits 4/5) or a positive elapsed handler clock. Select the current move's idle action with
  `0x427c80(actor, 0)` and let native chaining perform the transition. Other moves and enemies retain their
  own virtual input callback. This does not permit button escape while the native input lock is set.
- At `0x54a215`, skip the party actor's flag clear and forced-idle portion of powerup expiry. Cleanup of that
  PC powerup must not end a move-owned stun, especially a later refresh or a rate clamped at 10. Enemy expiry
  remains native. The callback's missing-victim handling is unchanged.

When the replacement input handler selects another move, it clears the flag before that move's entry.
PC enters the new handler before its final current-move assignment; clearing only in the setter lets idle's
entry redirect back into stun. This ordering was reproduced with the actual idle-selector branch and corrected.

Party membership uses entity `+4` bit 20 and controller `+0x3d4` in 0..3, consistent with the native party
collector `0x46d460`; it includes AI followers and remote players, and excludes unseated enemy doubles.
The new/current move is compared with `0x427ca0(actor, &0x78a934)`, the same actor-specific resolver used by
native expiry. The move parser `0x4f6b80` does store the interned name at `+8`, but name equality is not a
substitute for resolution: `0x501a50` checks applicability and can recursively select an alternate name.
The setter's full `0x41d890..0x41d8b2` context is guarded, including null-to-idle resolution and null refusal.
Its bridge resumes with an indirect jump after restoring registers and flags.

The hook adds no timer or get-up invulnerability. Native powerup creation, lifetime calculation and affecters
remain, but the powerup no longer owns party reaction expiry. Accepted fresh hits can still restart the stun,
as in XML1. Knockback, get-up animation speeds, NPC power decisions and enemy stuns are unchanged.

## Installation and validation

All eighteen guards pass before any of the four writes. `xml1_hook_transaction` acquires pages, writes, reads back, flushes
and rolls back on failure. One failure line identifies refusal. A failed requested install is unverified for
online agreement; unsafe rollback/protection failure refuses startup. Absent/`xml2` omits rule 18, preserving
the released LC OFF digest. An explicit opt-in also enlists agreement outside LC. No protocol or save-format change.

`xml2_stun_rule_test [retail-executable]` exercises the production installer, every guard refusal and the
installed bridges with synthetic actors: acceptance, refusal, repeated stun, interruption, all four controller
slots, default/explicit/clamped rates, native lock and pending-chain gates, pressed versus held buttons,
alternate resolution versus name equality, native clocks, unrelated flag bits, enemy bypass, and preserved
registers/flags. Every byte of each guard is corrupted in turn to verify refusal. The optional executable
is read only to verify signatures and execute its stun-powerup entry/expiry in the owned fixture: 56 combinations
of susceptibility, fresh/refresh, party/enemy and competitive/noncompetitive mode compare native and installed
activation bytes, assigned powerup lifetime, request counts and expected rates. Expiry retains party reaction
state and preserves native enemy cleanup. The retail input caller and idle-selector branch reproduce the late
flag-clear failure before exercising the fix. The retail actor resolver and alternate traversal also run
against synthetic style tables, proving that the installed bridge follows actor-specific pointer identity.
`xml2_gameplay_agreement_test` checks on/off, failed install, revision
mismatch with revision 1 in both directions and rule-17 registration in either order. Builder tests check key ownership, removal, and
release-DLL capability refusal. Live acceptance evidence is recorded separately from these synthetic checks.
