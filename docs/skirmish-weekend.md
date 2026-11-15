# Experimental Skirmish

Use the matching Legends Classic content with this fix. The campaign still
unlocks characters and competitive modes. Join everyone in the lobby before
starting a round; Skirmish keeps a fixed roster until it returns to the lobby.
The host's campaign Drop-in setting is retained and works again in campaign.

The native competitive state is Danger Room free play `0xfd` with kind 1–4.
During that state the host refuses a post-START request before participant
allocation, even when campaign Drop-in is enabled. The existing native refusal
opcode carries a small fixed-roster presentation reason, accepted only from
the proven host for the current join attempt. Broker presence uses the effective
policy. Private room codes remain resolvable so the host can explain its refusal;
public rooms stop advertising free seats during the round.

The story-spectator banner is suppressed in that competitive state. The original
seat tracker, readiness, controller ownership and restoration remain unchanged.

LC profile progress reconciles eligible heroes after a confirmed owned campaign
load and observes subsequent legitimate level recalculation. It retains historical
maximums, excludes guests and Danger Room preparation/artificial levels, and uses
the existing guarded LC profile writer. The new-game flag, campaign format and
character unlocks are unchanged. Failed or cancelled loads cannot earn progress.

No gameplay-agreement revision is added: the profile change is personal metadata,
the banner and reward fixes are presentation, and late admission remains the
existing host-local policy. No shared damage, powers, RNG, transport, actor
ownership or restoration decision changes. Rule 26 retains revision 7 and its
existing parameters. A future 0.7 beta must carry this admission behavior and
the same shared rules to interoperate; a content identity still distinguishes
different paired LC builds before play.

The native matrix covers the four fixed-roster modes and campaign policy
restoration. The mapped native UI fixture verifies authenticated refusal notices.
Real separate-PC acceptance remains a release gate; same-PC fixtures or rounds
do not establish WAN, relay, three/four-player behavior or balance parity.
