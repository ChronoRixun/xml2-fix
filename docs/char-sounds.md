# Character sound cache

`[Limits] CharSounds=127` raises the retail character sound cache from 16 records to 127.
Absent or `16` keeps executable bytes and the gameplay agreement unchanged. Every other value is refused,
leaving online admission unverified.

Each actor holds one reference keyed by its stats' nonempty sounddir. When the retail cache fills, another
directory cannot claim a record; the actor keeps -1 and the getter supplies record 0, playing another character's
sounds. This can occur independently after AnimEnums expands the animation-set cache to 128.

The replacement owns 127 native-constructed records of 0xdc bytes in the DLL. Six address operands and two signed
bounds commit together: getter 0x438cb8, bases 0x438cc3/0x438cc9, scan 0x438dfc/0x438e8d, hit 0x438e50,
claim 0x438eb4 and teardown 0x42198d. The old static's CRT lifecycle stays native. The teardown bound must
accompany the getter to release indices 16..126.

Installation runs after the entity layout is published and requires actor capacity strictly below 127.
Constructor code is guarded and hashed before execution. A clean transaction failure retains retail bytes
and registers an unverified request; an uncertified rollback retains backing and refuses startup.
Rule 49, CharSounds revision 1, advertises `{127,0,0,0}` only after successful installation.
Peers with different settings refuse by rule name.

An additional guarded call at actor init 0x4239be observes the original allocator's result without changing key
sharing, loading, early returns or fallback. The `CHAR_SOUNDS` test-pipe command returns `installed`, `requested`,
`capacity`, `peak`, `refusals`, `empty_sounddir_actors` and `allocations`. Peak is updated at every allocation;
empty-directory actors are counted separately from capacity refusals. The pipe reads atomic counters rather than
mutable cache bytes. Counters cover this module's installed path; absent/16 leaves the native path uninstrumented.

`test/char_sound_sites_test.py <retail-exe>` checks the design ledger, full-text absolute references and every
literal source guard against the changed spans. `xml2_char_sounds_test.exe <retail-exe>` executes the production
transaction and native getter, allocation, construction and teardown in an owned test image with synthetic
services. Live sound playback acceptance (RV1) remains pending.
