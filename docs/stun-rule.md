# Party-hero stun reaction ownership

`[Game] StunRule=xml1` prevents a rejected or interrupted stun from trapping a party hero again after getting up.
Missing, empty or `xml2` keeps native behavior. The builder writes `xml1` and rejects an existing companion DLL
that does not recognize the key. This is gameplay rule **18**, revision **1**, enabled values `{1,0,0,0}`.
Rule 17 belongs to ObjectWorldImpact; this installer neither uses it nor registers a placeholder.

## Original behavior

XML1's ordinary stun impact does not create PC's timed `shared_stunned` powerup. The impact routine at `0x44630`
selects the stun move, derives its timing argument (the default value is two, adjusted by stun susceptibility and
clamped to 0..10), and calls `0x40df0` at `0x44cb0`. This dispatcher asks whether the requested move's
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

## PC difference and hook

PC impact processing `0x435bf0` creates the stun powerup. Its entry `0x549fc0` sets actor `+0x765` bit zero
**before** requesting the reaction at `0x54a11f`. Dispatch `0x432030` can refuse it, while the flag remains until
powerup cleanup `0x54a1e0`. An interrupted stun also keeps the flag. Idle selection at `0x4ee010` then redirects
the hero into stun after get-up. The PC priority comparison `0x500650` itself is not the lost flag cleanup.

Two guarded sites restore that ownership for party heroes:

- Wrap the stun powerup's call at `0x54a11f`. Forward all six arguments exactly once to native dispatch, then
  clear bit zero if the actor is not in the stun move. A refused request has no deferred reaction flag.
- At the resolved current-move assignment `0x41d8ac`, clear bit zero when the new move is not stun. The bridge
  preserves the registers and flags and executes the displaced assignment before resuming at `0x41d8b2`.

Party membership uses entity `+4` bit 20 and controller `+0x3d4` in 0..3, consistent with the native party
collector `0x46d460`; it includes AI followers and remote players, and excludes unseated enemy doubles.
The move's interned name at `+8` is compared with the native stun-name cell `0x78a934`.

The hook adds no stun/get-up timer or invulnerability. Native powerup life, susceptibility, refresh, damage
affecters and accepted request arguments remain intact. Existing uninterrupted stun loops are still allowed;
zero **stale** get-up redirects does not mean zero visits to idle selection while currently stunned. Enemy
powerups and move changes remain native. This is a bounded reaction-ownership restoration, not replacement of
PC's entire powerup system or XML1's input handler.

## Installation and validation

All eight guards pass before either write. `xml1_hook_transaction` acquires pages, writes, reads back, flushes
and rolls back on failure. One failure line identifies refusal. A failed requested install is unverified for
online agreement; unsafe rollback/protection failure refuses startup. No key registers verified-off in an LC
installation, while an explicit opt-in also enlists agreement outside LC. No protocol or save-format change.

`xml2_stun_rule_test [retail-executable]` exercises the production installer, every guard refusal and both
installed bridges with synthetic actors: acceptance, refusal, repeated stun, interruption, all four controller
slots, native clocks, unrelated flag bits, enemy bypass, and preserved registers/flags. The optional executable
is read only to verify signatures and execute its stun-powerup entry/expiry in the owned fixture: 56 combinations
of susceptibility, fresh/refresh, party/enemy and competitive/noncompetitive mode compare native and installed
actor bytes, assigned duration and request counts. `xml2_gameplay_agreement_test` checks on/off, failed install, revision
mismatch and future rule-17 registration in either order. Builder tests check key ownership, removal, and
release-DLL capability refusal. Live acceptance evidence is recorded separately from these synthetic checks.
