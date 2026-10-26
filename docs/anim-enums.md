# Animation enum capacity

`[Limits] AnimEnums=512` raises the animation enum capacity from 210 to 512 and
the animation-set cache from 16 to 128 records. An absent key or `0` contributes
no patch and no agreement rule. Every other value is refused and leaves online
agreement unverified. Restart the game after changing the key or sidecar.

Native IDs 0 through 209 and the native name map stay in place. The DLL owns the
expanded cache and ID-to-animation-name table. One startup transaction contributes
81 operand edits, five record-bound caves and two name lookup/registration hooks.
Every instruction is guarded against the retail executable. Publication happens
only after the common transaction commits; a certified rollback restores all
contributors, and an uncertified rollback refuses startup while retaining backing.
The native x87 duration calculations are unchanged.

The 23 XML1 names occupy fixed IDs `0xd2` through `0xe8`, in
`anim_enum_registry::builtin_names` order. The built DLL contains
`XML2FIX.ANIMENUMS.1`, a NUL, a little-endian 16-bit count, and the NUL-separated
names in ID order. `anim_enums::layout()` is null until publication;
`extension_id()` and `builtin_table()` are the consumer interfaces.

Optional base file `Data/anim_enums.txt` adds names from `0xe9` in file order.
Use one lowercase `ea_[a-z0-9_]{1,60}` name per line, with optional `#` comments.
Whitespace around a name is ignored. Native names, built-in names, duplicates,
NULs, invalid characters and more than 279 mod names are refused. Names from mod
directories are not read. The native lookup still performs its own lowercase and
63-character truncation before the extension miss lookup. Native hits win.

Agreement rule **48 AnimEnums**, revision 1, records four words: capacity with
record count in the high 16 bits, extension name count, FNV-1a-32 of extension
names in ID order with a newline after each, and layout revision 1. Requested
but uninstalled settings poison agreement. OFF peers and ON peers refuse each
other naming AnimEnums in both directions. Animation content remains covered by
the existing content fingerprint; this patch assigns no release identity.

`[Test] AnimEnumTrace=1` logs registration and each unknown `ea_` lookup once.
The test pipe status includes table occupancy, extension count and observed cache
peak. The DLL adds no clips or mappings: the companion content build supplies them.

The shared `xml1_flight_eligible(actor)` predicate emulates XML1's constructor-set
flag at flight restriction tests `0xda3d5` and `0xda4e5`. The separate fix7/carry
handler port must call it at those two source test points. This base has no such
port yet; PC's controller-index-based `actteamplayer` test is untouched.

Verification, from a Win32 Release build:

```
python -B scripts/anim_enum_sites.py <retail-XMen2.exe> --check
python -B test/anim_enum_sites_test.py <anim-enum-design-folder>
build/bin/Release/xml2_anim_enum_registry_test.exe
python -B scripts/test_anim_enums.py build/bin/Release/xml2_anim_enums_test.exe <retail-XMen2.exe>
```

These fixtures execute the owner's retail routines in an owned address-space
reservation. They never call the game entry point. XML file services, string
interning and animation databases use invented fixture data; the native map,
registration, lookup wrapper, parser, heap sort, cache loop and x87 code execute
from the supplied retail executable. Full-cache coverage uses 128 distinct
character keys, then verifies refusal of key 129 and release/reuse.
