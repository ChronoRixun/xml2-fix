# LC save protection and supported content

The LC writer uses save_guard_rules.hpp; ledger/import writes retain the generic save_format_rules writer.
Before replacing a newer writer/content/catalogue original, it publishes a flushed, byte-verified .newer through a non-replacing,
write-through rename. An identical protected copy is reused; a different/unreadable .newer or a stale
.newer.tmp refuses the write. A newer original found only in .bak is also protected before backup rotation.
The destination stays in place until the final rename; create/write/flush/verify/publication/replacement failures
keep its bytes and pathname. Content/catalogue rollback protection also applies when the DLL semver did not change. Normal intact files rotate .bak; a damaged file cannot replace a good backup.

Campaign and profile refusal latches are independent and keyed by destination. Failed reads and preflight of
existing files protect unresolved identities, damaged originals without a usable backup, wrong kinds and
needs-newer files from default/automatic writes. The overwrite sequence's preliminary delete does not clear
protection. A deliberate menu Delete clears only that campaign's latch and removes its primary/backup;
.newer remains. Profile protection has no automatic delete/reset path. Successful translation into a card
buffer does not clear a prior refusal: the Core native-completion path must call
save_format::confirm_load_success(kind, slot) after successful application. Until then the conservative latch
remains, even if a repaired file translates. This branch does not guess native completion.

Mod checks use mod_loader's actual startup winner snapshot. Cosmetic resources, UI and scripts remain allowed.
Replacing a bind-covered file (including extension aliases), catalogue metadata, numbered definitions or Maps
placement XMLB/ENGB refuses LC activation with the winner filename/root. Placement files are covered by saved
provenance even where no sidecar exists. Added catalogue/numbered roots and package references with no supported
bind refuse as ID-0/unknown identities. Package resources referencing verified identities remain allowed;
unverifiable package winners refuse with their filename. ID-0/keyref KEYS and ID-0 bind rows refuse intact.
Offline saves from other content versions are still permitted when their actual identities resolve.

The builder's append-only release_identity history assigns unique four-character GameVersion tokens, reserves
old X1.* tokens, pins the candidate bind manifest and archives the catalogue/legacy/bind hashes in
_build/lc-release-identity.json. A new content/catalogue identity or changed immutable bind manifest needs an
appended identity/token; the final integrator must assign its final release identity rather than reusing the
candidate. The current candidate (content 16, catalogue 2) is LC20. Fingerprints are bind manifests, not a claim
that every script/asset was hashed into the save.

For LC, missing/retail/invalid tokens and any refused version patch leave game sockets disabled with WSAEACCES.
If the socket exclusion hook itself cannot be installed, DLL initialization fails, preventing an unsafe retail
fallback. Non-LC behavior retains the ordinary retail fallback. Native tests exercise actual socket calls after
absent, retail, invalid and unpatchable tokens.

The builder V-TBD (number assigned at merge) counts unique talent names plus distinct files across permanent,
zone/NPC and worst-party packages: 100 talents and 9 files are separate caps. Runtime guarded bridges observe
new registration refusals at 0x4c011e and the file-cap check at 0x4c04a8, log the name/file and latch LC save/load
off for that session. Existing-name/file reuse follows native behavior; talent IDs keep radix 100.
These guards do not install a registry capacity expansion.

Run xml2_save_guards_test, xml2_save_content_guards_test and xml2_test --game-version-rules/
--save-format-rules. The synthetic tests cover preservation collisions, both save kinds, two writes, existing
newer backups, injected failure points, cosmetic/bound overlays, keyrefs and actual socket exclusion.
The LC device-info campaign count at 0x55f190 counts LC primary/backup slots rather than retail Save/*.save.
This prevents an existing LC campaign from being hidden behind the native NO SAVE DATA popup. Refused files
and recoverable backups stay visible; .newer and profiles do not add campaign slots. Readiness/import notices
remain reachable even when no usable campaign can be listed.

Optional [Debug] SaveTrace=1 records actual native capture (call 0x466a44 to 0x4664d0) and per-entity restore
(call 0x486260 to 0x465c10). Both guarded wrappers forward arguments/results unchanged and only count/log calls.
It defaults off and is acceptance telemetry, not a save-format change. An already patched site disables its
installation; the integrator must reconcile this optional restore call with Core's restore hook when enabling it.

Owned full-access DR/local acceptance passed with real input and cold load/resave. Evidence remains outside
the repositories in ../accept-full. Runtime overflow and combined Core online acceptance remain unproved.
