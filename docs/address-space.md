# Address space (E12): the large-address-aware exe

The Legends Classic builder sets `IMAGE_FILE_LARGE_ADDRESS_AWARE` on the build's own copy of XMen2.exe (one word of
the PE file header; the install's exe is never changed). A 32-bit process on 64-bit Windows then has 4 GB of address
space instead of 2 GB; Wine and Proton honour the flag too. Windows reads it when the process starts, so the fix
cannot switch it; it reports it and keeps two things true. Nothing is refused: an exe without the flag only logs.

- **Start line:** `address space: laa on|off; 2 GB guard on|off|unavailable; top 0x...; n MB free below 2 GB`
  (the older `limits: address space - LAA ...` line stays as it was).
- **The 2 GB guard:** with the flag on, the 64 KB block at 0x7FFF0000 (the last one below 2 GB, which a 2 GB process
  never has) is reserved at start, so no allocation can straddle 0x80000000. The static risk check found an MMX
  mip-filter loop (linked into XMen2.exe, cgD3D8.dll and libIGGfx.dll) that compares pointers signed; it goes wrong
  only for a buffer that starts below 2 GB and ends above it. `unavailable` = something else held the block (seen
  only where another runtime reserves it); the status then says `a block crosses 2 GB` if one ever does.
- **Test pipe:** `status` adds `memory laa on; top 0xFFFEFFFF; above 2 GB n MB; private n MB; highest 0x...; 2 GB
  guard on`; `memory` alone gives the same words. `memory high <MB>` reserves the free space below 2 GB except
  <MB>, so the game's next allocations land above 2 GB: a controlled high-address test (test builds only; the pipe
  is off in players' installs).
- **Crash line:** an unhandled crash writes `crash: exception 0xc0000005 at 0x... (module+0x...), writing 0x...;
  laa on; above 2 GB ...; private ...; highest ...; 2 GB guard on` to `xml2-fix.log`, then the crash goes on to the
  previous handler or Windows' error reporting as before.

Our own code had two 2 GB ceilings, both fixed with this: the classic main backdrop's guarded memory reader and the
test pipe's state reader refused addresses above 0x7fffffff.

Tests: `xml2_address_space_test` (the rules: straddle test, reservation sizes, totals, status words, the pipe verb,
the state reader above 2 GB) and `xml2_address_space_host` (the module in a large-address-aware host of its own:
guard, status, `memory high`, a bottom-up block landing above 2 GB afterwards, the crash line of a crashing child).
