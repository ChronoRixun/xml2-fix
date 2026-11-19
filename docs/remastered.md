Remastered rendering preview (XML2 Fix 1.12.0-beta.1)
==========================

Private settings take effect on process restart:

```ini
[Remastered]
Enabled=0
Anisotropy=0
AnisotropyOnPointArt=0
MSAASamples=4
CenteredHud=0

[Test]
RemasteredTrace=0
CaptureBackBuffer=1
```

Enabled, CenteredHud and AnisotropyOnPointArt accept exact 0/1. Anisotropy accepts
0/2/4/8/16; MSAASamples accepts 0/2/4/6/8. Invalid component values disable that
component and log the key. Missing Enabled disables the profile. Missing AF,
POINT-art and CenteredHud settings are OFF. Zero MSAA means saved native FSAA.
No profile setting writes the INI or registry.

Beenox Advanced Options
-----------------------

CenteredHud currently selects the Beenox panel correction only. Gameplay HUD
anchoring, native menus/pause, dialogue box and glyphs, top overlay, maps, movies,
world projection and online menus retain their existing geometry. Dialogue
scaling is deferred pending an owner visual decision.

The guarded base Window constructor (0x621ca0) applies sx=sy=H/480 and left=-ox/s,
where ox=(W-640*s)/2, before child construction. A 9-byte detour at 0x61dcab consumes
the displaced x87 result once, preserves registers/flags/FPU state, corrects the
builder's subsequent left overwrite, replays the panel-cell load and resumes at
0x61dcb4. Modal windows using the base constructor are also covered. At 1920x1080
the canvas origin is 240 pixels.

Wide scale retains the native height reciprocal/multiply. The float origin is
biased outward by at most one ULP when needed before native integer truncation;
this prevents integer canvas edges such as 240 or 1200 rounding down one pixel.

BXIG computes X as logicalX*sy-left*sx; hit tests consume pixel rectangles. Mouse
events keep actual backbuffer pixels. Native window dispatch subtracts widget
pixel origins, then slider/scrollbar code inversely maps using the current track
dimensions. A second 640-unit inverse would break these controls. Inclusive
geometry endpoints and native PtInRect right/bottom exclusion are retained, as
is native capture/drag behavior outside the canvas. World picking is unchanged.

Draw/input adapters refresh surviving windows synchronously on the UI thread
when dimensions change. Native zero-delta move updates base/text/quad geometry
and Cycle option rectangles. ControlList's separate row metrics and padding are
rebuilt from guarded authored values 20/16 and 2*sx. No widget pointer is retained.
Exact 4:3 construction keeps retail fields/arithmetic. Below-4:3 remains outside
the centered pilot and uses retail layout. All sites/layout dependencies are
verified before one transaction publishes any hook; mismatch refuses correction.
OFF or CenteredHud=0 installs no panel hooks/trampolines. InGameOptions=0 does not
disable the independent native panel geometry correction.

Selective anisotropy
--------------------

Anisotropy opts into the classification supported by the live probe. The bridge
reads igImage +0x58 as a NUL-terminated char*, copies the complete authored path
before conversion/discard and publishes it after successful source installation.
Unnamed, unreadable or oversized names remain excluded; there is no fixed
260-byte read or invented hash. Accepted roots are Assets/tilesets/ and
Art/Models/Actors/, case-insensitive with either separator. UI/HUD/menu/font/effect
folders, atlas/glyph/sprite-sheet names, traversal and lookalike roots are excluded.
Other art, including props, stays native.

Create/source replacement/delete maintain context/full-handle/allocation identity.
Low-index reuse cannot inherit an old path. Replacement invalidates the previous
identity even if upload fails. Borrowed igImage pointers/COM references are not
retained. Engine draw wrappers establish the current context. At final D3D8 draw,
actual projection/vertex format exclude the 2D orthographic canvas and RHW draws.
The generic layer dispatcher and cached ZENABLE are not ownership tests.

Enabled-unit tables and physical stage maps are bounded; duplicate/unknown maps
refuse the draw before writes. GetTexture must match native record +0x40, retaining
the check validated by the probe's 25,780/25,780 agreement. Actual type, mip chain
and usage must admit ordinary 2D color textures. Unknown bindings/shaders stay
native. The first switch preserves authored POINT minification; the independent
AnisotropyOnPointArt=1 switch also admits named world POINT-min art. It has no
effect with Anisotropy=0/profile OFF. POINT magnification remains excluded.

Only actual MINFILTER/MAXANISOTROPY are saved, changed at draw and restored before
the caller resumes. MAG/MIP and engine caches stay native. Caps clamp the amount;
missing mips/global filtering are not forced. Ordinary draw arguments/HRESULTs
are preserved, including API failures. Partial application unwinds first.
Restoration failure disables the device generation and marks its state invalid.

An unsuitable bound texture (no mip chain, RT/depth or another texture type) is
excluded for that draw. It does not prevent the next eligible texture using the
same stage. Binding mismatches and sampler/description query failures quarantine
the affected physical stage until a successful real reset.

The transaction is visible before sampler calls and retains dirty stages while
setters are in flight. Wrapper reentry restores before a nested raw/UI draw,
disables the generation and cleans a pending outer write again. Recursive cleanup
is bounded: an unclean nested draw returns D3DERR_INVALIDCALL rather than drawing
with temporary AF. This exceptional refusal does not promise every nested HRESULT
under an arbitrarily recursive wrapper. Real successful Reset/recreation rebuilds
caps/state/generation; unchanged-reset skips retain generation. Loss stops AF;
recovery uses current bindings. Vtable classes retain separate verified originals.
OFF returns before new hooks, allocations or sampler queries.

MSAA, capture and diagnostics
-----------------------------

Existing applied-MSAA observation/status remains: actual backbuffer/depth
descriptors are queried/released after real successes, descriptor values win,
failed queries show unknown and loss shows unavailable. Requested FSAA/registry
values remain separate; native OFF request/fallback policy is retained.
CaptureBackBuffer=0 preserves MSAA and InputPipe input/state commands, registers
no capture callback and rejects screenshots with `capture disabled; MSAA preserved`.
NoActivate changes shell behavior independently of sample count. API state does
not certify driver image quality.

RemasteredTrace=1 also requires Enabled=1/CenteredHud=1. C0 retains bounded
256-record/16-per-boundary observations at existing adapters and applies no
additional transform. Only actual backbuffer descriptors supply its canvas;
records do not prove complete HUD/map/queue ownership.

Offline checks
--------------

Build Win32 Release at Idle, serial MSBuild /m:1 /nr:false. New tests:

- xml2_remastered_1c_rules_test: eight aspects, 4:3 math, inverse/edges, paths/defaults.
- xml2_remastered_ui_test <retail.exe> [all|off|guard0..guard15]: retail
  move/Cycle/ControlList code, builder ABI/FPU stack, actual PtInRect/Slider
  arithmetic at every aspect, reset relayout, atomic refusal, untouched dialogue.
- xml2_texture_filtering_test: caps/path/POINT rules, failures and reentry during
  apply/draw/restore, including bounded persistent recursion.
- xml2_texture_filtering_runtime_test <libIGGfx.dll> [all|guard0..guard7]: relocated
  guards/full strings/handle lifecycle, four draw slots, binding, 2D exclusion,
  POINT opt-in, restoration/loss/reentry and distinct vtable classes.

Existing device/options, centered math, focused menus, native/startup, guard,
OFF-image and installer/union gates remain required. run_native.py accepts
optional XML2_FIX_TEST_GFX; otherwise it uses XML2_FIX_TEST_GAME_DIR/libIGGfx.dll.
Offline tests do not certify visual fit, physical mouse/DPI behavior, real loss,
wrapper quality or complete game ownership. Follow the task report's live matrix
before promotion.

Stable 1.11.1 integration
-------------------------

The profile remains opt-in on the stable 1.11.1 online line (rebased from 1.11.0). Its master switch is OFF
when absent; AF, POINT-art AF, centering and tracing also default OFF. With the
master enabled, the existing MSAA request default is 4 samples. The DLL release base is 1.12.0; the intended package is 1.12.0-beta.1.
The released gameplay-agreement and LC content identities are unchanged.

The online screens retain their own drawing and 512x384 input mapping. The
Beenox correction changes only its native panel adapters; the online and font
adapters add bounded diagnostics only. Online menus verify the complete retail
Advanced Options renderer before the later MSAA status CALL is installed.

Present keeps the join-hold overlay and network notice before capture, then
calls the device's original Present. Linked frame pacing and capacity sampling
still run once after a successful Present; a failed Present does not advance
that callback. Reset keeps per-device originals and context, with temporary
surface references released before returning to the engine. Matching Legends Classic 0.6.1
menus/fonts/resources remain a separate companion requirement.

The combined fake-device fixture checks overlay/capture/original/pacing order
with the profile both OFF and ON and with successful and lost-device results.
Live driver recovery, physical input/DPI, visual quality and multiplayer
determinism still need the combined acceptance round.

Multiplayer policy
------------------

Remastered ON/OFF peers may play together when their existing gameplay and
content agreement matches. Device presentation/MSAA, temporary sampler state
and native Beenox panel geometry are local rendering choices. The profile
changes no assets, input-frame records, synchronized RNG, simulation clock,
camera/world projection or gameplay-capability entries. Device creation retains
FPU_PRESERVE, and panel/classification arithmetic preserves caller FP state.
Present preserves the released linked-frame pacing and successful-frame callback
order. There is therefore no new agreement rule for this visual profile.
The live ON+ON, mixed-peer and late-join checks remain acceptance requirements.

A Remastered page in Ultimate Legends is planned (not released yet); until then
set these keys by hand. That page is to edit only [Remastered] keys, validate every
requested value before writes, preserve comments/unrelated settings and require Fix
1.12.0 or newer. Its CenteredHud label is "Centered Beenox menus": this switch
does not scale dialogue or the gameplay HUD. Internal resolution scaling and
bundling a D3D wrapper require separate owner decisions.
