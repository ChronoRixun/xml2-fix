// Synthetic trace timestamps: separate from the recoil/input fixtures.
void trace_gate_cases()
{
 using namespace npc_semi_auto_rules;
 trace_state s{};
 check(!trace_gate(s, 100, 1, 0.f, 0x3f000000), "initial zero last trace rejects before half a second");
 check(!trace_gate(s, 100, 1, .499f, 0x3f000000) && s.last == 0, "rejection does not move last trace");
 check(trace_gate(s, 100, 1, .5f, 0x3f000000) && s.last == .5f, "trace equality is admitted");
 check(!trace_gate(s, 100, 1, .66f, 0x3f000000), "recoil shot between traces is rejected");
 check(trace_gate(s, 100, 1, 1.f, 0x3f000000), "next equality admitted");
 s.last = -1;
 check(trace_gate(s, 100, 1, .1f, 0x3f000000) && s.last == -1, "negative timestamp bypasses without being updated");
 check(!trace_gate(s, 100, 2, .1f, 0x3f000000) && s.last == 0, "new incarnation starts at zero");
 s.last = 9;
 check(!trace_gate(s, 101, 2, .1f, 0x3f000000) && s.last == 0, "new actor starts at zero");
 s.last = 16777216.f;
 check(!trace_gate(s, 101, 2, 16777216.f, 0x3f000000), "equal large timestamps reject");
 check(trace_gate(s, 101, 2, 16777218.f, 0x3f000000), "binary32 timestamps subtract as doubles");
 s.last = 1;
 check(!trace_gate(s, 101, 2, .5f, 0x3f000000) && s.last == 1, "same incarnation clock rewind does not invent a trace");
 state recoil{}; rearm(recoil, 101, 2, 1.25f, .16f); released(recoil, 101, 2);
 check(!trace_gate(s, 101, 2, 1.25f, 0x3f000000), "semi entry and release do not reset trace state");
 check(trace_gate(s, 101, 2, 1.5f, 0x3f000000), "timer survives input transitions");
 for (unsigned pc : {_PC_24, _PC_53, _PC_64}) {
  unsigned saved = 0, ignored = 0; _controlfp_s(&saved, 0, 0);
  const auto original_csr = _mm_getcsr();
  _controlfp_s(&ignored, pc | _RC_DOWN, _MCW_PC | _MCW_RC);
  const auto cw = x87_control_word(); const auto csr = _mm_getcsr();
  trace_state env{101, 2, .5f};
  const bool equal = trace_gate(env, 101, 2, 1.f, 0x3f000000);
  check(equal && env.last == 1.f && x87_control_word() == cw && _mm_getcsr() == csr, "trace gate fixes and restores caller FP environment");
  _controlfp_s(&ignored, saved, _MCW_PC | _MCW_RC); _mm_setcsr(original_csr);
 }
}
