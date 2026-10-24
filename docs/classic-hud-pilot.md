# Opt-in classic HUD

`[Game] ClassicHud=xml1` enables a presentation-only adapter for matching
builder-generated assets. The default is off; absent, `0` and `off` make no HUD writes. The retired
`ClassicHudPilot` key is ignored. Unknown values, an unsupported
image base, a missing asset contract or a retail-byte mismatch retain the stock
HUD and log a refusal. This is an experimental companion to the builder's
`xml1build.classic_hud` module, not a released complete HUD skin.

The installation uses one guarded transaction with eight four-byte pointer writes
and one five-byte call redirect:

| Retail site | Expected pointer | Replacement role |
|---|---|---|
| `0x69dcf0` | `0x59a140` | HUD primitive callback; scoped hero fills and target frame/slant |
| `0x69dc60` | `0x5ec560` | UV callback, only the three HUD sprite batches |
| `0x59a53a` | `0x69dc08` | Private composite HUD atlas resource name |
| `0x5a4376` | `0x69dec8` | Primary panel model resource name |
| `0x5a438f` | `0x69deac` | Secondary panel model resource name |
| `0x5a5bcf` | `0x69df2c` | Classic party-selector/power-wheel emblem model resource name |
| `0x69db98` | `0x5a0bb0` | Append the small-map chrome after native icon drawing |
| `0x69dc78` | `0x5a70a0` | Scoped power-wheel art and per-player input labels |
| `0x5a904f` | Call to `0x5f1130` | Fit the primary target title with native font measurements |

Dependency guards cover the native queue and UV helper, six hero-fill call sites,
four target-rectangle calls, and the native energy slant displacement at `0x69de88`.
Every destination is verified before writing, with readback, rollback and page
protection verification supplied by `xml1_hook_transaction`. An uncertifiable
rollback refuses startup rather than continuing with a partial adapter. No game
executable is changed on disk.

Health, energy, maximums, clamping, delayed damage fractions, pulse timing,
level/XP numbers and target lifecycle remain native. Only fill art, slant and
energy alignment change. The target name and subtitle renderer, including
`ResistanceDisplay=xml1` at `0x5a8dec..0x5a8e14`, are not patched. Hero fill
brightness/alpha carries the native pulse over the original gradient. Fonts,
controller ownership and gameplay/save rules are unchanged; no gameplay-agreement
rule ID is allocated.

The private atlas supplies eight additional sprite IDs (32–39); these are HUD
draw IDs, not gameplay-agreement rules. The native sprite queue count is unchanged.
Resource lifetime stays with the original UI resource manager and teardown.
`[Test] ClassicHudTrace=1` logs sampled primitive contracts and the first UV use of
each HUD batch/icon. It is not needed in normal play.

`xml2_classic_hud_install_test <owned-retail-executable> <mode>` checks the real
installer against a private image fixture. Modes are `on`, `off`, `invalid`,
`zero`, `named-off`, `legacy`, `base`, `assets`, `alias`, `stale`, and `guard0` through `guard43`. Fixtures contain synthetic asset
markers and never execute the game. Follow the local isolated-desktop test-runner
requirements and bracket any registry-sensitive suite as usual.

The explicit emblem model occupies the PC party/power-wheel frame role.
Complete wheel and multiplayer composition therefore need separate acceptance;
this option must not imply that importing one mesh establishes all HUD states.
The builder registers all private resources and the `lc-hud.txt` marker
(`LC-HUD-4`) in its normal output manifest. Both `_pc` model aliases are required.
Complete state and multiplayer acceptance remain release gates.

The target adapter recognizes the native 224x24 selected-target and 48x12
overhead-bar contracts. It retains their centres when removing slant and fits
the native black/pulsing interior and unchanged-width fills inside the new
border. Separate masks preserve the bevel without covering it with a rectangular
interior. Unknown sizes keep native target geometry and log once in trace mode.

The measured target call continuations are `0x599361` (outer), `0x599435`
(interior), `0x599534` (trail) and `0x5996a7` (fill). Removing slant moves the
outer origin by half the native skew; the interior uses a one-unit inset and
the unchanged-width native fills use four units. The synthetic installer suite
also checks these centre/containment contracts and excludes the resistance-text
call region from adaptation.

The paired builder places the imported panel face at local Y=-6. The native
parent adds 10 and the controller-colour rectangles subtract 5, leaving the
face in front of those rectangles while player-colour tags remain visible.
This is a model-data correction, with no additional hook or palette allocation.
Both target and mirrored two-player panels were checked at 4:3 and 16:9. Full
family acceptance still requires revival, all party/player slots, complete
target lifecycle, original multiplayer references and integrated build validation.


Target fills and damage trails use the explicit white coverage masks as well as
the interior. A native point-UV solid sample on the composite atlas could vanish
after a menu/load round-trip even though the queued width and red tint remained
correct. Explicit coverage restores the visible fill across that sequence without
changing health, fill widths, pulse colours, depth, or resistance selection.
Trace mode keeps separate hero/target budgets and records position and tint.
The lower-level cause of the point-sample failure is not yet established.


Counter sprite IDs10/11/12 use the original health-pack, energy-pack and currency
cells already present in the private atlas. Mapping requires the expected native
32x32 UV cell; foreign layouts fall back to the PC quadrant. Map arrow13 remains
native. The adapter does not change counts, capacities, pickup or use behaviour,
text, or input. Counter zero/99 fixtures are staged; use is tested with real pads.


The measured HUD prompt-background call at0x59e144 (continuation0x59e147) uses
channel1/icon27 after the native text service expands input tokens and measures
all lines. A dependency guard protects that call; bounded positive rectangles
receive classic bevel art with their native alpha and size. No text, glyph,
controller detection, font slot or input binding is replaced. Keyboard/pad
switching and stacked prompts were verified at4:3 and16:9. The installer suite
also corrupts this dependency independently (54 total modes).


The small-map callback calls the original0x5a0bb0 unchanged, then appends one
224x224 frame at the native centre(0x8a61dc/0x8a61e4). It requires mode1 at0x8a61c4,
the native visible flag(+0x18), loaded geometry(+0x30) and the expected HUD manager.
Off, overlay, pause-map and no-map states receive no frame. Its atlas sprite38
has a fully transparent interior. Native terrain, fog, world transforms, marker
art, save data and map toggles are unchanged. The small-mode horizontal inset
uses the same112-unit half-extent, keeping the frame inside the native safe edge.
This encloses the eight-cell terrain window at scale.05, including cell
quantization and rotated edge markers; the prior48-unit half-frame did not.
Only small-mode inset operands0x5a023f and0x5a0717 are redirected. Large/no-map
placement is retained. No additional resource object is
allocated; the normal bounded sprite queue owns the one extra quad.

The frame is an owner-requested adaptation of the original HUD's bevel style.
Original small-map reference captures were unframed; this is not an imported
terrain map or a claim of a pixel-identical original map-frame composition.
Read-only guards cover the map entry, visibility/geometry fields, mode, centre
stores, both small-mode inset instructions, radius, scale, icon size and cell
span, in addition to the map vtable destination guard. Every new dependency is
independently corrupted by the isolated installation fixture to verify refusal.


XP continuation0x5a3f5e receives sprite39 only for the native channel0/icon27,
height2, width0..73 and finite fraction0..1 contract. Original silver artwork
is supplied by LC-HUD-4. Presentation changes to three-pixel height and removes
the slant displacement; native XP math, width, fraction, alpha and level digits
are retained. Guards0x5a3f5b,0x69de8c,0x5a3f1e cover the call, full width and
height. XP adds three dependency guards; before the wheel there were24 dependencies plus
7 destinations. The stale contract3 refusal is tested.


The power-wheel callback69dc78 (retail5a70a0) keeps native entries, assignments,
icons, locked state, fades and hold/release behavior. An ordered context mirrors
the native32-entry validity loop; only power entries1–4 receive silver classic
rings and input labels. Quick entries and other HUD rings retain their roles.
The labels use the existing pad_prompts service's current row0 binding and device
for that player. The helper returns no label if that service was disabled or
refused installation. No input binding, console glyph bitmap, font or palette
slot is introduced. Locked labels sit inside their rings; unlocked labels leave
the power artwork visible. The text queue getter4bdfa0 and enqueue5f11b0 are
guarded, as are the player field, entry loop, validity branches and ring call.
The wheel brought the contract to eight four-byte writes and 36 read-only dependencies.
The target-title fit adds one call redirect and eight read-only dependencies:
nine destinations, 44 read-only dependencies and 63 installer modes in total.

Target validation uses ordinary power aiming: the human path at0x42fca0 updates
actor+0x3e0;0x46f7ef forwards that handle through0x59ac20. Ordinary damage alone
is not proof of a selected target. Acquisition, different-target selection and
loss were exercised at4:3 and16:9 without target writes. Boss combat was tested
after explicitly staged approach setup. Authored physical resistance and energy
immunity subtitles remain visible; the immunity target setup was staged.
The primary target title is fitted within 216 HUD units inside the 224-unit frame.
The native font manager measures each candidate at the existing font and scale;
titles that already fit are unchanged. Long names receive `...`, retaining the
native numeric level suffix. The queue copies the result synchronously; literal
percent signs are escaped for its formatter. This legacy wrapper does not forward
ordinary varargs correctly, so the adapter supplies a literal format string.
The title call at `0x5a904f` alone is redirected; subtitle/resistance drawing at
`0x5a90df`, target selection and all health/combat values remain native. New guards
cover the title alignment/scale, queue, font preparation, manager, measurement,
measurement vtable, channel stride and selected-font field. A mismatch refuses
the entire HUD transaction, including the previous eight destinations.

The classic panels and wheel labels were exercised in a real local session
with four virtual pads at960x720 and1280x720. All four living player-owned actors
moved through independent pad input. Setup used copied/staged party checkpoints;
XP and auto-spend were staged for the all-unlocked wheel. All sixteen power
icons rendered, and player1 keyboard labels differed from players2–4 pad labels.
Actual Xtreme input ran its animation and consumed the native counter1→0 at
both aspects. The separate charge-strip chrome remains native. These checks
do not establish online play or original four-player layout parity.
