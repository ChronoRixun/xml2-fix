# Xtraction equipment-shop adapter

`[Game] XtractionShop=1` enables a guarded option hook in the full native Xtraction menu.
The default is `0`. A companion builder calls `extractionPointServices(owner, permission)`
(`n(ai)`) with the permission for this point and campaign state. Native `extractionPoint`
calls have no shop permission. The script table is installed with `[Game] ForcedTeams=0`
or `1`; `xml2fixFeature("xtractionshop")` reports function availability, independently of
whether the option hook is enabled.

The hook replaces only the destination of the getter call at `0x4a6d33`, immediately before
the dialog finalizes through vt+0x68. It adds string ID 2005 with the existing shop script
adapter, `shopMenu('equip')`. It reads the label from the build's own strings. Byte guards
cover the native Save option's calling convention, the finalize call, and both singleton
getters. A mismatch installs nothing. Save, Change Team, and the world-map switch keep
their existing behavior. Point permission is restored after the native handler returns.

The original full handler tests the vendor's campaign availability; its lite handler also
tests flag 4. The companion builder uses the original contact script's saved signal and
preserves that point-local flag. XML2 has a shared shop UI, so this adapter does not rebuild
the original shop layout or stock.

Synthetic and retail-byte checks: `xml2_test --xtraction-shop-rules` and
`xml2_test --script-rules`. `XML2_FIX_TEST_EXE` may point at a legally owned retail executable
for the read-only guard checks. The added function takes the current table to 319 of the
320 registered-name capacity; future function additions must audit that limit.
