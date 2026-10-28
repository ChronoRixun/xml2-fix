# Scoped online fonts

`[Game] LegendsClassicFonts=1` enables a presentation-only capability. An absent,
zero or invalid key returns before hook allocation, guards or game writes. This adds
no gameplay agreement rule and changes no capability wire bytes.

The font manager returned by `0x597c20` moves to DLL-owned storage: eight slots
of the original `0x1c18` stride, followed by the relocated counter/current fields.
Indices 0..3 retain medium/big/HUD/digital. `lc_title`, `lc_row`, `lc_body` and
`lc_label` select 4..7. The unchanged metrics loader at `0x596af0` and all index
readers at `0x5971a0..0x5972a0`, width paths `0x596df0` and `0x597c90`, use the
returned manager. The constructor's slot count, getter's absolute roots, map
loader's counter/current offsets and eight-name bounds are patched together.

`0x598142`/`0x598157` select separate `lc_fonts_pc`/`lc_fonts_hd` maps. The
builder's original maps and classic metric/atlas pairs are byte-identical. Both
private maps and every private metric/atlas file must exist before installation.

`0x5d7e79` reads the existing font attribute plus private-style `textscale` into
a side table keyed by style pointer. Retail case-insensitive comparison is kept:
`big`/`BIG`/`BiG` stay 1, other retail values stay 0.
Only ready private slots admit scale; non-finite/out-of-range/trailing text falls
back to 1. The style record's size and meaning of its other fields are unchanged.
`0x5c636e` and `0x5c62f5` supply the same scale to drawing and clipping.
Glyphs are drawn in back-buffer pixels (the classic fonts are the same pixel
size at 720p and 1080p) while every menu position is in the 512 x 384 view, so a
private style's `textscale` is in view units and the pushed scale is
`textscale x screen height / 384`, read from the engine's screen height at
`0xa0a000` (its store `0x5fad5c` and its reader `0x5fa066` are guarded). The
log line names this `lc-fonts-screen/1`. Classic styles keep scale 1.

`[Online] Menus=1` needs this capability: when the font transaction did not
commit (key off or refused), the co-op screens are not installed and Play
Online stays the game's own menu (`online menus: [Game] LegendsClassicFonts is
not active ...`). The
text-item reader at `0x5c6c30` remains unmodified and is region-guarded.

The `0x5c61d0` entry wrapper enables prompt glyph selection only during rendering
of a private-font text item, including its width/clipping work. The existing
prompt adapter still reads actual bindings and the last-used device. The private
codes are A/B/X/Y `0x80..83`, menu/pages/direction `0x84..86`, common keys
`0x90..9f`, letters `0xc0..d9`, digits `0xe0..e9`, F1..F12 `0xea..f5`, Ctrl/Alt
`0xf6..f7`, Ctrl+V `0xf9`. Unknown controls retain readable bracketed names.
Nothing substitutes private bytes into strings stored by unrelated menus.

`lc_fonts_sites.hpp` contains short site signatures and SHA256 region guards.
The common patch transaction checks all guards and region hashes, obtains every
page before writing, verifies readback, restores protection, and publishes only
after success. A certified refusal/rollback retains the original font behavior;
an uncertified rollback stops startup because continuing with mixed code is unsafe.

Run the native `xml2_lc_fonts_host <retail-exe> <private-asset-root>` test and
`scripts/check_lc_font_guards.py <retail-exe>` without launching the game. The host
executes original slot initialization/readers, metrics loading and the map-name
loop with synthetic dependencies, plus the production style and scale stubs.
Full Direct3D drawing/device changes still require the separate live procedure.
