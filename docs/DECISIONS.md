# Limits wave 2 (W2): decisions in one page (2026-10-25, design revision 2)

Full design: `limits-wave2.md`; revision 1 kept as `limits-wave2.v1.md`. Addresses are retail XMen2.exe
(SHA-1 7d95cdb4...), bytes re-read in this pass.

1. **One native layout, "W2", one key.** `[Limits] SaveLayout=w2` (requires `EntityCapacity=1`) selects the layout
   and every engine raise together; nothing is individually selectable (the one-pass rule in code). No key = stock
   bytes. The LC file layout does not change.
2. **Targets:** definitions 1024 (u16 index), enhancement records 4096 (own-slot "none" 0xffff), attached 4096,
   instances 1024, world slots 1024 (u16, 0xffff none), objectives 1024 / 127 per act / 64 files, talents pool 256
   with hero-ID multiplier M = 200 (descriptor constant 101..220; the G11 census may move it), zone records **8
   natively**, per record 4096 states (incl. the unused record 0), 4096 ordinals, 2048 dynamic, 2048 smart, f32
   positions. Characters stay 21.
3. **Structures frozen** (limits-wave2.md section 3): item head 0x826 (u16 count, 1024 pairs), instance head 6, mission
   snapshot 0x540 (1024 objective bytes), state record 0x20 (8 payload bytes unchanged, new u16 world slot at +0x12,
   3 x f32 at +0x14), zone record 0x24388, manager 8 records = 0x122190. Bounded stream 1,279,625 bytes = 61 % of the
   0x200000 payload: **the payload and wrapper do not grow** (revision 1's 0x280000 was wrong).
4. **Layout identity, not size.** `use_native_profile` selects the lcsave descriptor by id ("RETAIL" / "E10A2" / "W2")
   and vetoes a mismatch; supported combinations are listed; the W2 install is one contribution to the entity-capacity
   startup transaction and publishes layout pointer, demand caps, menu feature names and the agreement rule together;
   failure = rollback to retail, rule unverified, LC seam not installed.
5. **E9 natively at 8**, not a DLL-side swap: the native eviction (0x466960 -> 0x465ec0) frees dependent world-item slots,
   so records outside the manager would need a second item lifecycle. Lifecycle kept as XML2's (capture at zone leave
   and save, LRU eviction, reset only by 0x484370 / the constructor); records survive mission boundaries (second
   deliberate difference from XML1 beside eviction). R17's remaining question (who calls 0x484370) is RE-13.
6. **E7:** 0x4bdc12 (`imul eax,eax,0x64; ret 4`, 7 bytes, no direct callers) -> DLL stub with M; the shared cap
   `push 0x63` at 0x4c05d9 -> thunk on `call 0x4c0460`; three pools at 0x4bdfe4 / 0x4be7e4 / 0x4be044 plus the other
   immediates of the 27 found in 0x4bd000-0x4c1000 (classification RE-11); no divide-by-100 exists there. Historical
   imports keep 100; the LC file stores catalogue ids, so re-basing M never marks a save.
7. **Feature bits:** bit 0 contiguous10 (as today), **bit 1 NATIVE_W2**, bit 2 reserved for E4 stage 2 (its design says
   bit 1: correct it). Bit 1 is set exactly when the E10A2 reverse translation of the file about to be written would
   refuse or drop (a dry run on the writer's own build); the hand list (fractional / out-of-range positions, > 5 zone
   records, > 256 / 375 / 150 pools, handle >= 150) is the test oracle. Older builds refuse bit-1 files with the
   existing generic message; W2 adds the named-version table for its own future.
8. **Affix pickers** 0x47e690 / 0x47ee90 are replaced whole at full capacity (differential test vs retail at <= 375).
9. **Agreement:** new rule **45 `NativeSaveLayout`** (ID requested), values {layout ordinal | records << 8 | structural
   revision << 16, definitions | enhancement records << 16, instances | world slots << 16, objectives | M << 16};
   registered only when W2 is installed (released records unchanged; a 0.4.1 peer is refused as "a rule this PC does
   not have", the W2 side names 45); rule 1 is not reused. Transfer unchanged.
10. **Gates:** G1 codec (reference-exported W2 vectors, exact bytes, dump equality with an explicit difference list),
    G2 engine (union host: ledger vs SPEC tables, OFF byte-identical, pickers / pools / records / objectives / talents
    differentials, guard-page proof), G3 live saves (Owen's, Shelley's, testers' `.lcsave` + `.bak` + ledgers + the 82
    corpus: offline round trips and census, then cold load / zone change / save / reload / co-op join on the candidate),
    G4 boundary list.
11. **Order:** descriptors -> converter -> E15 + E9 -> E3c / E3b / E3 -> E2 -> E14 -> E7 -> rule / builder -> real saves
    -> second-model review -> one build. Risk ranking E2 > E3c > E14 > E15/E9 > E3 pickers > E7 > E3b.
12. **Routing:** RE-1..13 to GPT Astra high (read-only), census C1-C3 to Sonnet, implementation to GPT Sol (Opus
    reviews E2), review of the whole diff by a second model before the build.

Open: RE-1..13 (free bytes, caller lists, picker and smart-manager specs, the 27 talent immediates, 0x484370's callers),
census C1-C3 (M, loader arrays, real-save peaks), the rule-45 ID, the E4 doc's bit number.
