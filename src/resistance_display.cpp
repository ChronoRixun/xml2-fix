#include "resistance_display.hpp"
#include "resistance_display_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"

namespace resistance_display
{
 namespace
 {
  bool applied=false,trace=false;
  template<class T> T native(std::uint32_t a) { return reinterpret_cast<T>(a); }
  using aggregate_t=unsigned char(__fastcall*)(void*,void*,int,float*,float*,const void*,int);
  using getter_t=void*(__cdecl*)();
  using text_t=const char*(__fastcall*)(void*,void*,int);
  using formatter_t=const char*(__fastcall*)(void*,void*,void*,int,const void*);
  struct query
  {
   void* actor;
   bool original=false;
   resistance_display_rules::range operator()(int attribute,unsigned mask,int mode)
   {
    // The native damage context starts at a hit event+0xc; damage bits are
    // context+0x10. No source/move is supplied, matching the original HUD's
    // default attack context. Query both bounds directly: 0x550410 would
    // draw an RNG value for a ranged affecter, changing combat's RNG stream.
    std::array<std::uint32_t,0x64/4> context{};
    context[4]=mask;
    resistance_display_rules::range value;
    value.low=value.high=mode==1 ? 1.f : 0.f;
    value.present=native<aggregate_t>(0x54ff80)(actor,nullptr,attribute,&value.low,&value.high,context.data(),mode)!=0;
    if ((attribute==39 || attribute==56) && value.present) original=true;
    return value;
   }
  };
  struct text
  {
   std::string operator()(int format,int word) const
   {
    auto* manager=native<getter_t>(0x4bdb30)();
    auto get=native<text_t>((*static_cast<const std::uint32_t* const*>(manager))[2]);
    // The PC retains the original three-scope summary at ID 103.
    if (!word) return get(manager,nullptr,format);
    const auto* pattern=get(manager,nullptr,format);
    const auto* name=get(manager,nullptr,word);
    const std::string source=pattern ? pattern : "";
    const auto at=source.find("%s");
    if (at==std::string::npos) return {};
    return source.substr(0,at)+(name ? name : "")+source.substr(at+2);
   }
  };
  void __fastcall append(char* buffer,void*,void* actor)
  {
   // Preserve every native formatter query, in its original order and with
   // the same arguments. Those calls may refresh caches or draw ranged values;
   // a display adapter must not remove their existing simulation side effects.
   std::array<std::string,7> stock;
   for (int i=0;i<7;++i)
   {
    auto* manager=native<getter_t>(0x543f80)();
    const auto* line=native<formatter_t>(0x53d9c0)(manager,nullptr,actor,13+i,nullptr);
    stock[i]=line ? line : "";
   }
   query q{actor};text t;
   const auto values=resistance_display_rules::sample(q);
   const auto rendered=q.original ? resistance_display_rules::render(values,t) : std::string{};
   // Native CHudTarget created a 120-byte, terminated subtitle buffer. Keep
   // its existing boost text and all drawing, target and health-bar gates.
   auto append_line=[&](const std::string& line)
   {
    const auto size=strnlen(buffer,120);
    if (size<120 && line.size()<120-size) std::memcpy(buffer+size,line.c_str(),line.size()+1);
   };
   if (q.original) append_line(rendered);
   // The first three native types are represented by the original subtitle
   // when scoped modifiers exist. Other PC-only types remain native, as does
   // the entire native display when no original damage modifier is present.
   for (std::size_t i=q.original ? 3u : 0u;i<stock.size();++i) append_line(stock[i]);
   if (trace)
   {
    static std::uint32_t last=0;
    static std::string previous;
    const auto handle=*reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(actor)+0x1c);
    if (last!=handle || previous!=rendered)
    {
     last=handle;previous=rendered;
     const auto* stats=*reinterpret_cast<const unsigned char* const*>(static_cast<const unsigned char*>(actor)+0x35c);
     logger::write("resistance display: target %08X stats %s scopes %d:%.6g/%.6g %d:%.6g/%.6g %d:%.6g/%.6g text <%s>",
      handle,stats ? reinterpret_cast<const char*>(stats+0x150) : "",values[0].present,values[0].low,values[0].high,
      values[1].present,values[1].low,values[1].high,values[2].present,values[2].low,values[2].high,rendered.c_str());
    }
   }
  }
 }
 bool installed() { return applied; }
 void install(HMODULE game)
 {
  if (applied) return;
  const auto mode=resistance_display_rules::parse(ini::text(L"Game",L"ResistanceDisplay").value_or(""));
  if (mode==resistance_display_rules::choice::off) return;
  if (mode!=resistance_display_rules::choice::xml1 || reinterpret_cast<std::uintptr_t>(game)!=0x400000)
  { logger::write("resistance display: invalid option or image base; native HUD retained");return; }
  xml1_hook_transaction::memory memory;
  // Depend on the read-only aggregate and localized text lookup used below.
  for (const auto& g:std::array<limits_rules::guard,5>{{
   {0x54ff80,"558bec83e4f883ec54837d18015356","affecter bounds query"},
   {0x4bdb30,"8a0d98337700b80100000084c8","localized text manager"},
   {0x543f80,"8a0d78b67900b80100000084c8","native powerup manager"},
   {0x53d9c0,"81ec0c010000a1f8386f005355","native resistance formatter"},
   {0x69786c,"c0d95300","native resistance formatter vtable"}}})
  {
   std::array<unsigned char,16> bytes{};
   if (!memory.read(g.va,bytes.data(),g.hex.size()/2) || !limits_rules::matches(bytes.data(),g.hex))
   { logger::write("resistance display: dependency guard refused; native HUD retained");return; }
  }
  bool safe=true;
  applied=xml1_hook_transaction::apply(memory,resistance_display_rules::patches(reinterpret_cast<const void*>(&append)),&safe);
  if (!applied)
  {
   if (!safe) game_version::refuse_startup();
   logger::write(safe ? "resistance display: guarded transaction refused; native HUD retained" :
                       "resistance display: rollback/protection verification failed; startup refused");return;
  }
  trace=ini::flag(L"Test",L"ResistanceDisplayTrace",false);
  logger::write("resistance display: original scoped labels; native queries preserved, no additional RNG draws");
 }
}
