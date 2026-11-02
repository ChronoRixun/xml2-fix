# Attached powerups and virtual-function nodes

These two independent runtime limits provide headroom for the X-Men Legends I
compatibility content. When the native attached-powerup pool fills, a new power
attachment is omitted. A full virtual-function-node pool omits a callback
registration. Neither allocator reports the lost request to the player.

```ini
[Limits]
AttachedPowerups=256
VFNodes=256
```

Each key accepts only 256. An absent or empty key keeps every byte for that pool
retail (128 attached powerups, 192 VF nodes). Other values are refused. Each
owner has a separate guarded transaction, so either key can be used alone.
A requested but refused row closes online admission through an unverified
gameplay-agreement registration. A successful row registers revision 1 with
`{256,0,0,0}`: rule 61 `AttachedPowerups`, rule 62 `VFNodes`. Absent keys add no
entries and preserve the released agreement record. No release identity changes.

## Layout and installation

The retail getters still allocate, construct, publish and free their own
singletons in heap pool 14, aligned to 16 bytes. The allocation operand and
every local metadata consumer move together. These are **outer-object** offsets;
internal template methods receive `outer+4`.

| Field | Attached 128 | Attached 256 | VF 192 | VF 256 |
|---|---:|---:|---:|---:|
| Owner pointer | `0x7988D4` | same | `0x79BA3C` | same |
| Getter | `0x538990` | same | `0x54C690` | same |
| Record stride | `0x68` | same | `0x14` | same |
| Constructed bitmap | `0x3404` | `0x6804` | `0xF04` | `0x1404` |
| Free-ring storage | `0x3414` | `0x6824` | `0xF1C` | `0x1424` |
| Ring write/read/free | `0x3618/1C/20` | `0x6C28/2C/30` | `0x1220/24/28` | `0x1828/2C/30` |
| Live bitmap | `0x3624` | `0x6C34` | `0x122C` | `0x1834` |
| Live count | `0x3634` | `0x6C54` | `0x1244` | `0x1854` |
| Generation IDs | `0x3638` | `0x6C58` | `0x1248` | `0x1858` |
| Mask/shift fields | `0x3838/3C` | `0x7058/5C` | `0x1548/4C` | `0x1C58/5C` |
| Allocation size | `0x3840` | `0x7060` | `0x1550` | `0x1C60` |

The generated ledger has 66 attached-powerup and 71 VF-node operands. Six
private code stubs clear all eight dwords of each constructed/live bitmap,
including destruction. The stubs preserve every register and EFLAGS, independent
of the direction flag. Handles remain 32 bits; the native constructors derive
mask `0xFF` and shift 8 from the patched last-index seed.

Shared helpers retain their original bytes. Only these pool-local calls change:

| Pool call | Private copy of | Changes inside the copy |
|---|---|---|
| `0x538546` | bitmap scan `0x403CA0` | capacity 256 |
| `0x538709` | ring pop `0x403F20` | capacity 256; metadata `+0x408/+0x40C` |
| `0x54C199` | bitmap scan `0x54C060` | capacity 256 |

The VF ring helper `0x54C410` belongs to this pool and moves in place. The three
shared bodies and their 14 unrelated direct callers receive no writes.

`power_pool_plan.hpp` constructs the complete transaction. It checks the retail
operand/clear/helper/call bytes and a null singleton immediately before writing.
The installer finishes and protects unpublished executable storage first;
layout descriptors and diagnostic readers publish only after commit, readback
and protection restoration. A certified refusal frees the private code; an
uncertifiable rollback retains referenced storage and refuses startup.

## Diagnostics composition

The prepared base predates the pool diagnostics row. This change supplies that
row's generated `pool_counter_sites.hpp`, its generator and observer emitter for
integration. Its 29 readers start at stock offsets. A successful power-pool
publication updates only the matching reader's offset/capacity; refusal leaves
the stock reader intact. `limits::status()` also reports both live counts.

D1 observes the unchanged branch/return spans at `0x5386AA` and `0x54C39A`, after
the capacity comparison. Their branch targets and refusal behavior are the same
at either layout. This avoids overlapping any raised operand and requires no
stand-down or duplicate refusal counter. All other D1 observer rows stay as in
the diagnostics generator. Integrators should merge these reader/observer
changes into their diagnostics row rather than reverting its other W3 updates.

## Reproduction and limits of the proof

Use an owned retail executable through `XML2_FIX_TEST_EXE`. No game or emulator
is started by these gates.

```text
python -B tools/power_pool_ledger.py --check --verify
python -B scripts/pool_counter_sites.py <retail-exe> --check --verify
cmake -S . -B build -A Win32
cmake --build build --config Release --target xml2_power_pools_host
build/bin/Release/xml2_power_pools_host.exe <retail-exe>
python -B tools/test_power_pools.py --host build/bin/Release/xml2_power_pools_host.exe --work build/power-proof
```

The host executes the retail getters, constructors, allocators, handle validation,
FIFO rings, generations, release and shutdown on dirty synthetic owners. Every
slot, full refusal, bitmap boundary, multiple ring wraps, generation reuse and
signed rollover are checked with diagnostics off and on at stock and 256.
Prefix canaries and an exact protected end boundary catch owner overruns; a
deliberate one-byte overrun must fault. Every generated guard is independently
mutated, and protection/flush failures must leave or restore the entire image.
Separate processes test `off`, `invalid`, `base`, `guard`, `singleton`, `on`,
`attached` and `vf` installer modes. The Python gate independently packs members,
checks every emitted operand, and scans all source guards against both write sets.

This establishes the local native layout and lifecycle. Live gameplay, long
sessions, save/load and multiplayer have not been exercised for these raises.
An exhaustive transitive alias/serialization proof remains outside the established
static census. Capacity 256 exceeds twice the observed peak of 114; it is not a
measured bound for future larger actor fights. Remeasure after actor-capacity work.
