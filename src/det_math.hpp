#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>

// Deterministic transcendental functions for the lockstep simulation path (the NPC weapon brain's aiming).
//
// Online play is lockstep: both PCs must compute bit-identical floats. +, -, *, / and sqrt are IEEE-754 correctly
// rounded, so they give the same bits on every CPU once the rounding environment is fixed. MSVC's x86 /arch:SSE2 code
// is mostly SSE2, but it still emits x87 instructions (a double returned in ST0 is often computed by a final x87
// fmul / fadd), and an x87 operation rounds to the precision in the x87 control word: 24 bits under a Direct3D 8
// device without FPU_PRESERVE, 53 by default - review 2026-10-25 measured 0x3cfef7d0 vs 0x3cfef7c0 for the aim
// coefficient at dt = 1/60. So every weapon-brain entry point runs inside an fp_env: the GAME'S OWN simulation x87
// control word (game_x87_cw below) and MXCSR 0x1f80 (round to nearest, no FTZ/DAZ), both restored on exit. The game
// routines the brain calls while it runs (random draw, angles, turns, move requests, visibility and grid queries)
// therefore compute exactly as the same routines do outside the guard, on every PC.
//
// The game's word (review round 3, evidence in beta7-fixes evidence/fpu-cw-audit.txt): XMen2.exe's CRT startup sets
// 53-bit precision (0x6729f6: _controlfp(_PC_53, _MCW_PC), the CRT's _setdefaultprecision; rounding stays the process
// default, nearest, exceptions masked: 0x027f); the Direct3D 8 device is created by libIGGfx.dll with
// D3DCREATE_FPU_PRESERVE (0x2cbbb..0x2cc04: behaviour |= 2 when alchemy.ini's `preserveFPU` is true, its default when
// absent - neither the retail nor the Legends Classic alchemy.ini sets it; a live log shows `behaviour 86` =
// FPU_PRESERVE | MULTITHREADED | MIXED_VERTEXPROCESSING), so Direct3D never lowers the precision; xml2-fix's display
// hook keeps the engine's behaviour on creation, adding FPU_PRESERVE only should an edited alchemy.ini have turned it
// off (display.cpp), and IDirect3DDevice8::Reset takes no behaviour (windowed, borderless and resets alike); nothing
// else in the install calls _controlfp or keeps a changed word (XMen2.exe and
// libIGGfx.dll switch to truncation around vertex loops with fldcw and restore the saved word; cg.dll's CRT math
// saves and restores). With an x87 word of 0x027f an x87 +,-,*,/ of normal doubles rounds exactly as SSE2 does.
// The x87 transcendentals the game's own routines use while the brain runs (fpatan in the direction-to-angles
// routine at 0x560835 / 0x56088e, fsin/fcos elsewhere) are the game's: lockstep depends on them everywhere in retail
// already, so the brain adds no new cross-CPU risk with them. The CRT's pow, exp, sin
// and cos give no such promise: their implementations pick code paths by CPU feature (SSE2 / FMA3 dispatch, x87
// fsin/fyl2x whose results differ between CPU vendors) and are only "faithfully" rounded. These replacements use
// only correctly rounded double operations in a fixed order (Cody-Waite range reduction and fixed polynomials), so
// their results depend on nothing but the input bits. They are accurate to about 1e-16 relative in double; the float
// results are within one float ulp of the exact value (test/npc_semi_auto_test.cpp pins their bits and checks the
// error against the CRT). They are not meant for |x| beyond about 1e5 radians (never reached: AI angles stay within
// a few turns), where the reduction loses accuracy - still deterministic.
namespace det_math
{
 // Round to nearest, every exception masked, no flush-to-zero / denormals-are-zero: Windows' default MXCSR.
 constexpr std::uint32_t default_mxcsr = 0x1f80;
 // x87: the game's own simulation word - 53-bit precision, round to nearest, every exception masked (see above).
 constexpr std::uint16_t game_x87_cw = 0x027f;
 constexpr std::uint16_t default_x87_cw = game_x87_cw;

 // The fixed floating-point environment for one weapon-brain entry: saves the caller's x87 control word and MXCSR,
 // loads the defaults, restores both when it goes out of scope (also if a C++ exception unwound through it; the
 // entry points are noexcept, so none does).
 class fp_env
 {
  std::uint16_t cw_ = 0;
  std::uint32_t csr_ = 0;
 public:
  fp_env() noexcept
  {
   std::uint16_t cw = 0;
   std::uint16_t want = game_x87_cw;
   __asm { fnstcw cw }
   __asm { fldcw want }
   cw_ = cw;
   csr_ = _mm_getcsr();
   _mm_setcsr(default_mxcsr);
  }
  ~fp_env()
  {
   std::uint16_t cw = cw_;
   _mm_setcsr(csr_);
   __asm { fldcw cw }
  }
  fp_env(const fp_env&) = delete;
  fp_env& operator=(const fp_env&) = delete;
 };

 // 2^k * v by exact power-of-two steps (each factor is a normal double).
 inline double scale(double v, int k)
 {
  while (k > 1000) { v *= std::bit_cast<double>(std::uint64_t(1023 + 1000) << 52); k -= 1000; }
  while (k < -1000) { v *= std::bit_cast<double>(std::uint64_t(1023 - 1000) << 52); k += 1000; }
  return v * std::bit_cast<double>(std::uint64_t(1023 + k) << 52);
 }

 // e^x. Reduction x = k ln2 + r (|r| <= ln2/2, ln2 split as fdlibm's ln2_hi + ln2_lo so k * ln2_hi is exact), then
 // the degree-13 Taylor polynomial of e^r (truncation below 5e-18) in Horner form.
 inline double exp(double x)
 {
  if (x != x) return x;
  if (x > 709.78) return std::numeric_limits<double>::infinity();
  if (x < -745.2) return 0.0;
  constexpr double inv_ln2 = 1.44269504088896338700e+00, ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
  const double t = x * inv_ln2;
  const int k = static_cast<int>(t < 0 ? t - 0.5 : t + 0.5);
  const double r = (x - double(k) * ln2_hi) - double(k) * ln2_lo;
  double p = 1.0 / 6227020800.0;            // 1/13!
  p = p * r + 1.0 / 479001600.0;            // 1/12!
  p = p * r + 1.0 / 39916800.0;
  p = p * r + 1.0 / 3628800.0;
  p = p * r + 1.0 / 362880.0;
  p = p * r + 1.0 / 40320.0;
  p = p * r + 1.0 / 5040.0;
  p = p * r + 1.0 / 720.0;
  p = p * r + 1.0 / 120.0;
  p = p * r + 1.0 / 24.0;
  p = p * r + 1.0 / 6.0;
  p = p * r + 0.5;
  p = p * r + 1.0;
  p = p * r + 1.0;
  return scale(p, k);
 }

 // base^y = e^(y ln base) for the weapon brain's two aim-smoothing bases (std::pow(0.15 or 0.25, dt) before):
 // ln of the double nearest each base, as a constant.
 constexpr double ln_0_15 = -0x1.e5a9a7c3ac418p+0, ln_0_25 = -0x1.62e42fefa39efp+0;
 inline double pow_0_15(double y) { return exp(y * ln_0_15); }
 inline double pow_0_25(double y) { return exp(y * ln_0_25); }

 // sin and cos of a float angle (radians), computed in double and rounded once to float. Reduction by the nearest
 // multiple of pi/2 (fdlibm's pio2_1 + pio2_1t; k * pio2_1 is exact for |k| < 2^20), then fdlibm's __kernel_sin /
 // __kernel_cos polynomials on [-pi/4, pi/4] and the quadrant.
 struct sin_cos { float s, c; };
 inline sin_cos sincos(float angle)
 {
  const double x = angle;
  if (x != x || x - x != 0) { const float nan = std::numeric_limits<float>::quiet_NaN(); return {nan, nan}; }
  constexpr double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11;
  const double t = x * invpio2;
  if (!(t < 1048576.0 && t > -1048576.0)) return {0.f, 1.f}; // beyond 1.6e6 rad: not an AI angle
  const int k = static_cast<int>(t < 0 ? t - 0.5 : t + 0.5);
  const double r = (x - double(k) * pio2_1) - double(k) * pio2_1t;
  const double z = r * r;
  constexpr double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
                   S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
  constexpr double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
                   C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
  const double sr = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  const double s = r + (z * r) * (S1 + z * sr);
  const double cr = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  const double c = 1.0 - (0.5 * z - z * cr);
  switch (k & 3) {
  case 0: return {float(s), float(c)};
  case 1: return {float(c), float(-s)};
  case 2: return {float(-s), float(-c)};
  default: return {float(-c), float(s)};
  }
 }
}
