# Primitives per effect

`[Limits] PrimitivesPerEffect=32` permits up to 32 primitive pointers in one effect
definition. Missing/10 keeps stock; 11..32 is accepted. An invalid value retains
stock storage and makes the requested agreement rule unverified.

Pointers 0..9 remain in the native definition at `+4..+0x2b`. Bitmap `+0x2c`,
count `+0x30`, loop-time fields and the `0x44` record stride stay native. Pointers
10..31 live in a zeroed DLL table with 22 words for each definition slot. The
table takes 45,056 bytes at 512 definitions. Slot arithmetic uses the published
effect-library base and N: `(definition - pool_base) / 0x44`. No runtime heap
allocation occurs while effects append or release primitives.

The six generated caves are:

| Path | Native site | Result |
|---|---|---|
| Append | `0x4188cd..0x4188d7` | Select native/spill destination, store pointer |
| Classification | `0x407a98` | Select primitive and replay its type comparison |
| Spawner cursor | `0x40b181` | Rebase EDI to the selected cell; its four later reads follow it |
| Spawner | `0x40b480` | Select primitive and replay the flags load |
| Release | `0x4189c0` | Select pointer and preserve the native manager call/stack argument |
| Flag query | `0x418cd0` | Select pointer and replay the native flag test |

There are eleven element accesses including the append store and the spawner's
cursor aliases. Registers and flags are preserved around address selection.
Native indices 0..9 execute the same load/store and displaced instructions with
the same registers and flags. The release count reset at `0x4189db` also clears
all 22 words of that slot's spill row. The admission comparison at `0x4188b4`
uses the configured capacity and refuses the next primitive without allocating.

The shared ten-slot bitmap helper at `0x5615b0` is guarded read-only. A DLL copy
changes its five bounds to the configured capacity; only the effect destructor
jump at `0x4186f3` and release call at `0x4189d5` move to that copy. Unrelated
containers continue using the retail helper.

W3-4's descriptor must agree with the native definition-base and mask operands;
both stock and raised descriptors work. The owner at `0x6f3c3c` must be zero
before installation. A separate transaction guards every cave span, metadata
initialization/reset, admission bound and shared helper. Storage and caves are
prepared before the first write. Only a verified transaction publishes the
effective capacity. Failed requests poison agreement; an uncertifiable rollback
retains referenced blocks and refuses startup.

Rule **58 PrimitivesPerEffect**, revision 1, is `{capacity,0,0,0}` after success.
Absent/10 adds no entry. Missing and different values refuse both ways by name.
Status reports the largest count among live definitions as
`prims/effect max m/32` (or the configured capacity).

Run `xml2_effect_templates_test <XMen2.exe> primitives` for native append,
consumer and release tests at N=220/512/2048 and capacities 11/32. Authored
definitions request 10/11/31/32/33 primitives, including the first and last slot.
The fixture compares every native element path below ten, checks spill/canaries
and release clearing, and compares the helper copy against all 1,024 native-only
bitmaps. `prim-on`, `prim-raised`, `prim-stock`, `prim-invalid`, `prim-built`,
`prim-guard`, `prim-allocation` and `prim-protect` exercise the production
installer. No game or engine session starts.
