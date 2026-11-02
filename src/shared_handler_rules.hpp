#pragma once
#include "limits_rules.hpp"
#include "placement_crush_rules.hpp"
#include "xml1_hook_transaction.hpp"
#include <array>
#include <string_view>

namespace shared_handler_rules {
using u32=std::uint32_t;
using placement_crush_rules::parse_rule;
using placement_crush_rules::rule;
inline constexpr std::array<const wchar_t*,7> keys{L"SkatingRule",L"TeleportLandingRule",L"PopupRule",L"PopupLandRule",L"ChargeRule",L"NightcrawlerRule",L"GambitRule"};
enum part : u32 { skating=1,teleport_landing=2,popup=4,popup_land=8,charge=16,nightcrawler=32,gambit=64,all=127 };
struct dependency { u32 parts;limits_rules::guard bytes;bool capacity_owned=false; };
inline constexpr dependency guards[]{
 {1,{0x693220,"80cf4e00","skating entry registration"}},
 {1,{0x69322c,"70c84e00","skating update registration"}},
 {2,{0x6937f0,"10004f00","teleport landing entry registration"}},
 {4,{0x6934b4,"000f4f00","popup decision registration"}},
 {4,{0x69388c,"000f4f00","air-spin decision registration"}},
 {32,{0x4f0626,"8be885ed747d8bcde8dd8bf3ff84c07472f686d8030000017569","Nightcrawler resolved target and actor-state inputs"}},
 {32,{0x4f065d,"8b4424788b48048b118bbec0030000ff92e00000008b178bd88b036a188bcf89442418ff9218010000508b4424188bcbff5014518b4d1c8bf88bc489088bce89642418e83bd2f3ff","Nightcrawler accepted-target continuation"}},
 {64,{0x4ed510,"51538b5c240c8b836c0700005556578d4c2418c64424100189442418e87f7ff7ff8bf0","Gambit native entry frame and actor register"}},
 {2,{0x427395,"c784248003000064366800c784248403000075000000","registered ea_power2 animation"}},
 {2,{0x4273d7,"c784249803000038366800c784249c03000078000000","registered ea_power3 animation"}},
 {2,{0x4273ed,"c78424a00300002c366800c78424a403000079000000","registered ea_power4 animation"}},
 {2,{0x42745b,"c78424c8030000e4356800c78424cc0300007e000000","registered ea_power6 animation"}},
 {16,{0x68012c,"0000803f","unchanged one-second charge contact cadence"}},
 {1,{0x4ec874,"d94020","PC elapsed-time x87 load"},false},
 {1,{0x4ec87f,"d88698070000d99698070000d986d0060000d81d2c176900dfe0f6c4057a2dd81d1c016900dfe0f6c44175228b1156ff520c6a016a008bcee8c4b3f3ff508bcee87cfaf3ff5e83c424c20800ddd8","PC low-speed time exit"},false},
 {1,{0x4ec8fd,"68f0166900","skating forward operand"},false},
 {1,{0x4ec943,"d815f8166900","skating upper lower-band compare"},false},
 {1,{0x4ec950,"d815fc166900","skating unchanged upper ceiling"},false},
 {1,{0x4ec96b,"c700000070c2","skating downward correction"},false},
 {1,{0x4ec984,"d81df4166900","skating bottom compare"},false},
 {1,{0x4ec99d,"c70000007042","skating upward correction"},false},
 {1,{0x4ecf95,"c786340400000ad7233c","skating PC entry coefficient"},false},
 {1,{0x4ecfe6,"898634040000","skating PC exit coefficient"},false},
 {1,{0x6916f0,"0000b94300004042000070420000f042","skating retail speed and thresholds"},false},
 {2,{0x4f0010,"8b44240c8b54240883ec1056578b7c241c505257e8a7bd00008b771885f67507b868196800eb16e84422110081e6ffffff008b4cb0048d84010840000068f424680050e80a25180083c40885c075406a016a0250c74424147c000000c74424187e000000c744241c7f00000089442420e85b0107008b54841483c40c6a006a00680000803f6a0c528bcfe8310cf4ff5f5e83c410c20c00","teleport landing entry and animation dispatch"},false},
 {2,{0x4fbdd0,"e88b40f6ff8b442404d998e8050000c20c00","base handler entry"},false},
 {2,{0x5601e0,"8a44240c568b742408578b7c","integer random callback"},false},
 // 11 bytes, not 12: the call target's entry only. Its next instruction (0x430cdb, cmp eax,0x10, the animation-set
 // cache bound) is [Limits] AnimEnums' site C13 (rule 48); with both on, the 12-byte guard refused rule 42.
 {2,{0x430cd0,"518b813c04000085c07c57","actor animation callback"},false},
 {4,{0x4f0f00,"83ec10568b742418578dbe200100006a028bcfff15d8f96700d9542408d81d30006800dfe0f6c441756a6a028d8e58010000ff15d8f96700d95c241c6a028d4e20ff15d8f96700d844241cd8a600020000d81dac736800dfe0f6c4417536578d4c2410ff15e0f967006a028d4c2410ff15c4f967008b4c2420d941208d54240cd80d74186900528bced86c240cd918e8ac26f8ff5f5e83c410c20800","popup and air-spin decision"},false},
 {8,{0x4f0fb5,"8b865c0300008a88ae02000084c9780a","popup landing conditional reset"},false},
 {8,{0x4f0fc5,"c786780200000000803f8a8e6407000080e13f80c940888e64070000","popup landing coefficient and flags"},false},
 {16,{0x4ebe20,"e83b40f7ff8b442404d88040070000c780f002000000000000d998e8050000c20c00","charge entry deadline"},false},
 {16,{0x4ebe50,"8b442404c780f002000000000000c20400","charge exit contact reset"},false},
 {16,{0x4ebe70,"538b5c240c568b74240c5753568bf9e88c0f010084c074278b8e780300008b0156ff500c84c074176a188bcee8dfbdf3ff5f8986bc0300005eb0015bc2080053568bcfe8980f01005f5e5bc20800","charge input callback"},false},
 {16,{0x6937a4,"30d74f0020be4e0050be4e0090bd4e0030bf4e0000c54f0070be4e00c0be4e00","charge eight callback slots"},false},
 {16,{0x4ebf30,"83ec7c568bb424840000008b86f00200005789442408e8153ff7ffd85c2408dfe0f6c4410f85d5000000e8013ff7ffd8052c0168000fbf8e38070000668b863a070000d99ef0020000894c2408668986bc0200008b461cdb4424088dbeb4020000c786c002000002000000c786c402000080000000d99eb8020000c786c8020000080002008907e87440f6ff8b108bc8ff52206a018986dc0200008a47446a0068800000006a010c018847446a00c786f4020000000000008a57446a005180ca028bc48857448d4c2434c70000000000e8ebd7f3ff578d4c241cc744245400000000e8997df6ffe81440f6ff8b108d4c241851568bc8ff52348886e2020000","charge contact callback through pre-event bridge"},false},
 {16,{0x4ec02f,"6683be4407000000","charge event insertion and movement compare"},false},
 {16,{0x4ec037,"7e4c8d562c528d44241050e8494907000fbf8e44070000894c241083c4088d54240cdb44240852d94424108bced8c9d95c2410d9442414d8c9d95c2414d9442418d8c9d95c2418ddd8e8bb75f8ff5f5e83c47cc20800","charge movement and epilogue"},false},
 {16,{0x4234ac,"66898638070000a15cd0690089863c07000066c7863a070000f401c78640070000000080bf","actor charge sound handle and duration initialization"},false},
 {16,{0x4eec97,"85c074548b8ed80600003b0d5cd0690075466a006804b66d006800b36d006a0050e89136180083c41485c0742b578b7818e8b3370a008b106800001643680080a244680000803f8d4e2051578bc8ff524c8986d80600005f","native equivalent CCESound cast and positional start"},false},
 {16,{0x4eed00,"a15cd06900568b7424083986d8060000743de869370a008b8ed80600008b10518bc8ff526084c07421e852370a008b8ed80600008b10518bc8ff52748b155cd069008996d80600005ec3a15cd0690089","native equivalent sound validity and stop"},false},
 {16,{0x592480,"64a1000000008a0da0d87f006aff","sound manager getter"},false},
 {16,{0x67234e,"ff2518f16700","MSVC dynamic cast import"},false},
 {16,{0x69d05c,"ffffffff","invalid sound handle"},false},
 {16,{0x6db300,"809c6a00000000002e3f415643436f6d6261744e6f64654576656e74","combat event type descriptor"},false},
 {16,{0x6db604,"809c6a00000000002e3f4156434345536f756e644040","sound event type descriptor"},false},
 {16,{0x45fe60,"8b0d348471008b01","game clock"},false},
 {16,{0x427c80,"8b4424048b91780300005150","actor action resolver"},false},
 {16,{0x42c340,"56578b7c240c85ff8bf17434","actor move dispatcher"},false},
 {16,{0x4fce50,"6aff689b52670064a1000000","default handler input"},false},
 {32,{0x4f04f3,"e848d9f9ff8b108bc8ff525c84c00f85bb010000","Nightcrawler PC game-state exclusion"},false},
 {32,{0x4f0640,"8bcee8d99bf2ff8bcd8bd8e8d09bf2ff3bd8755183bdd4030000ff7448","Nightcrawler PC ally gate"},false},
 {32,{0x41a220,"8b41048bd0c1ea1df6c2017407b81d000000eb208bd0c1ea1ef6c2017407b81e000000eb0fc1e81f2401f6d81bc083e00383c01cf6416c08741a83f81e741083f81f740b83f81d750bb81f000000c3b81d000000c3","team category mapping"},false},
 {32,{0x5252e0,"8a0d9c887900b80100000084c875","AI manager getter"},false},
 {32,{0x695d34,"704a5200","AI eligibility virtual slot"},false},
 {32,{0x524a70,"8b5424048b0152ff905802000085c0740d8a8079020000d0e82401c2040032c0c20400","AI eligibility token lookup and flag"},false},
 {64,{0x4ed5d7,"8b83c0030000","Gambit missing probe insertion"},false},
 {64,{0x4ed659,"8b168bceff92e80000008945008b44241050568bcbe8cdecf3ff5f5e5d5b59c20c00","Gambit selected move continuation"},false},
 {64,{0x4ed59c,"85f68b6c2420743385ff752f8b8bc00300008b01576a18ff9018010000508bcbe80ffbf2ff8bc8e8884401008bf085f6c6442410000f8582000000","Gambit held-object path and switch flag"},false},
 {64,{0x4ed696,"6a008d4c2438e8cfbefbff8b068d4c2424518bceff50248b10895424188b4804894c241c8b50088d462c508d4c24285189542428e8c13207008b10895424148b4804894c2418d94008d94424148b561cd80d4041680083c408d94424108d4c2408d80d4041680089542408d95c2410d9c9d80d40416800d95c2414d8442418d95c240cd9442410d844241cd95c2410d9442414d8442420d95c2414e87a7df7ff8b108d4c2434518d4c2428518bc8ff5228508d542414528d44242450518b4e1c8bc48908e801e2fbff83c41484c00f848a0000008d4c2434e84dbefbff84c0757d8d4c2434e810befbff8b401c85c08944240874698d4c2408e83c7df7ff84c0745c518d4c24388bfce8ecbdfbff8b501c8917e802c0f3ff83c40485c0743f80b81c0300000273366a006a5c8bc8e847ff050084c075278b8ec00300008b016a006a18ff9018010000508bcee8e9f8f2ff8bc8e8624201008bf885ff753c","PC related forward probe dependency mapping"},false},
 {64,{0x4a9590,"568bf1833e00","trace entity accessor"},true},
 {64,{0x4a95c0,"51568bf1833e00","trace player exclusion accessor"},true},
 {64,{0x4ab960,"6aff6838426700","forward box trace API"},true},
 {64,{0x4654b0,"568bf1e8682a0600","World token resolution"},false},
 {64,{0x4654d0,"568bf1e8482a0600","World token validity"},false},
 {64,{0x41fb10,"568d4c2408e8965904008bf085f6742a8b06578bceff108b1540b8700083c2248bca83e11fbf01000000d3e7c1fa05857c90145f74048bc65ec333c05ec3","actor token classification"},false},
 {64,{0x4297b0,"568d4c2408e8f6bc03008bf085f6742a8b06578bceff108b15683f750083c2248bca83e11fbf01000000d3e7c1fa05857c90145f74048bc65ec333c05ec3","physical token classification"},false},
 {64,{0x54d710,"8b44240883ec08508b4424108d5424045250e859fdffff83c408c20800","physical object affecter check"},false},
 {64,{0x560990,"8b4c2408d90530006800d941","angles to forward vector"},false},
 {64,{0x41d0d0,"8d8160030000c3cccccccccc","actor move style lookup"},false},
 {64,{0x501a50,"8b4424045556578b3833f685","style move resolution"},false},
};
inline std::vector<unsigned char> retail(u32 va,std::size_t n) {
 for(const auto& g:guards)if(va>=g.bytes.va && va-g.bytes.va+n<=g.bytes.hex.size()/2) {
  std::vector<unsigned char> b(n);for(std::size_t i=0;i<n;++i)b[i]=limits_rules::hex_byte(g.bytes.hex,i+va-g.bytes.va);return b;
 }
 return {};
}
inline std::vector<unsigned char> branch(u32 site,const void* target,std::size_t n) {
 std::vector<unsigned char> b(n,0x90);b[0]=0xe9;
 const auto rel=static_cast<u32>(reinterpret_cast<std::uintptr_t>(target))-(site+5);std::memcpy(b.data()+1,&rel,4);return b;
}
inline constexpr float forward=400.f,lower=36.f,upper_lower=48.f;
struct targets { const void *landing,*popup,*charge_enter,*charge_exit,*charge_input,*charge_update,*charge_event,*nightcrawler,*gambit; };
inline std::vector<xml1_hook_transaction::patch> patches(u32 mask,const targets& t) {
 std::vector<xml1_hook_transaction::patch> out;
 auto write=[&](u32 a,std::vector<unsigned char> after){out.push_back({a,retail(a,after.size()),std::move(after)});};
 auto nop=[&](u32 a,std::size_t n){write(a,std::vector<unsigned char>(n,0x90));};
 auto jump=[&](u32 a,const void* p,std::size_t n){write(a,branch(a,p,n));};
 auto operand=[&](u32 a,std::size_t n,std::size_t offset,const void* p){auto b=retail(a,n);const auto v=xml1_hook_transaction::pointer_bytes(p);std::copy(v.begin(),v.end(),b.begin()+offset);write(a,b);};
 if(mask&skating) {
  nop(0x4ec874,3);jump(0x4ec87f,reinterpret_cast<void*>(0x4ec8cd),6);
  operand(0x4ec8fd,5,1,&forward);operand(0x4ec943,6,2,&upper_lower);operand(0x4ec984,6,2,&lower);
  write(0x4ec96b,{0xc7,0,0,0,0x40,0xc2});write(0x4ec99d,{0xc7,0,0,0,0x40,0x42});
  nop(0x4ecf95,10);nop(0x4ecfe6,6);
 }
 if(mask&teleport_landing)jump(0x4f0010,t.landing,8);
 if(mask&popup)jump(0x4f0f00,t.popup,8);
 if(mask&popup_land)nop(0x4f0fb5,16);
 if(mask&charge) {
  jump(0x4ebe20,t.charge_enter,5);jump(0x4ebe50,t.charge_exit,14);jump(0x4ebe70,t.charge_input,5);
  write(0x6937b4,xml1_hook_transaction::pointer_bytes(t.charge_update));jump(0x4ec02f,t.charge_event,8);
 }
 if(mask&nightcrawler) { nop(0x4f04f3,20);jump(0x4f0640,t.nightcrawler,7); }
 if(mask&gambit)jump(0x4ed5d7,t.gambit,6);
 return out;
}
}
