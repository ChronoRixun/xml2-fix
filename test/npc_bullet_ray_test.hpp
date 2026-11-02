// Owned-code fixtures: copy instructions from the supplied XBE, replace only
// dependencies/addresses, execute them in this test process. No game is started.
struct bullet_owned_code {
 std::vector<unsigned char> raw;
 unsigned char* code = nullptr;
 bullet_owned_code(const char* path) {
  std::ifstream f(path, std::ios::binary); raw.assign(std::istreambuf_iterator<char>(f), {});
  code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x5000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
 }
 ~bullet_owned_code() { if (code) VirtualFree(code, 0, MEM_RELEASE); }
 bool copy(u32 va, unsigned size, unsigned offset) {
  if (!code || raw.size() < 0x200) return false;
  const auto word = [&](unsigned at) { u32 v = 0; std::memcpy(&v, raw.data() + at, 4); return v; };
  const auto base = word(0x104), sections = word(0x120) - base;
  for (unsigned i = 0; i != word(0x11c); ++i) {
   const auto at = sections + i * 56, first = word(at + 4), bytes = word(at + 16), file = word(at + 12);
   if (va >= first && va - first + size <= bytes && file + va - first + size <= raw.size()) {
    std::memcpy(code + offset, raw.data() + file + va - first, size); return true;
   }
  }
  return false;
 }
 bool copy_pc(const char* path, u32 va, unsigned size, unsigned offset) {
  std::ifstream f(path, std::ios::binary); const std::vector<unsigned char> pc((std::istreambuf_iterator<char>(f)), {});
  if (!code || pc.size() < 0x1000) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pc.data());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(pc.data() + dos->e_lfanew);
  const auto* sections = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i != nt->FileHeader.NumberOfSections; ++i) {
   const auto& s = sections[i]; const auto rva = va - nt->OptionalHeader.ImageBase;
   if (rva >= s.VirtualAddress && rva - s.VirtualAddress + size <= s.SizeOfRawData) {
    std::memcpy(code + offset, pc.data() + s.PointerToRawData + rva - s.VirtualAddress, size); return true;
   }
  }
  return false;
 }
 void rel(unsigned instruction, const void* target, unsigned operand = 1, unsigned size = 5) {
  const auto delta = u32(reinterpret_cast<std::uintptr_t>(target)) - u32(reinterpret_cast<std::uintptr_t>(code + instruction + size));
  std::memcpy(code + instruction + operand, &delta, 4);
 }
 void absolute(unsigned at, const void* target) {
  const auto value = u32(reinterpret_cast<std::uintptr_t>(target)); std::memcpy(code + at, &value, 4);
 }
 void replace(unsigned first, unsigned end, u32 original, const void* target) {
  for (unsigned at = first; at + 4 <= end; ++at) {
   u32 v = 0; std::memcpy(&v, code + at, 4);
   if (v == original) { absolute(at, target); at += 3; }
  }
 }
};
void* source_bullet_fragment = nullptr;
void* source_bullet_return = nullptr;
__declspec(naked) void source_bullet_done() { __asm { jmp dword ptr [source_bullet_return] } }
__declspec(naked) void invoke_source_bullet_aim(void*, void*, void*, void*, void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x100
  mov esi, [esp + 0x114]
  mov eax, [esp + 0x118]
  mov [esp + 0x10], eax
  mov ebx, [esp + 0x11c]
  mov edi, [esp + 0x120]
  mov ebp, [esp + 0x124]
  mov source_bullet_return, offset source_aim_return
  jmp dword ptr [source_bullet_fragment]
 source_aim_return:
  add esp, 0x100
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
float source_accuracy = 0, source_speed = 0;
unsigned source_draw_count = 0;
std::array<double, 3> source_draws{};
float __fastcall source_accuracy_get(void*, void*) { return source_accuracy; }
float __fastcall source_speed_get(void*, void*) { return source_speed; }
double __cdecl source_spread_random() { return source_draws[source_draw_count++ % 3]; }
bool same_point(const npc_semi_auto::point& a, const npc_semi_auto::point& b)
{ return std::memcmp(&a, &b, sizeof a) == 0; }

void owned_bullet_direction(const char* path, const char* pc_path)
{
 using namespace npc_semi_auto;
 bullet_owned_code fixture(path);
 constexpr u32 start = 0xba0cd, finish = 0xba1d2;
 const bool loaded = fixture.copy(start, finish - start, 0) && fixture.copy(0x129c0, 0x3d, 0x1000) &&
                     fixture.copy(0x1233a0, 0x14, 0x1100) && fixture.copy(0x23fc50, 0x37, 0x1200);
 check(loaded, "owned XML1 direction, normalize, RNG range and lerp instructions readable");
 if (!loaded) return;
 for (const u32 call : {0xba12du, 0xba18eu}) fixture.rel(call - start, fixture.code + 0x1000);
 for (const u32 call : {0xba151u, 0xba15cu, 0xba167u}) fixture.rel(call - start, fixture.code + 0x1100);
 fixture.rel(0xba1c8 - start, fixture.code + 0x1200);
 fixture.rel(0x1100, &source_spread_random);
 fixture.rel(0xba1b9 - start, &source_bullet_done, 2, 6);
 fixture.rel(0xba1cd - start, &source_bullet_done);
 const float zero = 0, one = 1, height = 36, threshold = .9f;
 for (const auto [old, replacement] : {std::pair<u32, const void*>{0x3c6de4, &zero}, {0x3cb244, &height},
                                     {0x3c8390, &threshold}, {0x3c6e40, &one}})
  fixture.replace(0, 0x1300, old, replacement);
 FlushInstructionCache(GetCurrentProcess(), fixture.code, 0x5000);
 std::array<void*, 16> methods{};
 methods[6] = reinterpret_cast<void*>(&source_accuracy_get); methods[14] = reinterpret_cast<void*>(&source_speed_get);
 void* prototype = methods.data();
 source_bullet_fragment = fixture.code;
 std::array<std::byte, 0x400> actor{};
 unsigned compared = 0;
 det_math::fp_env fp;
 for (const auto forward : {point{1, 0, 0}, point{.6f, .8f, 0}, point{.2f, -.6f, .7f}})
 for (const auto cached : {point{310, 20, 30}, point{200, 170, 31}, point{-80, 20, 30}, point{10, 20, 580}})
  for (const float degrees : {0.f, 5.f, 17.f})
   for (const auto random : {std::array<double, 3>{.1, .5, .9}, {.9, .2, .1}, {.5, .5, .5}}) {
    const bullet_frame frame{{10, 20, 30}, forward};
    field<point>(actor.data(), 0x318) = cached;
    source_accuracy = float(double(degrees) * 0.01745329238474369);
    source_speed = 0; source_draw_count = 0; source_draws = random;
    point actual{};
    invoke_source_bullet_aim(actor.data(), &prototype, const_cast<point*>(&frame.muzzle), const_cast<point*>(&frame.forward), &actual);
    const auto candidate = bullet_direction(cached, frame, std::bit_cast<u32>(degrees), random);
    if (!same_point(actual, candidate))
     std::printf("  direction mismatch degrees %.0f: source %.9g %.9g %.9g, adapter %.9g %.9g %.9g\n",
                 degrees, actual.x, actual.y, actual.z, candidate.x, candidate.y, candidate.z);
    check(same_point(actual, candidate), "direction/spread/dot result equals executed XBE instructions bit for bit");
    check(source_draw_count == 3, "executed XBE direction consumes exactly three draws");
    ++compared;
   }
 std::printf("  XML1 character direction: %u owned-code comparisons\n", compared);
 // Enter directly at the dot test, preserving exact boundary inputs rather
 // than constructing approximate normalized vectors around the threshold.
 source_bullet_fragment = fixture.code + 0xba195 - start;
 point forward{1, 0, 0}, muzzle{};
 for (const float dot : {std::nextafter(.9f, 0.f), .9f, std::nextafter(.9f, 1.f), -1.f,
                         std::numeric_limits<float>::quiet_NaN()}) {
  point source{dot, .25f, -.5f}; const auto candidate = bullet_dot_adjust(source, forward);
  invoke_source_bullet_aim(actor.data(), &prototype, &muzzle, &forward, &source);
  check(same_point(source, candidate), "owned dot rule: below/equal/above .9, negative and unordered agree bit for bit");
 }
 // The RNGs return integer * 2^-31 in x87 ST0. Preserve all of those bits in
 // the three existing spread calls, rather than round each to a C++ float.
 const bool rng_loaded = fixture.copy(0x123330, 0x68, 0x1800) && fixture.copy_pc(pc_path, 0x560090, 0x4e, 0x2000);
 check(rng_loaded, "owned XBE and PC simulation RNG instructions readable");
 if (rng_loaded) {
  int x1_seed = 1, pc_seed = 1; u32 pc_count = 0;
  const float factor = 0x1p-31f;
  fixture.replace(0x1800, 0x1868, 0x50622c, &x1_seed); fixture.replace(0x1800, 0x1868, 0x3db11c, &factor);
  fixture.replace(0x2000, 0x204e, 0x6e0874, &pc_seed); fixture.replace(0x2000, 0x204e, 0x7ad8b4, &pc_count);
  fixture.replace(0x2000, 0x204e, 0x69ae2c, &factor);
  FlushInstructionCache(GetCurrentProcess(), fixture.code, 0x5000);
  const auto source_rng = reinterpret_cast<double(__cdecl*)()>(fixture.code + 0x1800);
  const auto pc_rng = reinterpret_cast<double(__cdecl*)(bool)>(fixture.code + 0x2000);
  const auto random_saved = random_function; const auto clock_saved = clock_function;
  const auto frame_saved = bullet_frame_function; auto* scope_saved = current_bullet;
  random_function = reinterpret_cast<decltype(random_function)>(fixture.code + 0x2000);
  clock_function = &trace_clock; bullet_frame_function = &trace_frame;
  const auto row = *parse("traceguard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|3f000000|40a00000");
  for (const int seed : {1, 12345, 987654321, 2147483646}) {
   x1_seed = pc_seed = seed; pc_count = 0;
   std::array<double, 3> x1_draws{};
   bool has_extra_bits = false;
   for (auto& draw : x1_draws) {
    draw = source_rng(); const double pc_draw = pc_rng(true);
    check(draw == pc_draw, "owned PC and XBE RNGs produce the identical full-precision draw");
    has_extra_bits |= double(float(draw)) != draw;
   }
   check(has_extra_bits && pc_count == 3 && x1_seed == pc_seed, "three native draws retain bits a float temporary would discard");
   source_bullet_fragment = fixture.code; source_draws = x1_draws; source_draw_count = 0;
   source_accuracy = float(5.0 * 0.01745329238474369);
   const bullet_frame frame{{10,20,30},{1,0,0}};
   field<point>(actor.data(), 0x318) = field<point>(actor.data(), 0x3ac) = {310.125f, 120.5f, 66.01f};
   point original{};
   invoke_source_bullet_aim(actor.data(), &prototype, const_cast<point*>(&frame.muzzle), const_cast<point*>(&frame.forward), &original);
   pc_seed = seed; pc_count = 0; fixture_time = 1; trace_states = {};
   std::array<std::byte, 0x270> beam{}; bullet_scope scope{actor.data(), &row}; current_bullet = &scope;
   begin_bullet_trace(actor.data(), beam.data());
   check(pc_count == 3 && same_point(original, field<point>(beam.data(), 0x50)),
         "production spread matches executed XBE aim with three actual PC RNG draws, bit for bit");
  }
  current_bullet = scope_saved; random_function = random_saved; clock_function = clock_saved; bullet_frame_function = frame_saved;
 }
}

npc_semi_auto::bullet_segment owned_segment{};
void __fastcall source_segment(void*, void*, const npc_semi_auto::point* from, const npc_semi_auto::point* to,
                                const npc_semi_auto::point* extent, int mode)
{
 check(mode == 1, "owned segment calls the source sweep mode 1");
 owned_segment = {*from, *to, *extent};
}
__declspec(naked) void invoke_source_bullet_segment(void*, void*, void*, void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x100
  mov ebp, [esp + 0x114]
  mov ebx, [esp + 0x118]
  mov eax, [esp + 0x11c]
  mov [esp + 0x14], eax
  mov eax, [esp + 0x120]
  mov [esp + 0x68], eax
  mov source_bullet_return, offset source_segment_return
  jmp dword ptr [source_bullet_fragment]
 source_segment_return:
  add esp, 0x100
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
void owned_bullet_endpoints(const char* path)
{
 using namespace npc_semi_auto;
 bullet_owned_code fixture(path);
 constexpr u32 start = 0xba5ba, end = 0xba677;
 check(fixture.copy(start, end - start, 0), "owned XML1 segment instructions readable");
 if (!fixture.code) return;
 fixture.code[end - start] = 0xe9;
 fixture.rel(end - start, &source_bullet_done);
 const float back = -36;
 fixture.replace(0, end - start, 0x3d37bc, &back);
 FlushInstructionCache(GetCurrentProcess(), fixture.code, 0x5000);
 std::array<void*, 0x78 / 4> methods{}; methods[0x74 / 4] = reinterpret_cast<void*>(&source_segment);
 void* query = methods.data();
 std::array<std::byte, 0x200> prototype{};
 field<short>(prototype.data(), 0x1c4) = 550;
 source_bullet_fragment = fixture.code;
 unsigned compared = 0;
 for (const auto muzzle : {point{10, 20, 30}, point{.003f, -100.01f, .000003f}, point{1024.01f, -512, 100.1f}})
  for (const auto direction : {point{1, 0, 0}, point{.1234567f, -.991234f, .057813f}, point{-.9f, .1f, .03f}}) {
   det_math::fp_env fp;
   invoke_source_bullet_segment(const_cast<point*>(&muzzle), const_cast<point*>(&direction), prototype.data(), &query);
   const auto candidate = bullet_endpoints({muzzle, {}}, direction, std::bit_cast<u32>(550.f));
   check(same_point(owned_segment.from, candidate.from) && same_point(owned_segment.to, candidate.to) &&
         same_point(owned_segment.extent, candidate.extent), "segment endpoints/extents equal executed XBE instructions bit for bit");
   ++compared;
  }
 std::printf("  XML1 character segment: %u owned-code comparisons\n", compared);
}

struct ray_candidate { void* entity; int team; bool actor, physent, ignored, collides; };
struct ray_query { void** methods = nullptr; std::array<bool, 1024> bits{}; bool busy = false; };
std::array<ray_candidate, 1024> ray_candidates{};
std::array<ray_query, 3> ray_queries{};
std::array<void*, 0x78 / 4> ray_query_methods{};
std::array<void*, 3> ray_order_methods{};
void* ray_order = ray_order_methods.data();
std::vector<unsigned> ray_order_indices;
std::array<std::byte, 0x40> ray_record{};
npc_semi_auto::bullet_segment ray_recorded_segment{};
int ray_team = 30;
bool ray_friendly = false, ray_game_mode = false, ray_order_busy = false;
unsigned ray_source_team_calls = 0, ray_contact_calls = 0, ray_query_acquired = 0, ray_query_released = 0;
std::string ray_filter_calls;
bool ray_pools() { return !ray_order_busy && std::any_of(ray_queries.begin(), ray_queries.end(), [](const auto& q) { return !q.busy; }); }
bool __cdecl ray_friendly_mode() { return ray_friendly || ray_game_mode; }
int __fastcall ray_get_team(void*, void*) { ++ray_source_team_calls; return ray_team; }
ray_query* ray_acquire() {
 for (auto& q : ray_queries) if (!q.busy) { q.busy = true; q.methods = ray_query_methods.data(); ++ray_query_acquired; return &q; }
 check(false, "query acquisition never reaches the exhausted pool"); return nullptr;
}
void __fastcall ray_create(void** out, void*, const void* cls, const u32* token)
{
 check(cls == reinterpret_cast<void*>(0x753f64) || cls == reinterpret_cast<void*>(0x4c0a5c), "bullet query selects physents");
 auto* q = ray_acquire(); *out = q;
 if (q) for (unsigned i = 0; i != q->bits.size(); ++i) q->bits[i] = i != (*token & 1023) && ray_candidates[i].physent;
}
void __fastcall ray_copy(void** out, void*, void** from)
{ auto* q = ray_acquire(); *out = q; if (q) q->bits = static_cast<ray_query*>(*from)->bits; }
void __fastcall ray_release(void** ref, void*)
{ check(*ref && static_cast<ray_query*>(*ref)->busy, "release owns a live query"); static_cast<ray_query*>(*ref)->busy = false; *ref = nullptr; ++ray_query_released; }
void __fastcall ray_filter(void* self, void*, int mode, int property)
{
 auto& q = *static_cast<ray_query*>(self);
 // Source property IDs > 1 map to PC's IDs (neutral +2, solid +1).
 if (property == 0xd) property = 0xe;
 if (property == 0x1a) property = 0x1c;
 ray_filter_calls += std::to_string(mode) + ":" + std::to_string(property) + ";";
 for (unsigned i = 0; i != q.bits.size(); ++i) {
  const auto& c = ray_candidates[i];
  const bool member = property == 1 ? c.ignored : property == 0xe ? c.collides : c.team == property;
  if (mode == 1 && member) q.bits[i] = false;
  if (mode == 2 && !member) q.bits[i] = false;
 }
}
void __fastcall ray_class_filter(void* self, void*, int mode, const void* descriptor)
{
 check(mode == 2 && (descriptor == reinterpret_cast<void*>(0x70b83c) || descriptor == reinterpret_cast<void*>(0x485874)), "neutral filter selects actors only");
 auto& q = *static_cast<ray_query*>(self);
 for (unsigned i = 0; i != q.bits.size(); ++i) q.bits[i] = q.bits[i] && ray_candidates[i].actor;
}
void __fastcall ray_subtract(void* self, void*, int mode, void** other)
{
 check(mode == 1, "source neutral actors are subtracted");
 auto& a = *static_cast<ray_query*>(self); const auto& b = *static_cast<ray_query*>(*other);
 for (unsigned i = 0; i != a.bits.size(); ++i) a.bits[i] = a.bits[i] && !b.bits[i];
}
void __fastcall ray_set_segment(void*, void*, const npc_semi_auto::point* from,
                                 const npc_semi_auto::point* to, const npc_semi_auto::point* extent)
{ ray_recorded_segment = {*from, *to, *extent}; }
int __fastcall ray_count(void*, void*) { return int(ray_order_indices.size()); }
void* __fastcall ray_item(void*, void*, int index)
{ check(index == 0, "character bullet requests first contact only"); return ray_candidates[ray_order_indices.at(index)].entity; }
void* __fastcall ray_contact_record(void*, void*, void** out, int index)
{ check(index == 0, "character bullet requests first contact record only"); *out = ray_record.data(); return out; }
void* __cdecl ray_order_create(void* q, const void* metric)
{
 using namespace npc_semi_auto;
 check(field<u32>(const_cast<void*>(metric), 0) == 0x68d684 && field<void*>(const_cast<void*>(metric), 4) == q,
       "contact order uses the owner query's time-of-impact metric");
 check(!ray_order_busy, "order pool has a free slot"); ray_order_busy = true;
 const auto requested = ray_order_indices;
 ray_order_indices.clear();
 for (const auto i : requested) if (static_cast<ray_query*>(q)->bits[i]) ray_order_indices.push_back(i);
 return &ray_order;
}
void __cdecl ray_order_release(void*) { check(ray_order_busy, "release owns the contact order"); ray_order_busy = false; }
bool ray_is_physent(void* entity, u32 descriptor)
{
 check(descriptor == 0x753f64, "direct hit uses XML1's physent class test");
 for (const auto& c : ray_candidates) if (c.entity == entity) return c.physent;
 return false;
}
void ray_reset()
{
 ray_candidates = {}; ray_queries = {}; ray_order_indices.clear(); ray_order_busy = false;
 ray_query_acquired = ray_query_released = ray_source_team_calls = ray_contact_calls = 0; ray_filter_calls.clear();
 ray_query_methods[0x40 / 4] = reinterpret_cast<void*>(&ray_filter);
 ray_query_methods[0x54 / 4] = reinterpret_cast<void*>(&ray_class_filter);
 ray_query_methods[0x34 / 4] = reinterpret_cast<void*>(&ray_subtract);
 ray_query_methods[0x74 / 4] = reinterpret_cast<void*>(&ray_set_segment);
 ray_order_methods = {reinterpret_cast<void*>(&ray_count), reinterpret_cast<void*>(&ray_item), reinterpret_cast<void*>(&ray_contact_record)};
 ray_order = ray_order_methods.data(); ray_friendly = ray_game_mode = false;
}
void* source_filter_query = nullptr;
LONG CALLBACK source_filter_exception(EXCEPTION_POINTERS* e)
{
 std::printf("owned filter exception %08X eip %08X fragment-offset %08X access %08X ecx %08X esp %08X\n",
  unsigned(e->ExceptionRecord->ExceptionCode), unsigned(e->ContextRecord->Eip),
  unsigned(e->ContextRecord->Eip - u32(reinterpret_cast<std::uintptr_t>(source_bullet_fragment))),
  unsigned(e->ExceptionRecord->ExceptionInformation[1]), unsigned(e->ContextRecord->Ecx), unsigned(e->ContextRecord->Esp));
 return EXCEPTION_CONTINUE_SEARCH;
}
__declspec(naked) void invoke_source_bullet_filter(void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x100
  mov edi, [esp + 0x114]
  mov source_bullet_return, offset source_filter_return
  jmp dword ptr [source_bullet_fragment]
 source_filter_return:
  mov eax, [esp + 0x68]
  mov source_filter_query, eax
  add esp, 0x100
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
bool __fastcall source_game_friendly(void*, void*) { return ray_game_mode; }
std::array<void*, 0x58 / 4> source_game_methods{};
void* source_game = source_game_methods.data();
void* __cdecl source_game_get() { return &source_game; }
int __fastcall source_team(void*, void*) { ++ray_source_team_calls; return ray_team == 0x1c ? 0x1a : ray_team; }
void owned_bullet_filters(const char* path)
{
 using namespace npc_semi_auto;
 bullet_owned_code fixture(path);
 constexpr u32 start = 0xba557, end = 0xba5ba, team = 0x59960;
 const bool loaded = fixture.copy(start, end - start, 0) && fixture.copy(team, 0xE3, 0x1000);
 check(loaded, "owned XML1 bullet first-contact filters and team helper readable");
 if (!loaded) return;
 fixture.code[end - start] = 0xe9; fixture.rel(end - start, &source_bullet_done);
 fixture.rel(0xba56c - start, &ray_create);
 fixture.rel(0xba582 - start, &source_game_get);
 fixture.rel(0xba598 - start, fixture.code + 0x1000);
 fixture.replace(0, end - start, 0x485840, &ray_friendly);
 for (const auto [call, target] : {std::pair<u32, const void*>{0x59983, &source_game_get}, {0x599bf, &source_team},
       {0x599d5, &source_team}, {0x599e9, &ray_copy}, {0x59a2d, &ray_release}})
  fixture.rel(0x1000 + call - team, target);
 source_game_methods[0x54 / 4] = reinterpret_cast<void*>(&source_game_friendly);
 FlushInstructionCache(GetCurrentProcess(), fixture.code, 0x5000);
 const auto create_saved = bullet_query_function; const auto release_saved = bullet_release_function;
 const auto copy_saved = bullet_copy_query_function; const auto pool_saved = bullet_pool_function;
 const auto friendly_saved = bullet_friendly_function; const auto team_saved = bullet_team_function;
 const auto order_saved = bullet_order_function; const auto order_release_saved = bullet_order_release_function;
 bullet_query_function = reinterpret_cast<bullet_query_t>(&ray_create);
 bullet_release_function = reinterpret_cast<bullet_release_t>(&ray_release);
 bullet_copy_query_function = reinterpret_cast<decltype(bullet_copy_query_function)>(&ray_copy);
 bullet_pool_function = &ray_pools; bullet_friendly_function = &ray_friendly_mode;
 bullet_team_function = reinterpret_cast<decltype(bullet_team_function)>(&ray_get_team);
 bullet_order_function = &ray_order_create; bullet_order_release_function = &ray_order_release;
 std::array<std::byte, 0x800> actor{}; field<u32>(actor.data(), 0x1c) = 900;
 for (const int team_id : {30, 0x1c}) for (unsigned mode = 0; mode != 3; ++mode) {
  ray_reset(); ray_team = team_id; ray_friendly = mode == 1; ray_game_mode = mode == 2;
  ray_candidates[900] = {actor.data(), team_id, true, true, false, true};
  ray_candidates[901] = {reinterpret_cast<void*>(2), team_id, true, true, false, true};
  ray_candidates[902] = {reinterpret_cast<void*>(3), 0x1c, true, true, false, true};
  ray_candidates[903] = {reinterpret_cast<void*>(4), 0x1c, false, true, false, true};
  ray_candidates[904] = {reinterpret_cast<void*>(5), 29, true, true, false, true};
  ray_candidates[905] = {reinterpret_cast<void*>(6), 29, false, true, true, true};
  ray_candidates[906] = {reinterpret_cast<void*>(7), 29, false, true, false, false};
  source_bullet_fragment = fixture.code;
  auto* exception = AddVectoredExceptionHandler(1, &source_filter_exception);
  invoke_source_bullet_filter(actor.data());
  RemoveVectoredExceptionHandler(exception);
  const auto source_bits = static_cast<ray_query*>(source_filter_query)->bits;
  const auto source_calls = ray_filter_calls;
  ray_release(&source_filter_query, nullptr);
  check(ray_query_acquired == ray_query_released, "owned filter fixture releases its primary and temporary queries");
  ray_query_acquired = ray_query_released = 0; ray_filter_calls.clear();
  ray_order_indices = {906, 905, 902, 903, 904};
  const auto contact = first_bullet_contact(actor.data(), {{0,0,0}, {550,0,0}, {}});
  (void)contact;
  check(source_bits == ray_queries[0].bits && source_calls == ray_filter_calls,
        "1024-slot adapter filters equal executed XBE filters, including self, team, neutral props and friendlyfire");
  check(ray_query_acquired == ray_query_released && !ray_order_busy, "adapter releases every query and result order");
  if (!mode) check(ray_order_indices.front() == 903, "neutral prop stops the bullet before the later enemy");
 }
 // A contact on the world is still first; no scan past it to a later actor.
 ray_reset(); ray_friendly = true;
 ray_candidates[1000] = {nullptr, 29, false, true, false, true};
 ray_candidates[1001] = {reinterpret_cast<void*>(9), 29, true, true, false, true};
 ray_order_indices = {1000, 1001}; field<point>(ray_record.data(), 0x30) = {50, 3, 2};
 const auto world = first_bullet_contact(actor.data(), {});
 check(world.found && !world.entity && world.position.x == 50, "first world contact survives without an entity pointer");
 ray_reset(); for (auto& q : ray_queries) q.busy = true;
 check(!first_bullet_contact(actor.data(), {}).found && ray_query_acquired == 0, "exhausted query pool is never acquired");
 ray_reset(); ray_order_busy = true;
 check(!first_bullet_contact(actor.data(), {}).found && ray_query_acquired == 0, "exhausted order pool is never acquired");
 ray_reset(); ray_queries[1].busy = ray_queries[2].busy = true;
 check(!first_bullet_contact(actor.data(), {}).found && ray_query_acquired == 1 && ray_query_released == 1,
       "no temporary team-filter slot fails closed and releases primary query");
 bullet_query_function = create_saved; bullet_release_function = release_saved; bullet_copy_query_function = copy_saved;
 bullet_pool_function = pool_saved; bullet_friendly_function = friendly_saved; bullet_team_function = team_saved;
 bullet_order_function = order_saved; bullet_order_release_function = order_release_saved;
 std::printf("  XML1 first-contact filters: 6 owned-code comparisons, 1024-slot and exhausted-pool controls\n");
}

void* source_hit_entity = nullptr;
std::array<std::byte, 0x40> source_concrete{};
std::array<std::byte, 0x6c> ray_hit_attack{};
unsigned ray_hits = 0;
unsigned char ray_material = 0;
void* __fastcall source_contact_entity(void*, void*) { return source_hit_entity; }
const npc_semi_auto::point* __fastcall source_contact_point(void*, void*)
{ return &npc_semi_auto::field<npc_semi_auto::point>(ray_record.data(), 0x30); }
const npc_semi_auto::point* __fastcall source_contact_normal(void*, void*)
{ return &npc_semi_auto::field<npc_semi_auto::point>(ray_record.data(), 0x20); }
void* __fastcall source_entity_concrete(void*, void*) { return source_concrete.data(); }
void __fastcall source_take_hit(void* entity, void*, void* attack, u32 a, u32 b, u32 c)
{
 check(entity == source_hit_entity && a == 1 && b == 1 && c == 1, "owned take-hit passes (attack,1,1,1) directly");
 std::memcpy(ray_hit_attack.data(), attack, ray_hit_attack.size()); ++ray_hits;
}
unsigned char* __fastcall source_hit_material(void*, void*) { return &ray_material; }
npc_semi_auto::bullet_contact ray_test_contact(void*, const npc_semi_auto::bullet_segment&)
{
 ++ray_contact_calls;
 return {source_hit_entity, *source_contact_point(nullptr, nullptr), *source_contact_normal(nullptr, nullptr), true};
}
void __cdecl ray_pc_take_hit(void* entity, void* attack)
{ source_take_hit(entity, nullptr, attack, 1, 1, 1); }
__declspec(naked) void invoke_source_bullet_hit(void*, void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x100
  mov ebx, [esp + 0x114]
  mov eax, [esp + 0x118]
  mov [esp + 0x64], eax
  mov source_bullet_return, offset source_hit_return
  jmp dword ptr [source_bullet_fragment]
 source_hit_return:
  add esp, 0x100
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
void owned_bullet_first_hit(const char* path)
{
 using namespace npc_semi_auto;
 bullet_owned_code fixture(path);
 constexpr u32 start = 0xba70d, end = 0xba7b6;
 const bool loaded = fixture.copy(start, end - start, 0);
 check(loaded, "owned XML1 first-contact/direct take-hit instructions readable");
 if (!loaded) return;
 fixture.code[end - start] = 0xe9; fixture.rel(end - start, &source_bullet_done);
 for (const u32 call : {0xba716u}) fixture.rel(call - start, &source_contact_entity);
 fixture.rel(0xba74e - start, &source_contact_point); fixture.rel(0xba76c - start, &source_contact_normal);
 u32 class_index = 0; fixture.replace(0, end - start, 0x499568, &class_index);
 // Both original short exits target the first impact-material instruction.
 fixture.rel(0xba71f - start, &source_bullet_done, 2, 6);
 fixture.code[0xba748 - start] = 0x74;
 // Keep the short branch local, targeting the appended E9 at 'end'.
 fixture.code[0xba749 - start] = static_cast<unsigned char>(end - (0xba748 + 2));
 FlushInstructionCache(GetCurrentProcess(), fixture.code, 0x5000);
 std::array<void*, 0x198 / 4> methods{};
 methods[0] = reinterpret_cast<void*>(&source_entity_concrete);
 methods[0xa8 / 4] = reinterpret_cast<void*>(&source_take_hit);
 methods[0x18c / 4] = reinterpret_cast<void*>(&source_hit_material);
 void* target = methods.data();
 std::array<std::byte, 0x800> actor{}; field<u32>(actor.data(), 0x1c) = 0x123;
 field<point>(ray_record.data(), 0x30) = {200, 20, 30}; field<point>(ray_record.data(), 0x20) = {-1, 0, .125f};
 const point direction{.98f, .01f, -.1f};
 const auto saved_contact = bullet_contact_function; const auto saved_class = bullet_class_function;
 const auto saved_hit = bullet_take_hit_function; auto* saved_scope = current_bullet;
 bullet_contact_function = &ray_test_contact; bullet_class_function = &ray_is_physent; bullet_take_hit_function = &ray_pc_take_hit;
 const auto row = *parse("traceguard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|3f000000|00000000");
 for (unsigned kind = 0; kind != 4; ++kind) {
  ray_reset(); source_hit_entity = kind == 3 ? nullptr : &target;
  const bool physent = kind < 2; // character / destructible prop / non-physent / world
  field<u32>(source_concrete.data(), 0x18) = physent ? 2u : 0;
  ray_candidates[1000] = {source_hit_entity, 29, kind == 0, physent, false, true};
  std::array<std::byte, 0x6c> original{};
  for (unsigned i = 0; i != original.size(); ++i) original[i] = std::byte(i + 1);
  auto source_attack = original; ray_hits = 0; source_bullet_fragment = fixture.code;
  invoke_source_bullet_hit(const_cast<point*>(&direction), source_attack.data());
  auto source_attack_hit = ray_hit_attack; const auto source_hits = ray_hits;
  std::array<std::byte, 0x270> beam{}; auto pc_attack = original; ray_hits = ray_contact_calls = 0;
  field<void*>(beam.data(), 0x20c) = pc_attack.data();
  bullet_scope scope{actor.data(), &row, false, false}; scope.ray_frame = {{10, 20, 30}, {1,0,0}}; scope.direction = direction;
  current_bullet = &scope;
  check(skip_bullet_collision(actor.data(), beam.data()), "marked collision always replaces the PC beam body");
  check(ray_hits == source_hits && ray_hits == unsigned(physent), "owned/adapter first contact: characters and props take hit, world and non-physents stop it");
  if (physent) {
   check(same_point(field<point>(source_attack_hit.data(), 0x20), field<point>(ray_hit_attack.data(), 0x40)) &&
         same_point(field<point>(source_attack_hit.data(), 0x2c), field<point>(ray_hit_attack.data(), 0x4c)) &&
         same_point(field<point>(source_attack_hit.data(), 0x38), field<point>(ray_hit_attack.data(), 0x58)),
         "direct hit's position/direction/normal match the executed XBE call");
   for (unsigned i = 0; i != original.size(); ++i)
    if (i < 0x40 || i >= 0x64) check(ray_hit_attack[i] == original[i], "damage/type/modifier bytes are unchanged");
  }
  check(ray_contact_calls == 1 && same_point(field<point>(beam.data(), 0xb8), *source_contact_point(nullptr, nullptr)),
        "first contact updates the presentation endpoint once");
  skip_bullet_collision(actor.data(), beam.data());
  check(ray_contact_calls == 1, "nested/repeated collision entry cannot apply the admitted trace twice");
  scope.collision_done = false; scope.skip = true;
  skip_bullet_collision(actor.data(), beam.data());
  check(ray_contact_calls == 1, "a rejected trace performs no contact query or hit");
 }
 current_bullet = saved_scope; bullet_contact_function = saved_contact; bullet_class_function = saved_class; bullet_take_hit_function = saved_hit;
 std::printf("  XML1 first contact/direct take-hit: 4 owned-code comparisons, damage bytes preserved\n");
}

npc_semi_auto::bullet_transform_api ray_transform_api{};
npc_semi_auto::bullet_matrix ray_actor_matrix{}, ray_gun_matrix{};
bool ray_actor_model_present = true, ray_actor_bolt_present = true, ray_gun_bolt_present = true;
void* ray_actor_model_ref = nullptr;
void* ray_gun_resource = nullptr;
void* __fastcall ray_model_ref(void*, void*) { return ray_actor_model_present ? &ray_actor_model_ref : nullptr; }
void* __fastcall ray_skeleton(void*, void*) { return reinterpret_cast<void*>(1); }
void* __fastcall ray_weapon_resource(void*, void*) { return ray_gun_resource; }
bool __cdecl ray_actor_bolt(void* model, int bolt, void* out, bool automatic, bool enabled)
{
 check(model == &ray_actor_model_ref && bolt == 7 && !automatic && enabled, "actor bolt uses the source hand binding and world-frame flags");
 check(x87_control_word() == det_math::game_x87_cw && _mm_getcsr() == det_math::default_mxcsr,
       "muzzle engine calls execute inside fp_env");
 if (ray_actor_bolt_present) std::memcpy(out, ray_actor_matrix.data(), sizeof ray_actor_matrix);
 return ray_actor_bolt_present;
}
bool __fastcall ray_gun_bolt(void*, void*, const char* name, void* out)
{
 check(std::strcmp(name, "Gun1") == 0, "weapon muzzle requests emitter zero's Gun1");
 if (ray_gun_bolt_present) std::memcpy(out, ray_gun_matrix.data(), sizeof ray_gun_matrix);
 return ray_gun_bolt_present;
}
npc_semi_auto::bullet_transform_api ray_transforms() { return ray_transform_api; }
void ray_set_cw(std::uint16_t cw) { __asm { fldcw cw } }
int __cdecl source_gun_name(char* out, const char* format, int emitter)
{
 check(format == reinterpret_cast<const char*>(0x3d4894) && emitter == 1, "owned muzzle formats Gun<emitter+1>");
 std::memcpy(out, "Gun1", 5); return 4;
}
void* source_frame_output = nullptr;
__declspec(naked) void invoke_source_gun_frame(void*, void*, void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  sub esp, 0x100
  mov eax, [esp + 0x11c]
  mov source_frame_output, eax
  mov esi, [esp + 0x118]
  lea edi, [esp + 0x30]
  mov ecx, 16
  cld
  rep movsd
  mov edi, [esp + 0x114]
  mov dword ptr [esp + 0xfc], 0
  mov source_bullet_return, offset source_frame_return
  jmp dword ptr [source_bullet_fragment]
 source_frame_return:
  lea esi, [esp + 0x30]
  mov edi, source_frame_output
  mov ecx, 16
  rep movsd
  add esp, 0x100
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
void owned_bullet_muzzle(const char* path)
{
 using namespace npc_semi_auto;
 bullet_owned_code fixture(path);
 constexpr u32 start = 0xba017, end = 0xba08c;
 const bool loaded = fixture.copy(start, end - start, 0) && fixture.copy(0x23dc50, 0x37, 0x1000) &&
                     fixture.copy(0x23e770, 0x112, 0x1100) && fixture.copy(0x23e460, 0x18, 0x1300) &&
                     fixture.copy(0x242470, 0x1e3, 0x1400) && fixture.copy(0x23e3a0, 0x67, 0x1700);
 check(loaded, "owned XML1 Gun1 composition and native matrix instructions readable"); if (!loaded) return;
 fixture.code[end - start] = 0xe9; fixture.rel(end - start, &source_bullet_done);
 fixture.rel(0xba01f - start, fixture.code + 0x1000); fixture.rel(0xba042 - start, &source_gun_name);
 fixture.rel(0xba075 - start, fixture.code + 0x1400); fixture.rel(0xba084 - start, fixture.code + 0x1700);
 const float zero = 0, one = 1;
 fixture.replace(0x1100, 0x1212, 0x3c6de4, &zero); fixture.replace(0x1100, 0x1212, 0x3c6e40, &one);
 FlushInstructionCache(GetCurrentProcess(), fixture.code, 0x5000);
 ray_transform_api = {reinterpret_cast<decltype(ray_transform_api.identity)>(fixture.code + 0x1000),
                      reinterpret_cast<decltype(ray_transform_api.rotation)>(fixture.code + 0x1100),
                      reinterpret_cast<decltype(ray_transform_api.translation)>(fixture.code + 0x1300),
                      reinterpret_cast<decltype(ray_transform_api.multiply)>(fixture.code + 0x1400), &ray_actor_bolt};
 const auto saved = bullet_transforms_function; bullet_transforms_function = &ray_transforms;
 std::array<void*, 0x18c / 4> actor_methods{};
 std::array<void*, 6> model_methods{}; model_methods[0x14 / 4] = reinterpret_cast<void*>(&ray_skeleton);
 void* model = model_methods.data(); ray_actor_model_ref = &model;
 actor_methods[0x188 / 4] = reinterpret_cast<void*>(&ray_model_ref);
 std::array<void*, 3> weapon_methods{}; weapon_methods[2] = reinterpret_cast<void*>(&ray_weapon_resource);
 void* weapon = weapon_methods.data();
 std::array<void*, 0x34 / 4> resource_methods{};
 resource_methods[0x28 / 4] = resource_methods[0x30 / 4] = reinterpret_cast<void*>(&ray_gun_bolt);
 void* resource = resource_methods.data(); ray_gun_resource = &resource;
 std::array<std::byte, 0x800> actor{}; field<void*>(actor.data(), 0) = actor_methods.data(); field<void*>(actor.data(), 0x7a8) = &weapon;
 field<point>(actor.data(), 0x20) = {30, 40, 50}; field<point>(actor.data(), 0x2c) = {.1f, -.2f, .7f};
 int bolt = 7; std::array<std::byte, 0x270> beam{}; field<int*>(beam.data(), 0x224) = &bolt;
 det_math::fp_env fp;
 unsigned compared = 0;
 source_bullet_fragment = fixture.code;
 auto* exception = AddVectoredExceptionHandler(1, &source_filter_exception);
 for (const point rotation : {point{0,0,0}, point{.1f, -.2f, .7f}, point{0, 0, 1.57079637f}}) {
  ray_transform_api.identity(ray_actor_matrix.data()); ray_transform_api.rotation(ray_actor_matrix.data(), rotation.x, rotation.y, rotation.z);
  point actor_position{100.25f, -99.125f, 60}; ray_transform_api.translation(ray_actor_matrix.data(), &actor_position);
  ray_transform_api.identity(ray_gun_matrix.data()); ray_transform_api.rotation(ray_gun_matrix.data(), 0, .12f, -.3f);
  point muzzle{28.5f, -2, 1.125f}; ray_transform_api.translation(ray_gun_matrix.data(), &muzzle);
  for (const bool has_gun_bolt : {false, true}) {
   ray_actor_model_present = ray_actor_bolt_present = true; ray_gun_bolt_present = has_gun_bolt;
   source_bullet_fragment = fixture.code; bullet_matrix original{};
   invoke_source_gun_frame(&weapon, ray_actor_matrix.data(), original.data());
   bullet_frame candidate{}; native_bullet_frame(actor.data(), beam.data(), &candidate);
   const bullet_frame expected{{original[12], original[13], original[14]}, {original[0], original[1], original[2]}};
   check(same_point(expected.muzzle, candidate.muzzle) && same_point(expected.forward, candidate.forward),
         "character Gun1 muzzle/forward equal the executed XBE composition"); ++compared;
  }
 }
 ray_actor_bolt_present = false; ray_gun_bolt_present = true;
 bullet_frame missing_hand{}; native_bullet_frame(actor.data(), beam.data(), &missing_hand);
 check(missing_hand.muzzle.x == ray_gun_matrix[12] && missing_hand.muzzle.y == ray_gun_matrix[13], "failed actor bolt uses identity before Gun1 composition");
 ray_actor_model_present = false;
 bullet_frame missing_model{}; native_bullet_frame(actor.data(), beam.data(), &missing_model);
 check(same_point(missing_model.muzzle, field<point>(actor.data(), 0x20)), "missing actor model falls back to the entity frame");
 ray_actor_model_present = ray_actor_bolt_present = ray_gun_bolt_present = true;
 const auto snapshot = [&](std::uint16_t cw, u32 csr) {
  const auto saved_cw = x87_control_word(); const auto saved_csr = _mm_getcsr();
  ray_set_cw(cw);
  _mm_setcsr(csr);
  bullet_frame frame{}; native_bullet_frame(actor.data(), beam.data(), &frame);
  const auto direction = bullet_direction({401.125f, -60.25f, 70.125f}, frame, std::bit_cast<u32>(5.f), {.1f, .7f, .3f});
  const auto segment = bullet_endpoints(frame, direction, std::bit_cast<u32>(550.f));
  std::array<u32, 18> bits{};
  std::memcpy(bits.data(), &frame, sizeof frame); std::memcpy(bits.data() + 6, &direction, sizeof direction);
  std::memcpy(bits.data() + 9, &segment, sizeof segment);
  const bool restored = x87_control_word() == cw && _mm_getcsr() == csr;
  ray_set_cw(saved_cw);
  _mm_setcsr(saved_csr);
  check(restored, "character ray restores hostile caller x87/MXCSR state");
  return bits;
 };
 const auto reference = snapshot(0x027f, 0x1f80);
 for (const unsigned precision : {0u, 0x200u, 0x300u})
  for (const unsigned rounding : {0u, 0x400u, 0x800u, 0xc00u})
   for (const u32 csr : {0x1f80u, 0x3f80u, 0x9fc0u})
    check(snapshot(std::uint16_t(0x7f | precision | rounding), csr) == reference,
          "Gun1, spread, dot rule and endpoints have identical bits across caller FP modes");
 bullet_transforms_function = saved;
 RemoveVectoredExceptionHandler(exception);
 std::printf("  XML1 character Gun1 muzzle: %u owned-code comparisons and missing-bolt/model controls\n", compared);
}
