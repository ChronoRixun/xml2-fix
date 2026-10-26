#include "classic_hud.hpp"
#include "classic_hud_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include "limits_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "pad_prompts.hpp"
#include <intrin.h>
#include <filesystem>
#include <fstream>

namespace classic_hud
{
    namespace
    {
        using namespace classic_hud_rules;
        using primitive_t=void*(__thiscall*)(void*,const float*,const float*,const float*,float,float,int,int,int);
        using uv_t=const float*(__thiscall*)(void*,const unsigned char*);
        using wheel_t=void(__thiscall*)(void*);
        constexpr char atlas_name[]="textures/ui/lch.png";
        constexpr char panel_name[]="ui/hud/lc_healthpanel";
        constexpr char portrait_name[]="ui/hud/lc_partycross";
        bool installed=false, trace=false;
        struct wheel_context {
            const unsigned char* object=nullptr;
            int player=0,next=0;
            struct label {bool present=false;float x=0,z=0,alpha=0;bool locked=false;};
            std::array<label,4> labels{};
        };
        thread_local wheel_context* active_wheel=nullptr;

        // Only CHudTarget's primary name call is redirected. The native queue
        // copies the formatted string synchronously; no temporary pointer escapes.
        void __cdecl target_name_hook(void* queue,int font,int x,int y,int width,int height,
            float scale,int flags,const float* color,const char* source)
        {
            using prepare_t=bool(__thiscall*)(void*,int);
            using getter_t=void*(__cdecl*)();
            using measure_t=float(__thiscall*)(void*,const char*,float,int);
            using text_t=void(__cdecl*)(void*,int,int,int,int,int,float,int,const float*,const char*,...);
            if(!reinterpret_cast<prepare_t>(0x5f0fd0)(queue,font)) return;
            const auto font_id=*reinterpret_cast<const int*>(
                static_cast<const unsigned char*>(queue)+font*0x21634+0x2140c);
            if(font_id<0 || font_id>=4) return; // the native queue drops unavailable fonts
            auto* fonts=reinterpret_cast<getter_t>(0x597c20)();
            const auto measure=[&](const std::string& value) {
                return reinterpret_cast<measure_t>(0x596df0)(fonts,value.c_str(),scale,font_id);
            };
            const auto size=source ? strnlen(source,128) : 0;
            // CFontMgr already returns HUD coordinates, like the 224-unit bar.
            const auto fitted=fit_target_name(std::string_view(source ? source : "",size),216.f,measure);
            // This legacy wrapper forwards its varargs as one opaque argument.
            // Supply a literal format, escaping percent signs instead of using %s.
            std::string literal;
            for(char c:fitted) {literal+=c;if(c=='%') literal+='%';}
            reinterpret_cast<text_t>(0x5f1130)(queue,font,x,y,width,height,scale,flags,color,literal.c_str());
            if(trace && source && fitted!=std::string_view(source,size)) {
                logger::write_once("classic-hud-target-name-fit",
                    "classic HUD: target name fit %.6g -> %.6g within %.6g; native centre/font retained",
                    measure(std::string(source,size)),measure(fitted),216.f);
            }
        }

        void __fastcall wheel_hook(void* self,void*)
        {
            const auto* object=static_cast<const unsigned char*>(self);
            const int player=*reinterpret_cast<const int*>(object+0x1c);
            if(*reinterpret_cast<void* const*>(object+4)!=reinterpret_cast<void*>(0x81d7e0) ||
               player<0 || player>=4) {
                reinterpret_cast<wheel_t>(wheel_draw)(self);return;
            }
            wheel_context context;context.object=object;context.player=player;
            auto* previous=active_wheel;active_wheel=&context;
            reinterpret_cast<wheel_t>(wheel_draw)(self);
            active_wheel=previous;
            // Use the existing prompt service, including each player's current
            // binding and device. No console glyphs are copied into the artwork.
            using text_queue_t=void*(__thiscall*)(void*);
            using queue_text_t=void(__thiscall*)(void*,int,int,int,int,int,float,int,const float*,const char*);
            auto* queue=reinterpret_cast<text_queue_t>(0x4bdfa0)(reinterpret_cast<void*>(0x81d7e0));
            for(int i=0;i<4;++i) {
                const auto& label=context.labels[i];
                if(!label.present) continue;
                const char* text=pad_prompts::gameplay_power_label(player,i);
                if(!text || !*text) continue;
                const float color[]{1,1,1,label.alpha};
                reinterpret_cast<queue_text_t>(0x5f11b0)(queue,
                    0,static_cast<int>(label.x-30),static_cast<int>(label.z),60,14,.65f,9,color,text);
            }
        }

        const float* __fastcall coordinates_hook(void* self,void*,const unsigned char* sprite)
        {
            const auto* native=reinterpret_cast<uv_t>(coordinates)(self,sprite);
            const auto address=reinterpret_cast<std::uintptr_t>(self);
            if(!hud_batch_address(address)) return native;
            thread_local uv mapped;
            const auto id=*reinterpret_cast<const unsigned short*>(sprite+0x12);
            mapped=map_uv(id,native);
            if(trace && id<=small_inner) {
                static bool seen[3][small_inner+1]{};
                const auto batch=(address-hud_batch)/0x104;
                if(!seen[batch][id]) {
                    seen[batch][id]=true;
                    logger::write("classic HUD UV: batch %u icon %u stock %.6g %.6g mapped %.6g %.6g",
                        static_cast<unsigned>(batch),id,native[0],native[1],mapped[0],mapped[1]);
                }
            }
            return mapped.data();
        }

        __declspec(noinline) void* __fastcall primitive_hook(void* self,void*,const float* position,
            const float* size,const float* color,float rotation,float fraction,int icon,int channel,int skew)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const auto fill=hero_fill(caller);
            std::array<float,4> tint{};
            std::array<float,3> aligned{};
            std::array<float,2> dimensions{};
            if(active_wheel && caller==0x5a7599) {
                auto& context=*active_wheel;
                // Mirror the native ordered loop's validity checks. Extra quick
                // entries retain their native presentation and do not get labels.
                int index=context.next;
                for(;index<32;++index) {
                    const auto* entry=context.object+0x20+index*0x10;
                    if(wheel_entry_visible(*reinterpret_cast<const int*>(entry),
                        *reinterpret_cast<const std::uint32_t*>(entry+0xc))) break;
                }
                context.next=index+1;
                if(index>=1 && index<=4 && wheel_ring(caller,channel,icon,size) &&
                   std::isfinite(position[0]) && std::isfinite(position[2])) {
                    const auto* entry=context.object+0x20+index*0x10;
                    const bool locked=*reinterpret_cast<const int*>(entry)==1;
                    auto& label=context.labels[index-1];
                    label={true,position[0]+size[0]/2,
                        locked ? position[2]-size[1]/2-7 : position[2]-size[1]+3,color[3],locked};
                    tint={1,1,1,color[3]};color=tint.data();
                }
            }
            if(prompt_background(caller,channel,icon,size)) {
                // The native text service has already expanded device tokens
                // and measured all lines. Skin only its existing background.
                tint={1,1,1,color[3]};color=tint.data();icon=frame;skew=0;
            } else if(experience_draw(caller,channel,icon,size,fraction)) {
                // Original sprite 8 is a silver gradient, three pixels high.
                // Undo the native slant's XP displacement, retaining its width,
                // fraction, depth and alpha. Level digits remain native.
                aligned={position[0]+(skew<0 ? -5.f : 5.f),position[1],position[2]};
                position=aligned.data();
                dimensions={size[0],3};size=dimensions.data();
                tint={1,1,1,color[3]};color=tint.data();
                icon=experience;skew=0;
            } else if(channel==0 && icon==27 && fill>=0) {
                // The native routine has already computed clamped health/energy,
                // delayed damage values and its pulse. Keep its width, UV fraction,
                // alpha, depth and ordering; replace only the fill's art and slant.
                const float gain=std::max({color[0],color[1],color[2]});
                tint={gain,gain,gain,color[3]}; color=tint.data();
                if(fill==energy) {
                    aligned={position[0]+energy_offset(skew),position[1],position[2]};
                    position=aligned.data();
                }
                icon=fill; skew=0;
            } else if(channel==0 && target_draw(caller)) {
                thread_local target_box box;
                if(caller==0x599361) box=target_origin(position,size,skew);
                if(box.valid) {
                    aligned=target_position(box,caller,position[1]);position=aligned.data();
                    skew=0;
                    icon=target_art(box.width,caller);
                    if(caller==0x599361) {
                        tint={1,1,1,color[3]};color=tint.data();
                    }
                } else if(trace) {
                    logger::write_once("classic-hud-target-size","classic HUD: unsupported target size; native target geometry retained");
                }
            }
            if(trace && (fill>=0 || target_draw(caller))) {
                static unsigned hero_count=0, target_count=0;
                auto& count=target_draw(caller) ? target_count : hero_count;
                if(count++<32) logger::write("classic HUD draw: caller %08X icon %d channel %d position %.4g %.4g %.4g size %.4g %.4g fraction %.4g skew %d color %.4g %.4g %.4g %.4g",
                    static_cast<unsigned>(caller),icon,channel,position[0],position[1],position[2],size[0],size[1],fraction,skew,color[0],color[1],color[2],color[3]);
            }
            if(trace && fill<0 && !target_draw(caller)) {
                static std::array<std::uintptr_t,64> seen{};
                static unsigned used=0;
                if(used<seen.size() && std::find(seen.begin(),seen.begin()+used,caller)==seen.begin()+used) {
                    seen[used++]=caller;
                    logger::write("classic HUD other: caller %08X icon %d channel %d position %.4g %.4g %.4g size %.4g %.4g",
                        static_cast<unsigned>(caller),icon,channel,position[0],position[1],position[2],size[0],size[1]);
                }
            }
            return reinterpret_cast<primitive_t>(primitive)(self,position,size,color,rotation,fraction,icon,channel,skew);
        }

        bool assets(HMODULE game)
        {
            wchar_t path[32768]{};
            const auto size=GetModuleFileNameW(game,path,32768);
            if(!size || size>=32768) return false;
            const auto root=std::filesystem::path(path).parent_path();
            std::ifstream marker(root/L"lc-hud.txt");
            std::string line;std::getline(marker,line);
            return line=="LC-HUD-4" &&
                std::filesystem::is_regular_file(root/L"Textures/ui/lch.IGB") &&
                std::filesystem::is_regular_file(root/L"UI/hud/lc_healthpanel.IGB") &&
                std::filesystem::is_regular_file(root/L"UI/hud/lc_partycross.IGB") &&
                std::filesystem::is_regular_file(root/L"UI/hud/lc_healthpanel_pc.IGB") &&
                std::filesystem::is_regular_file(root/L"UI/hud/lc_partycross_pc.IGB");
        }
    }

    void install(HMODULE game)
    {
        if(installed) return;
        const auto option=ini::text(L"Game",L"ClassicHud").value_or("");
        if(option.empty() || option=="0" || option=="off") { logger::write("classic HUD: off");return; }
        if(option!="xml1" || reinterpret_cast<std::uintptr_t>(game)!=0x400000) {
            logger::write("classic HUD: unsupported option/image base; stock HUD retained");return;
        }
        try { if(!assets(game)) {logger::write("classic HUD: asset contract missing; stock HUD retained");return;} }
        catch(...) {logger::write("classic HUD: asset check failed; stock HUD retained");return;}
        xml1_hook_transaction::memory memory;
        // Read-only dependency guards, including call continuations used to scope
        // the adapter. The resistance-text region is deliberately untouched.
        const limits_rules::guard guards[]{
            {primitive,"f681305c080001","HUD primitive enabled gate"},
            {coordinates,"8a0d10188b00","native sprite UV table"},
            {0x5ee180,"568bf18b0d20398b00","native sprite queue"},
            {0x5a3933,"8b1150ff524c","health fill call"},
            {0x5a3a40,"8b11ff524c","health previous value call"},
            {0x5a3acc,"8b0152ff504c","health damage trail call"},
            {0x5a3c3c,"8b1150ff524c","energy fill call"},
            {0x5a3d4b,"8b11ff524c","energy previous value call"},
            {0x5a3dd7,"8b0152ff504c","energy trail call"},
            {0x59935e,"ff524c","target frame call"},
            {0x69de88,"000070c0","native energy slant displacement"},
            {0x599432,"ff504c","target interior call"},
            {0x599531,"ff524c","target trail call"},
            {0x5996a4,"ff524c","target health call"},
            {0x59e144,"ff524c","measured HUD prompt background call"},
            {0x5a3f5b,"ff554c","native XP draw call"},
            {0x69de8c,"00009242","native XP full width"},
            {0x5a3f1e,"c744242800000040","native XP height"},
            {wheel_draw,"81ec4c010000","native power-wheel draw entry"},
            {0x5a71e5,"8d7520","native wheel entry base"},
            {0x5a772b,"8b7c24384783c61083ff20","native wheel ordered loop"},
            {0x5a7223,"75078b4e0c85c9751f","native talent entry visibility"},
            {0x5a722c,"83f8017506837e0cff7f14","native locked entry visibility"},
            {0x5a7237,"83f8020f85eb0400008b460c85c0","native numbered entry visibility"},
            {0x5a7596,"ff504c","native power-wheel ring call"},
            {0x5a716b,"8b451c83f8ff","native power-wheel player field"},
            {0x69dd38,"a0df4b00","native HUD text service"},
            {0x5a748e,"8bc8e81b9d0400","native HUD text enqueue call"},
            {0x4bdfa0,"8d410cc3","native HUD text queue getter"},
            {0x5f11b0,"568b74240857568bf9","native HUD text enqueue entry"},
            {0x5a904f,"e8dc800400","target name text call"},
            {0x5a9030,"6a01680000803f6a126a64","target name alignment/scale/bounds"},
            {0x5f1130,"568b74240c578b7c240c","native formatted text queue"},
            {0x5f0fd0,"53578b7c240c8bd9","native HUD font preparation"},
            {0x597c20,"8a0d68d28100b80100000084c8","native font manager getter"},
            {0x596df0,"83ec1853568b74242485f6","native scaled text measurement"},
            {0x69d6d0,"f06d5900","native text measurement vtable"},
            {0x5f1173,"69f634160200","native font channel stride"},
            {0x5ef275,"8b860c140200","native selected font field"}
        };
        for(const auto& g:guards) {
            std::array<unsigned char,16> bytes{};
            if(!memory.read(g.va,bytes.data(),g.hex.size()/2) || !limits_rules::matches(bytes.data(),g.hex)) {
                logger::write("classic HUD: guard refused at %08X (%s); stock HUD retained",g.va,g.what);return;
            }
        }
        using namespace xml1_hook_transaction;
        std::vector<patch> writes;
        auto pointer=[&](std::uint32_t address,std::uint32_t before,const void* after) {
            std::vector<unsigned char> expected(4);std::memcpy(expected.data(),&before,4);
            writes.push_back({address,expected,pointer_bytes(after)});
        };
        pointer(primitive_slot,primitive,reinterpret_cast<const void*>(&primitive_hook));
        pointer(uv_slot,coordinates,reinterpret_cast<const void*>(&coordinates_hook));
        pointer(0x59a53a,0x69dc08,atlas_name);
        pointer(0x5a4376,0x69dec8,panel_name);
        pointer(0x5a438f,0x69deac,panel_name);
        pointer(0x5a5bcf,0x69df2c,portrait_name);
        pointer(wheel_slot,wheel_draw,reinterpret_cast<const void*>(&wheel_hook));
        std::vector<unsigned char> name_call{0xe8,0,0,0,0};
        const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&target_name_hook))-0x5a9054;
        std::memcpy(name_call.data()+1,&displacement,4);
        writes.push_back({0x5a904f,{0xe8,0xdc,0x80,0x04,0x00},name_call});
        bool safe=true;
        installed=apply(memory,writes,&safe);
        if(!installed) {
            if(!safe) game_version::refuse_startup();
            logger::write("classic HUD: transaction refused; safe rollback %d",int(safe));return;
        }
        trace=ini::flag(L"Test",L"ClassicHudTrace",false);
        logger::write("classic HUD: explicit atlas/geometry adapter enabled; native values and resistance text retained");
    }
}
