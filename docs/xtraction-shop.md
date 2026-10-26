# Xtraction services adapter

`[Game] XtractionShop=1` enables a guarded option hook in the full native Xtraction menu.
The default is `0`. A companion builder calls `extractionPointServices(owner, services)`
(`n(ai)`) with the services this point offers. Native `extractionPoint` calls offer none.
The script table is installed with `[Game] ForcedTeams=0` or `1`; `xml2fixFeature("xtractionshop")`
(1.4.0) and `xml2fixFeature("xtractionservices")` (the bitmask below) report function availability,
independently of whether the option hook is enabled. A builder that passes the bitmask checks
`xtractionservices`, so an older DLL keeps the native menu instead of reading the mask as a Forge permission.

`services` is a bitmask; 0 and 1 keep their 1.4.0 meaning:

| bit | option (string id) | added when | runs |
|---|---|---|---|
| 1 | Forge (2005) | the bit is set (the builder: the point's Forge flag and the contact signal) | `shopMenu('equip')` |
| 2 | Healer (2004) | the bit is set and `healer` is in the campaign | `shopMenu('item')` |
| 4 | Load (2006) | the bit is set (the builder: every first-game point) | `saveloadProcess(3)` |
| 8 | Danger Room (2003) | the bit is set and `storm` is in the campaign | `dangerRoomMenu()` |
| 16 | (Forge's rule) | with bit 1: Forge also needs `forge` in the campaign, the first game's test (without it, 1.4.0's builder permission alone) | |
| 32 | (no Change Team) | a lite point whose first flag is false: the native Change Team is skipped (the Xtract skip's rel8 at 0x4a6c7b points past it, to the Save add 0x4a6cf0, for this menu only) | |

## Online play

The first game had no network play, so the online rules are the port's. The point menu belongs to the hero who
opened it; every peer builds it from its own script run and its own registry, and the opener's input picks an
index (two-instance test: both peers logged the same build in the same second). XML2's hub Danger Room terminals and
shops (`dangerRoomMenu()`, `shopMenu`) open online for host and joiner alike, so Danger Room, Healer and Forge stay
selectable when the campaign has them. While more than one player is connected (the net session's online byte,
0x60c7e0 / +0x64, and the player manager's count, 0x612be0 / +0x278):

- Load is added greyed: loading would replace the shared session on one PC only. This mirrors what XMen2.exe's
  pause menu was seen doing with two players (its Load Game is skipped; the code behind that is not identified -
  CSaveLoad reads the same two values only in its save mode, 0x4aebb6-0x4aebde).
- Danger Room, Healer and Forge are added greyed, not left out, when this peer's campaign lacks their character: a
  joiner's campaign bits are not proven to equal the host's in every case (zones without the catch-up unlocks,
  mixed zone scripts), and a list that differs between peers would run different choices on different PCs.

Options are never removed online, so every peer keeps the same option positions. The option set and the revival price
are rule 10 of the shared gameplay agreement ([gameplay-agreement.md](gameplay-agreement.md)).

## Revival price

`[Game] RevivalCost=xml1` jumps over 0x4b8830's first instruction to the first game's price, 200 * G * G, where
G = min(5, L / 10 + 1) is the progression group of L, the highest current level on the registry's roster (0x449960;
default.xbe 0xaf6b0 reads the group that registry vt+0x58 = 0x54150 makes from vt+0x50 = 0x54c30's highest level).
So a revival costs, whoever fell:

| Highest roster level | 0-9 | 10-19 | 20-29 | 30-39 | 40-45 |
|---|---|---|---|---|---|
| Revival (Tech Bits) | 200 | 800 | 1,800 | 3,200 | 5,000 |

Retail is max(200, 2 * level^2) of the revived hero. XML2 Fix 1.5.0 to 1.8.0-beta.7 charged 200 * L * L (80,000 at
level 20): its notes took vt+0x54 = 0x54cb0, the highest acknowledged level, for the group slot. 1.8.0 charges the
group price and registers rule 10 as revision 3 whenever the key is set, so a fixed and an older PC refuse each other
naming `10 Xtraction services/RevivalCost` instead of debiting different amounts. Guards: the function's 26 bytes,
0x449960's prologue and its loop (guest skip, running maximum), the registry getter. Both games count the whole
playable roster (filled at the stats load, joined or not) minus guests; an empty roster is level 0 (200). The first
game stores G at zone placement, roster acceptance and the shop; the hook works it out at each price, so a level-up
mid-zone changes the price at once (on every peer alike).

Options are appended in the first game's order (Load, Danger Room, Healer, Forge) after the native Save.
"In the campaign" is the hero registry's per-save character bit (registry `0x44b8f0`, vt+0x3c name lookup,
vt+0x84 `0x44b800`), the bit the first game tests with its campaign registry (default.xbe `0x56df0`), set by the
same first-activation rule for hero-team actors (XMen2.exe `0x4bc990`, default.xbe `0xb22d0`) and by
`unlockCharacter`. The first game's points (default.xbe `extractionPoint` `0x9f110`, `extractionPointLite`
`0x9f4a0`): Save and Load always; Danger Room, Healer and Forge on the campaign checks above, a lite point also on
its second, third and fourth flag (its fifth is never read: `0x9f557` reads the fourth again).

The hook replaces only the destination of the getter call at `0x4a6d33`, immediately before
the dialog finalizes through vt+0x68. It reads labels from the build's own strings. Byte guards
cover the native Save option's calling convention, the finalize call, both singleton getters and the
registry's two slots. A mismatch installs nothing. Save, Change Team, and the world-map switch keep
their existing behavior. The point's services are restored after the native handler returns.

XML2 has one shop UI (`shopMenu` ignores its argument), so Healer and Forge open the same shop; the
first game's separate stock (Healer: packs and the Grab Bag; Forge: equipment) is not rebuilt here.

Synthetic and retail-byte checks: `xml2_test --xtraction-shop-rules` and
`xml2_test --script-rules`. `XML2_FIX_TEST_EXE` may point at a legally owned retail executable
for the read-only guard checks. The added function takes the current table to 319 of the
320 registered-name capacity; future function additions must audit that limit (the bitmask adds no function).

## Main-menu Danger Room gate

`[Game] DangerRoomGate=<item>` names the main menu's Danger Room item (the port: `button3`). After
MAIN_MENU's first update step (vt+0x10, slot `0x69f144`) the item is disabled through the menu's own
item setter (`0x5ade70`) unless the profile's character bit for `phoenix` is set (profile `0x48fed0`
vt+0x38 `0x48f770`, a map at +0x1c0). That is the first game's gate (default.xbe `0x17e93c`-`0x17e975`,
profile vt+0x34 `0x8c3b0`): the bit is set when a character first enters a campaign on the profile
(default.xbe `0x552de`; XMen2.exe `0x449c40`), for Phoenix on Jean's opening mansion tour. It is not XML2's
story-level-6 rule. The first game also sets the profile bits of every character in a save when it is
loaded (`0x56fe9`); XMen2.exe's load (`0x44ba30`) restores only the save's own bits, so the registry's load (vt+0xa4)
is wrapped to set Phoenix's bit after a local load - not in a network game, where the loaded campaign is the shared
session. Both vtable slots (0x69f144, 0x6854f0) go in through one install transaction (read back, both put back on
failure). When the bit is set the item is enabled explicitly too, so a main menu kept from earlier in the session
follows it. Without the key nothing
is installed. A failed read leaves the item available, with one log line.
