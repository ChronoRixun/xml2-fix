# XML2 Fix 1.11.1 remaining limits

See [Legends Classic known issues](https://github.com/ChronoRixun/legends-classic/blob/main/docs/KNOWN-ISSUES.md)
for campaign fidelity and gameplay limits. Skating works and its duration follows rank; Dark Avalanche's
slow effect matches the original. Neither is an outstanding stop-after-one-burst bug.

- Internet relay, different home networks, three/four separate PCs and long/slow online sessions need
  wider testing. Same-PC pairing is not Internet certification. Host departure ends the session.
- Skirmish (1.11.1) is experimental: a fixed roster per round (mid-round joins are refused with the reason); every
  hero, power and arena, the exact original balance, the relay and larger internet parties need wider testing.
  Details: [skirmish-weekend.md](skirmish-weekend.md).
- The Remastered preview (1.12.0-beta.1, off by default) is checked on an NVIDIA PC only; AMD/Intel GPUs and a real
  Steam Deck are not checked yet. DXVK is not included.
- Button prompts after rebinding or Steam Input may be wrong.
- Guard and the 96-live-actor option remain optional and off by default.
- xemu/XML1-recomp save import remains disabled. Larger-format saves are not backward readable by 1.8.x.
- Xtraction return placement has live regression coverage at Interior Quarters and the sewer Healer
  point. The 66-point census includes three story-locked points where Team Management is unavailable;
  it does not certify every roster, save state or multiplayer return at every point.
