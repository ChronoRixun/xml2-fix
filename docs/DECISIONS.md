# Limits wave 2 (W2): decisions in one page (2026-10-25, design revision 3)

Full design: `limits-wave2.md` (revision 3; section 15 is the implementation-ready ledger); revision 1 kept as
`limits-wave2.v1.md`. Addresses are retail XMen2.exe (SHA-1 7d95cdb4...), bytes re-read in this pass. Revision 3 folds
the GPT Astra re-review (e377: layout arithmetic confirmed, seven new findings, RE-1..13 answered) and census C1-C3.

1. **One native layout, "W2", one key.** `[Limits] SaveLayout=w2` (requires `EntityCapacity=1`) selects the layout
   and every engine raise together; nothing is individually selectable (the one-pass rule in code). No `SaveLayout`
   key = the configured baseline (RETAIL / E10A2, byte-identical to 0.4.x); no `[Save] Format` = stock bytes. The LC
   file layout does not change.
2. **Targets:** definitions 1024 (u16 index), enhancement records 4096 (own-slot "none" 0xffff), attached 4096,
   instances 1024, world slots 1024 (u16, 0xffff none), objectives 1024 / 127 per act / 64 files, talents pool 256
   per registry pool with hero-ID multiplier **M = 200** (descriptor constant 101..219; rule: position < M, shared
   < M-1; census C1: team bonuses 0, immunities 14, shared 63, so 200 is final), zone records **8 natively**, per record
   4096 states (incl. the unused record 0), 4096 ordinals, 2048 dynamic, 2048 smart, f32 positions, **zone-string table
   8192 slots / 0x20000 arena**. Characters stay 21. Zone-loader placement arrays stay 1,024 (census C2: XML1 max 582,
   XML2 stock 750): not save-bound, a whole-frame re-layout of a 1,636-instruction function; W2 adds a validator error
   (> 1024 placements) and a runtime guard that refuses the zone instead of overrunning; the 2,048 raise is a later
   runtime row (RE-17).
3. **Structures frozen** (limits-wave2.md section 3): item head 0x826 (u16 count, 1024 pairs), instance head 6, mission
   snapshot 0x540 (1024 objective bytes), state record 0x20 (8 payload bytes unchanged, new u16 world slot at +0x12,
   3 x f32 at +0x14; every constructor / copy / clear initialises or carries them), zone record 0x24388, manager 8
   records = 0x122190 (E10A2 comparison offsets 0x809a8 / 0x809ac / 0x809b0 / 0x80c1c / 0x80c20). Bounded stream
   1,421,114 bytes = 67.8 % of the 0x200000 payload (1,279,802 with the retail string table): **the payload and wrapper
   do not grow**.
4. **Layout identity, not size.** `use_native_profile` selects the lcsave descriptor by id ("RETAIL" / "E10A2" / "W2")
   and vetoes a mismatch; the W2 install is one contribution to the entity-capacity startup transaction and publishes
   layout pointer, demand caps, menu feature names and the agreement rule together; `objective_text`, ForcedTeams,
   `test_state`, SaveTrace and the save-core bridges read the descriptor and join the same transaction; failure =
   rollback, rule unverified, LC seam not installed.
5. **E9 natively at 8**, not a DLL-side swap: eviction (0x466960 -> 0x465ec0) frees dependent world-item slots through
   0x479c10 -> 0x4655d0 (one payload byte) -> 0x479ad0, so W2 adds a **disposal adapter** that reads the u16 slot at
   +0x12 (otherwise slot 0 would be freed and the real slot leaked). Lifecycle preserved exactly as XML2's: the unload
   method `CMap` vt+0x30 (0x4844a0, zone loader 0x484e48) either resets all records (vt+0x34 = 0x484370 at 0x484627,
   also clearing `CSmartEntMgr`) or captures (0x484900), by its boolean; who passes it set is RE-15. The revision-2
   claim that records survive mission boundaries is withdrawn; L3 is evidence, not acceptance, until RE-15 answers.
6. **E7:** helper function 0x4bdc00 (vtable 0x68ec24 +0x30, caller 0x4c061e; tail `imul eax,eax,0x64; ret 4` at
   0x4bdc12) -> 5-byte jmp to a DLL stub with M; endpoint `lea ecx,[eax+0x64]` 0x4c0644 -> cave; shared cap `push 0x63`
   0x4c05d9 -> thunk on `call 0x4c0460` writing M-1 at [esp+0xc]; the registry (0x3228 at 0x4c0584, ctor 0x4c0370,
   singleton 0x77339c; three pools A/B/C + index tables) relocates whole to 256 per pool with regenerated pool-relative
   offsets; the five-at-a-time generation loop 0x4bfa20..0x4bfaf8 is replaced by a capacity loop; four-dword bitmap
   clears become eight; the 27 immediates are classified (RE-11). Historical imports keep 100; the LC file stores
   catalogue ids, so re-basing M never marks a save.
7. **Feature bits:** bit 0 contiguous10 (as today), **bit 1 NATIVE_W2**, bit 2 reserved for E4 stage 2 (its design says
   bit 1: correct it). Bit 1 is set by a **target-aware representability probe** for target E10A2: the target's reverse
   translation with the target's mappings (M = 100), plus the engine constraints the translation does not check
   (objective index <= 255, enhancement record < 512, pools 256 / 375 / 150, handles < 150, <= 5 zone records, integral
   positions in range), drops and feature gates. The hand list is the test oracle. Older builds keep their generic
   refusal; W2 adds the named-version table for its own future.
8. **Objectives:** the loaded objective's wide index lives in a **DLL side table keyed by pool slot** (+0x1aa is live
   data: target count / flags), with copy (0x488430 bridge), release / reuse (0xffff) and sort semantics specified;
   RE-16 decides whether a stack-temporary shadow is needed. `objective_text` / ForcedTeams / `test_state` stop holding
   literals.
9. **Affix pickers** 0x47e690 / 0x47ee90 are replaced whole at full capacity to the RE-4 specification (one RNG call
   `0x5601e0(0, count-1, true)`; differential vs retail at <= 375).
10. **Agreement:** new rule **47 `NativeSaveLayout`** (coordinator-assigned), values {layout ordinal | records << 8 |
    structural revision << 16, definitions | enhancement records << 16, instances | world slots << 16, objectives | M <<
    16}, registered only when W2 is installed (released records unchanged); rule 1 is not reused. Transfer unchanged.
11. **Gates:** G1 codec (reference-exported W2 vectors, exact bytes, dump equality with an explicit difference list),
    G2 engine (union host: ledger vs SPEC tables, OFF byte-identical, pickers / pools / records / disposal / objectives
    / talents differentials, T9b largest-index vs largest-count pairs, guard-page proof), G3 live saves (the census C3
    list: Owen's, Shelley's, testers' `.lcsave` + `.bak` + ledgers + the 82 corpus: offline round trips and census, then
    cold load / zone change / save / reload / co-op join on the candidate), G4 boundary list.
12. **Order:** descriptors -> converter + probe + consumer modules -> E15 + E9 (+ strings, disposal, loader guard) ->
    E3c / E3b / E3 -> E2 -> E14 -> E7 -> rule / builder -> real saves -> second-model review -> one build. Risk ranking
    E2 > E3c > E14 > E15/E9 > E3 pickers > E7 > E3b. Section 15 is the row-for-row ledger (W2-L-*, W2-E15-*, W2-E3c-*,
    W2-E3b-*, W2-E3-*, W2-E2-*, W2-E14-*, W2-E7-*, W2-T-*).
13. **Routing:** RE-14 (zone-string sites), RE-15 (unload boolean callers), RE-16 (objective copy destinations), RE-17
    (loader frame map, later) to GPT Astra high (read-only); implementation to GPT Sol (Opus reviews E2); review of the
    whole diff by a second model before the build.

Open (none gates the start; each row has its fallback named in section 15): RE-14, RE-15, RE-16; the E4 doc's bit
number; RE-17 is a later runtime row.
