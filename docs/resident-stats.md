# ResidentStats

`[Limits] ResidentStats=320` raises the resident CStats pool from 31 to 320 and the NPC payload pool from 11 to 320 in one startup transaction. Hero payload capacity stays 22. The manager grows from 0x12334 to 0x777e0 bytes. Absent or 31 contributes no writes or agreement entry; unsupported values leave rule 50 unverified, closing online admission.

The generated 275-row table is pinned to R2 SHA-256 `5111dd02726c6b3fc120b42ccf414fe4f33fad6a3b9cb1d96a3635df59bd0fb0`. Regenerate with `python tools/generate_resident_stats_sites.py <approved-ledger.md>`; use `--check` to verify. The five replaced NPC functions use full original-region hashes and short entry guards. No retail function bodies are embedded in the header.

Installation requires the lazy manager cell at 0x71770c to be empty, the retail 297-definition loader cap and 22-hero payload check, all four registered layout consumers, matching regions, and no conflicting transaction writes. Discord, forced parties, revival pricing and XP guards follow the published layout through `stats_layout::expected`. Cave/rewrite/replacement spans are refused by that helper.

The admission guard preserves AL, tests the low byte of the file-class stack dword, and refuses before allocation when the outer or required payload pool is full. The native refusal returns loader success with handle zero and fallback stats. Logs name the definition and reason once per definition per zone; counters count every refusal. All three store caves preserve the widened signed CMP flags. Handle lookup retains retail masking; released handles receive no new stale-generation check.

The NPC replacement contract retains signed truncating division in owns (AL only), unguarded empty pop, clear-before-enqueue free, constructed-only destructor clears, ascending ring initialization with the extra ring cell untouched, and no live-count reset by ring initialization. The sole constructor wrapper completes both ten-word bitmap clears; the native constructor clears live.

Rule 50 ResidentStats revision 1 advertises `{320,22,320,1}` only after verified installation. Peers with differing settings refuse each other by name.

`RESIDENT_STATS` on the test pipe reports installation, counts, peaks, refusal reasons and zone invariant failures. `RESIDENT_CENSUS` requests a game-thread definition walk; poll again after a sample for the cached result. The status line includes resident/hero/NPC occupancy when installed. Zone unload/load hooks verify hero + NPC = resident.

Save blocks remain unkeyed and are written for resident heroes in definition order. Compatibility here requires the same resident hero set: beta 8 has 21 heroes on both layouts. The native fixture compares and cross-loads those streams. Larger rosters need the separate R2b work.

Native checks: `xml2_resident_stats_test <owned-retail-exe>`, `xml2_resident_payload_test <owned-retail-exe>`, `xml2_stats_layout_test`, and `python test/resident_stats_sites_test.py <approved-ledger.md> <owned-retail-exe>`. These fixtures execute retail routines loaded from the owner's executable with invented services; no game entry point runs. The indexed UI smoke stops after its two payload calls, before unrelated GUI work. Live RV3 and seeded-save RV3b remain integration gates.
