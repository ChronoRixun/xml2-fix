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
loaded (`0x56fe9`); XMen2.exe's load (`0x44ba30`) restores only the save's own bits. Without the key nothing
is installed. A failed read leaves the item available, with one log line.
