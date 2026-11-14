# Whole-roster reset at fresh XML1 mission starts

resetHeroVitals() is registered last by forced_teams and runs only with ForcedTeams enabled.
It invokes the registry's vt+0x0c (retail 0x449d70): all saved roster vitals, including benched heroes,
are cleared through each record's native extension reset. The next spawn initializes fresh vitals.
It does not revive actors mid-zone, seat heroes, change progression or charge currency.

Retail function bytes and the registry vtable slot join the module's all-or-nothing guards. Expected
bytes are translated through stats_layout for the raised ResidentStats layout. Runtime object/vtable
checks and SEH refuse an unavailable call. The new entry needs a script tree larger than the retail
320-name cap; if it would be dropped, rule 63 is unverified and online admission closes.

LC emits the call at keepheroes=false starts, before party construction/menu, and after a side return
snapshot is captured. Keepheroes=true continuations remain unchanged. seatParty retains beta.8's fallen
seated-hero safety. Rule 63 revision 1 uses {1,1,0,0}, refusing beta.9's {1,0,0,0} in both directions.
The mapped-retail test runs the actual reset loop against synthetic records with a hole and checks
that every extension is reset while the registry bytes stay unchanged. Builder tests cover guards,
continuations, nested/inlined starts, ordering and repeat conversion.
