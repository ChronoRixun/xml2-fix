# Personal-item descriptions

XML2 Fix 1.4 enables `[Game] PersonalItemText=1` by default. It corrects the personal-item menu's background
image depth so a bedroom item's description remains visible. Set it to `0` to retain the original behavior.
The image size and artwork remain the builder's responsibility.

## Cause

The text is loaded and its anchor is bound. None of the three early exits in the textbox draw at `0x5c7a00`
fails in the reproduced personal menu: hidden bit `item+0x54 & 4` is clear, `item+0x58` exists, and its type-5
rendering component (virtual `+0x18`, `0x5731d0`) exists. The upstream `item+0x55 & 5` suppression is also clear.
The original game's textbox at `0x17e1f0` requires the equivalent three prerequisites.

Instead, the personal menu inherits the image viewer's fullscreen 512x384 image and depth **-10**. Its position
getter `0x5b7c80` reads the signed short at `menu+0x1928`. The description anchor is at depth **0**; the Back/Scroll
anchors are about **-11.6**. The opaque fullscreen image therefore covers the description while Back remains
visible. The original game uses a smaller sprite window above the description, so its image does not overlap it.
Reframing the artwork alone still leaves an opaque fullscreen sprite.

A live one-field experiment changed depth -10 to +10 and exposed the already loaded description; changing it
back hid it again. No text, flag, binding, style, or textbox drawing instruction was changed.

## Patch and scope

The single factory CALL at `0x5b84e9` is redirected to a wrapper. The wrapper invokes the original personal-menu
constructor `0x5b8180`, verifies its returned object's personal-menu vtable and inherited depth, then sets that
depth to **10**, behind the text. The later menu-file loader still applies any explicit `depth` attribute.

Only the PERSONAL_MENU factory branch is patched. The shared image-viewer constructor, position getter,
textbox renderer, other menu types, artwork, and save data are unchanged. That isolation makes the fix on by
default appropriate for the supported executable; authors who intentionally want the old overlap can disable
it or provide an explicit menu depth.

Before writing any executable byte, the fix checks the factory branch, type name, constructor, inherited depth
initializer, personal position-getter slot and implementation, and explicit-depth assignment against the retail
bytes. A mismatch or unreadable code refuses the entire patch and logs the first failing guard. Failure to make
the CALL writable also refuses without writing it. Installation and disabled status are logged.

## Verification

`xml2_test --personal-item-text-rules` checks the depth field in synthetic objects, null/other-menu/custom-depth
refusals, disabled patch planning, the relative CALL destination, planning without image mutation, refusal after
every guarded byte is independently corrupted, and the guard bytes against a local executable when available
(`XML2_FIX_TEST_EXE`). The rule suite also runs in the normal test invocation.

Game acceptance uses actual interaction input after disclosed room-access staging: two different bedroom
items with the fix, the same DLL with the key disabled, and a separate image-viewer textbox menu. Screenshots,
state samples, and game-derived analysis are local evidence only and are not distributed with the source.

## Picture window (`[Game] PersonalItemPicture=window`)

XMen2.exe adds a menu's image as a sprite sized by the menu's size getter (vtable `+0x90`, `0x5b7c60`, a
fixed 512x384) and placed by its position getter (`+0x94`, `0x5b7c80`: x 0, y = the signed depth, z 384, the sprite's top
edge). The first game's default.xbe adds the personal menu's picture at 256x128, x 128..384, z 202..330, in front of the
menu IGB's panels (picture frame, text panel, Back bar). The fullscreen sprite hides all of those panels and is opaque
(it ignores texture alpha), so a texture re-composed around the picture could only show an enlarged copy of the art.

With the key set to `window`, the two vtable slots of the PERSONAL_MENU class (`0x69fa0c`, `0x69fa10`) point to
thunks that build the same igVec2f / igVec3f through the same constructors with 256x128 and (128, depth, 330). The
art is the first game's 512x256 texture, as it is. The text-depth patch above is unchanged and installs first (its
guard reads the position getter's slot). Four guards (the two slots, both getters' bytes, the constructor) must match
before the slots are written; a mismatch, a protect failure or an unknown value logs one line and leaves the
fullscreen picture. Absent or `stock` leaves the picture as 1.6: textures re-composed to 512x384 by an older builder
keep working with the key off.
