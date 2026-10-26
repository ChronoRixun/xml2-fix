# Effect templates

`[Limits] EffectTemplates=512` raises both the effect-file table and the
effect-definition table in the native effect library. Missing/220 keeps stock;
221..2048 is accepted. Other values retain stock storage and register an
unverified request, closing online admission. The DLL default stays stock.

The library is the heap singleton at `0x6f3c3c`, created by `0x4078d0`. Its file
constructor is `0x405f70`; its definition constructor is `0x4063c0`. Both tables
remain inside that one allocation. Definitions retain their `0x44` stride and
native handles, generation IDs, fields and reference counts.

| Capacity | Definition base in owner | Allocation size | Bitmap dwords |
|---|---|---|---|
| 220 | `0x4874` | `0x8a18` | 7 |
| 512 | `0xa868` | `0x14104` | 16 |
| 2048 | `0x2a128` | `0x50344` | 64 |

`effect_templates_rules::layout_for` derives the two layouts from their native
constructors. The file pool includes a sentinel ring word and N+1 value words;
the definition pool includes both bitmaps, the free ring, IDs, mask and shift.
Both record arrays retain their native strides. Masks are generated from N-1,
including non-power-of-two capacities.

`tools/w3_effect_templates_ledger.py --check --verify` reads the locally owned
retail executable and the instruction inventory. It verifies the executable's
SHA-256, decodes operands, rejects unclassified layout operands, checks all
overwritten instruction boundaries and incoming branches/data pointers, and
checks DLL layout consumers. `XML2_FIX_TEST_EXE` or `--exe` selects the image;
`--db` selects the read-only instruction inventory. The generated
`src/effect_template_sites.hpp` contains 174 operands in 172 instructions across
30 functions, five bitmap-clear spans and the primitive spill ledger.

The extra clears matter: the file constructor, both definition constructor
bitmaps, the definition destructor and unload-all each have seven unrolled
stores in retail. All five become preserving caves that clear ceil(N/32)
dwords. The native constructor, lookup, allocation and teardown code otherwise
uses the new offsets and bounds in place.

The shared `bitset<220>::findNext` at `0x403d40` remains read-only. Only the effect
call at `0x4053c4` moves to a DLL copy with its own capacity. ModelPools owns the
other call at `0x571724`; the installer accepts its stock bytes or the exact
target published after ModelPools commits. An arbitrary redirect is refused.

Before any write, the owner pointer must be zero and every expectation must
match. All writes use one `patch_transaction`; publication occurs only after
readback and protection restoration. Failed installs retain stock publication.
Uncertifiable rollback retains referenced memory and refuses startup.

`effect_template_layout.hpp` publishes `{definition_base_offset, mask_offset,
stride, capacity}`, with stock defaults. Lock-on handle validation and multipart
loop-time reads use this descriptor. Their installation guards also validate
the descriptor's native mask and capacity operands, rather than expecting the
retail displacement after a raise. Publication precedes any zone load.

Rule **55 EffectFiles**, revision 1, registers `{N,0,0,0}` only after a successful
requested raise. A failed/invalid request registers unverified zero values;
absent/220 adds no rule. Peers with different capacities or a missing rule refuse
in both directions by name. Status includes `fx files n/N; fx defs n/N`.

The native `xml2_effect_templates_test <XMen2.exe>` fixture executes the original
getter, constructors, load path (with synthetic parser/file services), tree
insertion and duplicate lookup, unload-all and destructor at 220/221/512/2048,
with ModelPools on/off. It checks canaries, every guard byte, layout publication,
helper equivalence below 220, OFF plans and agreement mismatches. The `off`,
`stock`, `on`, `invalid`, `base`, `built`, `guard`, `foreign-model`, `allocation`
and `protect` modes run the production installer separately. No game starts.

See [primitives per effect](primitives-per-effect.md) for the independent record
spill option. These limits change transient runtime storage; they add no save
fields or release identity.
