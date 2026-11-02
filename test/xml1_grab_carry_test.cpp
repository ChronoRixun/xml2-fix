// Native guards and multiplayer agreement. Reads the owned executable only.
#include <Windows.h>
#include "xml1_grab_carry_rules.hpp"
#include "xml1_air_carry_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
namespace {
using namespace xml1_grab_carry_rules;
int checks=0, failures=0;
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
void rules(){
    for(const auto* name:{"grab","grabattack","PICKupobject","telekinesis","ABILITY_DRAIN"})check(retaining_prefix(name),"four-byte retention prefix");
    for(const auto* name:{"", "gra", "airgrab", "run", "attack", "xgrab", "idle"})check(!retaining_prefix(name),"unrelated and short move names release");
    check(!retaining_prefix(nullptr),"missing move name releases");
    check(parse("")==setting::native&&parse("xml2")==setting::native&&parse("OFF")==setting::native&&parse("0")==setting::native,"OFF settings");
    check(parse("xml1")==setting::xml1&&parse("typo")==setting::invalid,"ON and invalid settings");
    using namespace gameplay_capabilities;
    record before,off,on,on2,failed;
    before.finalize();off.finalize();
    on.add(grab_carry_id,agreement_revision,{1,feature_mask,0,0},true);on.finalize();
    on2.add(grab_carry_id,agreement_revision,{1,feature_mask,0,0},true);on2.finalize();
    failed.add(grab_carry_id,agreement_revision,{0,feature_mask,0,0},false);failed.finalize();
    check(std::equal(before.bytes().begin(),before.bytes().end(),off.bytes().begin()),"OFF canonical record byte-identical");
    check(on.compare(on2.bytes())==result::match,"ON peers agree");
    record old_ground;old_ground.add(grab_carry_id,1,{1,1,0,0},true);old_ground.finalize();
    check(on.compare(old_ground.bytes())!=result::match&&old_ground.compare(on.bytes())!=result::match,"ground-only candidate refuses the new lifecycle contract in both directions");
    record old_lifecycle;old_lifecycle.add(grab_carry_id,2,{1,7,0,0},true);old_lifecycle.finalize();
    check(on.compare(old_lifecycle.bytes())!=result::match&&old_lifecycle.compare(on.bytes())!=result::match,"revision2 mask7 refuses carry revision3 both directions");
    // [Game] Xml1FlyingCarry=0 (080h): mask 23; it agrees with itself and refuses the carry's mask 15 both ways.
    check(mask_for(true)==15&&mask_for(false)==23&&feature_mask_no_flying_carry==23,"flying carry masks 15 / 23");
    record nocarry,nocarry2;nocarry.add(grab_carry_id,agreement_revision,{1,mask_for(false),0,0},true);nocarry.finalize();
    nocarry2.add(grab_carry_id,agreement_revision,{1,mask_for(false),0,0},true);nocarry2.finalize();
    check(nocarry.compare(nocarry2.bytes())==result::match,"two Xml1FlyingCarry=0 peers agree");
    check(on.compare(nocarry.bytes())!=result::match&&nocarry.compare(on.bytes())!=result::match,"flying carry on / off refuse each other both directions");
    check(nocarry.compare(old_lifecycle.bytes())!=result::match&&old_lifecycle.compare(nocarry.bytes())!=result::match,"revision2 mask7 refuses Xml1FlyingCarry=0 both directions");
    difference diff;
    check(on.compare(off.bytes(),&diff)!=result::match&&diff.buckets==(1ull<<44),"ON/OFF refuses only rule45");
    char text[256]{};on.describe(diff.buckets,diff.local_only,true,text,sizeof text);
    check(std::strstr(text,"45 Xml1GrabCarry")!=nullptr,"ON names rule45");
    check(off.compare(on.bytes(),&diff)!=result::match,"OFF/ON refuses");
    off.describe(diff.buckets,diff.local_only,true,text,sizeof text);
    check(std::strstr(text,"45 Xml1GrabCarry (disabled on this PC)")!=nullptr,"OFF names disabled rule45");
    check(!failed.ready()&&failed.compare(on.bytes())==result::unavailable,"failed request cannot join");
}
void retail(const char* path){
    std::ifstream f(path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    if(bytes.size()<4096){check(false,"readable retail image");return;}
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data())->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    std::vector<limits_rules::guard> all(guards.begin(),guards.end());all.insert(all.end(),xml1_air_carry_rules::guards.begin(),xml1_air_carry_rules::guards.end());
    for(const auto& g:all){
        const unsigned char* b=nullptr;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];const auto rva=g.va-0x400000;
            if(rva>=s.VirtualAddress&&rva+g.hex.size()/2<=s.VirtualAddress+s.SizeOfRawData)b=bytes.data()+s.PointerToRawData+rva-s.VirtualAddress;
        }
        const bool ok=b&&limits_rules::matches(b,g.hex);if(!ok)std::printf("guard %08X %s\n",g.va,g.what);check(ok,"retail guard matches");
    }
    const std::string blob(bytes.begin(),bytes.end());
    check(blob.find(std::string("ch_grab_attack")+'\0')==std::string::npos,"retail lacks attack handler registration");
}
}
int main(int argc,char** argv){rules();const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");
    if(path&&*path)retail(path);else check(false,"retail executable is required");
    std::printf("%s grab carry rules: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
