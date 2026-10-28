// Executes production scope and policy on synthetic events; no engine addresses run.
std::string trace_calls;
unsigned trace_queries = 0, trace_tracers = 0;
bool nested_trace = false;
void* trace_nested_event = nullptr;
void* trace_context = nullptr;
float __cdecl trace_clock() { trace_calls += "c"; return fixture_time; }
float __cdecl trace_random(bool simulation)
{
 check(simulation && x87_control_word() == det_math::game_x87_cw && _mm_getcsr() == det_math::default_mxcsr,
       "spread draws use the simulation stream inside the game FP environment");
 trace_calls += "r"; return .5f;
}
void __cdecl trace_frame(void*, void*, npc_semi_auto::bullet_frame* out)
{ *out = {{10, 20, 30}, {1, 0, 0}}; }
npc_semi_auto::bullet_contact trace_contact(void*, const npc_semi_auto::bullet_segment& segment)
{
 check(segment.from.x == -26 && segment.to.x == 560 && segment.extent.x == 0,
       "admitted bullet uses the zero-extent -36/+550 segment");
 ++trace_queries; trace_calls += "q"; return {};
}
void __cdecl trace_tracer(u32 effect, const npc_semi_auto::point* origin, const npc_semi_auto::point* ray,
                          u32 token, float parameter, u32 flags)
{
 using namespace npc_semi_auto;
 trace_calls += "t"; ++trace_tracers;
 check(effect == 77 && origin->x == 10 && ray->x == 550 && ray->y == 0 && token == 0x123 && parameter == 1 && flags == 0,
       "tracer receives the native muzzle, full ray and effect arguments");
 if (nested_trace) {
  nested_trace = false; auto* outer = current_bullet;
  bullet_event(trace_nested_event, nullptr, trace_context);
  check(current_bullet == outer, "unmarked nested event restores its outer scope");
 }
}
bool __fastcall trace_native_event(void*, void*, void* context)
{
 using namespace npc_semi_auto;
 std::array<std::byte, 0x270> frame{};
 field<point>(frame.data(), 0x110) = {10, 20, 30};
 field<point>(frame.data(), 0x50) = {1, 0, 0};
 field<point>(frame.data(), 0x98) = {-99, -99, -99}; // output scratch, not the direction
 field<float>(frame.data(), 0x214) = 550;
 field<u32>(frame.data(), 0x228) = 77;
 field<float>(frame.data(), 0x234) = 1;
 auto* actor = field<void*>(context, 0);
 const bool skip = begin_bullet_trace(actor, frame.data());
 const bool scoped = current_bullet && current_bullet->row;
 if (scoped) check(field<u32>(frame.data(), 0x228) == 0, "only this beam's duplicate tracer argument is cleared");
 else check(field<u32>(frame.data(), 0x228) == 77, "unmarked beam frame is untouched");
 check(skip_bullet_collision(actor, frame.data()) == skip, "marked shots replace both stock collision queries");
 if (!skip) { ++trace_queries; trace_calls += "q"; }
 return true;
}
void bullet_trace_cases()
{
 using namespace npc_semi_auto;
 const auto saved_weapons = weapons; const bool saved_applied = applied;
 const auto saved_event = event_function; const auto saved_clock = clock_function;
 const auto saved_random = random_function; const auto saved_tracer = tracer_function;
 const auto saved_frame = bullet_frame_function; const auto saved_contact = bullet_contact_function;
 std::array<std::byte, 0x800> actor{}; std::array<std::byte, 0x500> stats{};
 std::array<std::byte, 0x50> event{}, unmarked{}; std::array<void*, 6> context{};
 std::memcpy(stats.data() + 0x150, "traceguard", 11);
 field<void*>(actor.data(), 0x35c) = stats.data(); field<u32>(actor.data(), 0x1c) = 0x123;
 field<point>(actor.data(), 0x3ac) = {560, 20, 66};
 context[0] = actor.data(); trace_context = context.data(); trace_nested_event = unmarked.data();
 field<void*>(event.data(), 0x14) = reinterpret_cast<void*>(1);
 field<u32>(event.data(), 0x3c) = bullet_marker;
 const auto event_before = event;
 weapons = {*parse("traceguard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|3f000000|00000000")};
 applied = true; event_function = reinterpret_cast<event_t>(&trace_native_event);
 clock_function = &trace_clock; random_function = &trace_random; tracer_function = &trace_tracer;
 bullet_frame_function = &trace_frame; bullet_contact_function = &trace_contact;
 trace_states = {}; trace_queries = trace_tracers = 0;
 for (const auto [time, expected] : {std::pair<float, bool>{.49f, false}, {.5f, true}, {.66f, false}, {1.f, true}}) {
  fixture_time = time; trace_calls.clear();
  check(bullet_event(event.data(), nullptr, context.data()), "converted event retains successful presentation result");
  check(trace_calls == (expected ? "rrrtcq" : "rrrtc"), "three spread draws and tracer precede trace admission; rejected shot has no collision");
  check(current_bullet == nullptr, "event scope is withdrawn after every shot");
 }
 check(trace_queries == 2 && trace_tracers == 4 && event == event_before, "four shots retain four tracers, two collisions, and immutable event bytes");
 trace_calls.clear(); nested_trace = true; fixture_time = 1.16f;
 bullet_event(event.data(), nullptr, context.data());
 check(trace_calls == "rrrtqc", "unrelated nested beam keeps its native collision and does not consume outer gate");
 for (unsigned control = 0; control != 5; ++control) {
  auto candidate = event; applied = true;
  if (control == 0) field<u32>(candidate.data(), 0x3c) = 0;
  if (control == 1) field<unsigned char>(candidate.data(), 0x45) = 1;
  if (control == 2) applied = false;
  if (control == 3) weapons[0].flags = type_beam;
  if (control == 4) weapons[0].actor = "otherguard";
  trace_calls.clear(); bullet_event(candidate.data(), nullptr, context.data());
  check(trace_calls == "q", "unmarked, split, OFF, non-bullet and unlisted controls consume no extra RNG or timer");
  weapons[0].flags = flag_rapidfire;
 }
 weapons = saved_weapons; applied = saved_applied; event_function = saved_event; clock_function = saved_clock;
 random_function = saved_random; tracer_function = saved_tracer;
 bullet_frame_function = saved_frame; bullet_contact_function = saved_contact;
}

void* trace_expected_frame = nullptr;
bool __cdecl trace_noisy_begin(void* actor, void* frame)
{
 check(actor == fixture_ai && frame == trace_expected_frame, "muzzle bridge forwards actor and exact native stack frame");
 __asm { fninit }
 __asm { fld1 }
 __asm { xorps xmm0, xmm0 }
 return forced_result != 0;
}
bool __cdecl trace_noisy_skip(void* actor, void* frame)
{
 check(actor == fixture_ai && frame == trace_expected_frame, "collision bridge forwards actor and exact native stack frame");
 __asm { fninit }
 __asm { fld1 }
 __asm { xorps xmm0, xmm0 }
 return forced_result != 0;
}
u32 trace_argument = 0;
void* trace_bridge_return_address = nullptr;
__declspec(naked) void trace_collision_continue()
{
 __asm {
  pop trace_argument
  mov chosen_path, 0
  jmp dword ptr [trace_bridge_return_address]
 }
}
__declspec(naked) void trace_native_continue()
{
 __asm { mov chosen_path, 0 }
 __asm { jmp dword ptr [trace_bridge_return_address] }
}
__declspec(naked) void trace_skip_continue()
{
 __asm { mov chosen_path, 1 }
 __asm { jmp dword ptr [trace_bridge_return_address] }
}
__declspec(naked) void invoke_trace_bridge(void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x280
  mov trace_expected_frame, esp
  mov trace_bridge_return_address, offset trace_bridge_return
  mov edi, [esp + 0x294]
  mov byte ptr [esp + 0x238], 0
  mov eax, 12345678h
  mov ebx, 22334455h
  mov ecx, 34567890h
  mov edx, 45678901h
  cld
  stc
  pushfd
  pushad
  mov esi, esp
  lea edi, before
  mov ecx, 9
  rep movsd
  popad
  popfd
  fxsave fp_before
  jmp dword ptr [bridge]
 trace_bridge_return:
  fxsave fp_after
  pushfd
  pushad
  mov esi, esp
  lea edi, after
  mov ecx, 9
  rep movsd
  popad
  popfd
  add esp, 0x280
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
void bullet_bridge_cases()
{
 using namespace npc_semi_auto;
 const auto save_begin = begin_trace_function; const auto save_skip = skip_trace_function;
 const auto save_mr = muzzle_resume, save_ms = muzzle_skipped, save_cr = collision_resume, save_cs = collision_skipped;
 begin_trace_function = &trace_noisy_begin; skip_trace_function = &trace_noisy_skip;
 muzzle_resume = u32(reinterpret_cast<std::uintptr_t>(&trace_native_continue));
 muzzle_skipped = collision_skipped = u32(reinterpret_cast<std::uintptr_t>(&trace_skip_continue));
 collision_resume = u32(reinterpret_cast<std::uintptr_t>(&trace_collision_continue));
 fixture_ai = reinterpret_cast<void*>(0x12340000);
 for (unsigned which = 0; which != 2; ++which) for (int result : {0, 1}) {
  forced_result = result; trace_argument = 0;
  bridge = which ? reinterpret_cast<void*>(&bullet_collision_hook) : reinterpret_cast<void*>(&bullet_muzzle_hook);
  __asm { fninit }
  __asm { fld1 }
  __asm { pcmpeqd xmm0, xmm0 }
  invoke_trace_bridge(fixture_ai);
  auto expected = before;
  if (which && !result) expected[5] = u32(reinterpret_cast<std::uintptr_t>(trace_expected_frame)) + 0x50;
  if (!which && !result) expected[8] = after[8]; // displaced CMP legitimately changes flags
  check(after == expected, "bullet bridges preserve native registers/flags except displaced CMP/LEA");
  check(fp_before == fp_after && chosen_path == result, "bullet bridges preserve all FP/SSE state and select the right continuation");
  check(!which || result || trace_argument == expected[5], "native collision continuation consumes exactly the displaced pointer push");
  __asm { fninit }
 }
 begin_trace_function = save_begin; skip_trace_function = save_skip;
 muzzle_resume = save_mr; muzzle_skipped = save_ms; collision_resume = save_cr; collision_skipped = save_cs;
}

void source_trace_rows(const char* path)
{
 using namespace npc_semi_auto_rules;
 std::ifstream file(path); std::string expected, line;
 std::getline(file, expected); std::vector<weapon> rows;
 while (std::getline(file, line)) {
  const auto w = parse(line); check(w.has_value(), "source schema 4 row round trip");
  if (w) rows.push_back(*w);
 }
 check(rows.size() == 20 && canonicalize(rows), "all twenty source rows canonicalize");
 const auto digest = table_digest(rows); constexpr char hex[] = "0123456789abcdef"; std::string text;
 for (const auto byte : digest) { text += hex[byte >> 4]; text += hex[byte & 15]; }
 check(text == expected, "C++ full-row digest agrees with independently exported Python bytes");
 std::printf("  schema 4 source rows: %zu, digest %s\n", rows.size(), text.c_str());
}

// Run the retail post-aim beam body itself with invented vector dependencies.
// This catches stack-offset mistakes that a helper-only fixture cannot detect.
using trace_point = npc_semi_auto::point;
trace_point* __fastcall fragment_mul(trace_point* self, void*, trace_point* out, const float* k)
{ *out = {self->x * *k, self->y * *k, self->z * *k}; return out; }
trace_point* __fastcall fragment_add(trace_point* self, void*, trace_point* out, const trace_point* rhs)
{ *out = {self->x + rhs->x, self->y + rhs->y, self->z + rhs->z}; return out; }
trace_point* __fastcall fragment_sub(trace_point* self, void*, trace_point* out, const trace_point* rhs)
{ *out = {self->x - rhs->x, self->y - rhs->y, self->z - rhs->z}; return out; }
trace_point* __fastcall fragment_copy(trace_point* self, void*, const trace_point* rhs) { *self = *rhs; return self; }
float __fastcall fragment_length(trace_point* self, void*) { return self->x*self->x + self->y*self->y + self->z*self->z; }
void* trace_fragment = nullptr; void* trace_frame_copy = nullptr;
__declspec(naked) void fragment_done() { __asm { jmp dword ptr [trace_bridge_return_address] } }
__declspec(naked) void invoke_trace_fragment(void*, void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x280
  mov edx, [esp + 0x294]
  mov esi, [esp + 0x298]
  mov trace_frame_copy, esi
  mov edi, esp
  mov ecx, 0x9c
  rep movsd
  mov edi, edx
  xor ebx, ebx
  mov trace_bridge_return_address, offset fragment_return
  jmp dword ptr [trace_fragment]
 fragment_return:
  mov esi, esp
  mov edi, trace_frame_copy
  mov ecx, 0x9c
  rep movsd
  add esp, 0x280
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
int fragment_exception(EXCEPTION_POINTERS* exception)
{
 std::printf("native trace fixture exception %08X at %08X (original offset %08X) address %08X\n",
             unsigned(exception->ExceptionRecord->ExceptionCode), unsigned(exception->ContextRecord->Eip),
             unsigned(exception->ContextRecord->Eip - u32(reinterpret_cast<std::uintptr_t>(trace_fragment)) + 0x452225),
             unsigned(exception->ExceptionRecord->ExceptionInformation[1]));
 std::fflush(stdout); return EXCEPTION_EXECUTE_HANDLER;
}
bool safe_trace_fragment(void* actor, void* frame)
{
 __try { invoke_trace_fragment(actor, frame); return true; }
 __except(fragment_exception(GetExceptionInformation())) { return false; }
}
void native_rejected_trace(const char* path)
{
 using namespace npc_semi_auto;
 std::ifstream file(path, std::ios::binary);
 const std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), {});
 if (raw.size() < 0x1000) { check(false, "native trace retail input readable"); return; }
 const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
 const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
 const auto* sections = IMAGE_FIRST_SECTION(nt);
 constexpr u32 first = 0x452225, end = 0x452f11;
 std::vector<unsigned char> body;
 for (unsigned i = 0; i != nt->FileHeader.NumberOfSections; ++i) {
  const auto& sec = sections[i]; const u32 rva = first - nt->OptionalHeader.ImageBase;
  if (rva >= sec.VirtualAddress && rva + end - first <= sec.VirtualAddress + sec.SizeOfRawData) {
   const auto offset = sec.PointerToRawData + rva - sec.VirtualAddress;
   body.assign(raw.begin() + offset, raw.begin() + offset + end - first); break;
  }
 }
 check(body.size() == end - first, "native beam post-aim fragment is readable");
 if (body.empty()) return;
 auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, body.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
 check(code != nullptr, "native trace fragment memory"); if (!code) return;
 const auto address = [code](u32 va) { return u32(reinterpret_cast<std::uintptr_t>(code + va - first)); };
 const auto patch = [&](u32 va, const void* target, unsigned length) {
  const auto bytes = stun_rules::branch(address(va), target, false, length);
  std::copy(bytes.begin(), bytes.end(), body.begin() + va - first);
 };
 auto mul = &fragment_mul; auto add = &fragment_add; auto sub = &fragment_sub;
 auto copy = &fragment_copy; auto length = &fragment_length; float threshold = 1;
 for (const auto [old, replacement] : {std::pair<u32, const void*>{0x67f9f8, &mul}, {0x67fa10, &add},
          {0x67fa0c, &sub}, {0x67fa2c, &copy}, {0x67fa58, &length}, {0x682a2c, &threshold}}) {
  for (std::size_t i = 0; i + 4 <= body.size(); ++i) {
   u32 value = 0; std::memcpy(&value, body.data() + i, 4);
   if (value == old) { value = u32(reinterpret_cast<std::uintptr_t>(replacement)); std::memcpy(body.data() + i, &value, 4); i += 3; }
  }
 }
 patch(0x452225, &bullet_muzzle_hook, 7); patch(0x452350, &bullet_collision_hook, 5);
 patch(0x452e5c, &fragment_done, 7);
 std::memcpy(code, body.data(), body.size()); FlushInstructionCache(GetCurrentProcess(), code, body.size());
 const auto mr = muzzle_resume, ms = muzzle_skipped, cr = collision_resume, cs = collision_skipped;
 muzzle_resume = address(0x45222c); muzzle_skipped = address(0x45230b);
 collision_resume = address(0x452355); collision_skipped = address(0x4528a9);
 const auto clock_saved = clock_function; const auto random_saved = random_function; const auto tracer_saved = tracer_function;
 const auto frame_saved = bullet_frame_function;
 const auto contact_saved = bullet_contact_function;
 clock_function = &trace_clock; random_function = &trace_random; tracer_function = &trace_tracer;
 bullet_frame_function = &trace_frame;
 bullet_contact_function = &trace_contact;
 std::array<std::byte, 0x800> actor{}; std::array<std::byte, 0x80> native_move{};
 field<u32>(actor.data(), 0x1c) = 0x123;
 field<point>(actor.data(), 0x3ac) = {560, 20, 66};
 const auto row = *parse("traceguard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|3f000000|00000000");
 std::array<std::byte, 0x270> frame{};
 field<void*>(frame.data(), 0x20c) = native_move.data();
 field<point>(frame.data(), 0x44) = field<point>(frame.data(), 0x110) = {10, 20, 30};
 field<point>(frame.data(), 0x50) = {1, 0, 0}; field<point>(frame.data(), 0x98) = {-99, -99, -99};
 field<float>(frame.data(), 0x214) = 550; field<float>(frame.data(), 0x234) = 1;
 field<u32>(frame.data(), 0x228) = 77;
 auto* previous = current_bullet;
 trace_states = {}; trace_fragment = code;
 for (const auto [time, admitted] : {std::pair<float, bool>{.49f, false}, {.5f, true}, {.66f, false}}) {
  bullet_scope scope{actor.data(), &row}; current_bullet = &scope;
  fixture_time = time; trace_calls.clear(); field<u32>(frame.data(), 0x228) = 77;
  check(safe_trace_fragment(actor.data(), frame.data()), "native fragment executes without exception");
  check(scope.skip == !admitted && !scope.pending && trace_calls == (admitted ? "rrrtcq" : "rrrtc"),
        "executed native beam emits presentation then invokes only the admitted character ray");
  const auto endpoint = field<point>(native_move.data(), 0x40);
  check(endpoint.x == 560 && endpoint.y == 20 && endpoint.z == 30, "executed native endpoint math agrees with the early tracer's stack fields");
  check(field<u32>(frame.data(), 0x228) == 0, "executed native tail receives no duplicate tracer");
 }
 current_bullet = previous; clock_function = clock_saved; random_function = random_saved; tracer_function = tracer_saved;
 bullet_frame_function = frame_saved;
 bullet_contact_function = contact_saved;
 muzzle_resume = mr; muzzle_skipped = ms; collision_resume = cr; collision_skipped = cs;
 VirtualFree(code, 0, MEM_RELEASE);
}
