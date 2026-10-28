// Synthetic entry decisions and short service signatures; no game is launched.
#include <Windows.h>
#include "gameplay_capabilities.hpp"
#include "xml1_rogue_decide_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace {
using namespace xml1_rogue_decide_rules;
int failures=0, checks=0;
void check(bool b,const char* what) { ++checks; if(!b){++failures;std::printf("FAIL %s\n",what);} }
// Distinct invented objects: no game data. Service calls are recorded so ordering,
// one-hit selection and which object receives validTarget are observable.
int actor_object, victim_object, drain_object, empty_object, context_object;
void* actor=&actor_object; void* victim=&victim_object; void* drain=&drain_object; void* empty=&empty_object;
struct fixture {
 bool hit=true, ai=false, friendly=false, exclude=false, valid=true, ready=true, special_present=true;
 int victim_team=30, firsts=0, gates=0, flags=0x80, animation=-1;
 std::vector<std::string> calls;
 void* first(void*){++firsts;calls.push_back("first");return hit?victim:nullptr;}
 bool ai_attacker(void*){return ai;}
 int team(void* p){++gates;return p==victim?victim_team:29;}
 bool friendly_hit(){++gates;return friendly;}
 bool excluded(void* p){check(p==victim,"neutral victim passed to AI exclusion");++gates;return exclude;}
 void* chain(void*,void*,int action){calls.push_back(action==24?"special":"special2");return action==24?(special_present?drain:nullptr):empty;}
 bool valid_target(void* p){check(p==victim,"validTarget is on victim");calls.push_back("valid");return valid;}
 void lock(void*,void* p){check(p==victim,"lock victim");calls.push_back("lock");}
 bool may_start(void* p,void*){check(p==empty,"only Special2 asks mayStart");calls.push_back("mayStart");return ready;}
 void fail(void*){flags|=3;calls.push_back("flags");}
 void feedback(void*){calls.push_back("feedback");}
 void error_sound(){calls.push_back("sound");}
 void tag(void*,void*){calls.push_back("tag1");}
 void start(void*,void* move,void*){animation=move==drain?101:202;calls.push_back(move==drain?"drain":"no-target");}
 void run(){enter(*this,actor,&context_object);check(firsts==1,"exactly one first-contact read");}
};
void rules(){
 check(parse("")==setting::native && parse("off")==setting::native && parse("xml2")==setting::native,"OFF settings");
 check(parse("xml1")==setting::xml1 && parse("ON")==setting::xml1 && parse("wrong")==setting::invalid,"ON and invalid settings");
 auto end=endpoint({5,8,10},{0.6f,0.8f,0});check(end.x==41 && end.y==56 && end.z==10 && half_box==12,"60-unit center/facing sweep, box 12");
 fixture a;a.run();check(a.calls==std::vector<std::string>{"first","special","valid","lock","tag1","drain"} && a.animation==101,"victim branch order");
 fixture b;b.hit=false;b.run();check(b.calls==std::vector<std::string>{"first","special2","mayStart","tag1","no-target"},"no victim branch");
 fixture c;c.victim_team=28;c.exclude=true;c.run();check(c.animation==202 && c.firsts==1,"rejected first hit does not seek a second victim");
 fixture d;d.victim_team=29;d.run();check(d.animation==202,"same-team rejected without friendly override");
 fixture e;e.victim_team=29;e.friendly=true;e.run();check(e.animation==101,"friendly override accepts same team");
 fixture f;f.ai=true;f.victim_team=28;f.exclude=true;f.run();check(f.gates==0 && f.animation==101,"AI attacker bypasses both team and neutral gates");
 fixture g;g.valid=false;g.run();check(g.calls==std::vector<std::string>{"first","special","valid","tag1","drain"},"invalid target suppresses lock only");
 fixture h;h.hit=false;h.ready=false;h.run();check(h.calls==std::vector<std::string>{"first","special2","mayStart","flags","feedback","sound","tag1"} && h.flags==0x83 && h.animation==-1,"mayStart failure flags feedback sound then tag without start");
 fixture i;i.special_present=false;i.run();check(i.calls==std::vector<std::string>{"first","special","valid","lock","special2","mayStart","tag1","no-target"},"missing Special falls through to Special2");
 check(gameplay_capabilities::rogue_decide_id==43 && std::string(gameplay_capabilities::rule_name(43))=="Xml1RogueDecide","coordinator rule 43");
}
	// Every guard against the file's bytes (sections mapped by hand; nothing executes).
	void retail(const char* path)
	{
		std::ifstream f(path, std::ios::binary);
		std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)), {});
		if (raw.size() < 0x1000) { check(false, "readable retail executable"); return; }
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
		check(nt->OptionalHeader.ImageBase == limits_rules::image_base, "retail image base 0x400000");
		const auto* sections = IMAGE_FIRST_SECTION(nt);
		auto bytes = [&](std::uint32_t va) -> const unsigned char* {
			for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
			{
				const auto& s = sections[i];
				const std::uint32_t start = limits_rules::image_base + s.VirtualAddress;
				if (va >= start && va < start + s.SizeOfRawData) return raw.data() + s.PointerToRawData + (va - start);
			}
			return nullptr;
		};
		for (const auto& g : guards)
		{
			const auto* b = bytes(g.va);
			const bool ok = b && limits_rules::matches(b, g.hex);
			if (!ok) std::printf("  guard 0x%08lX %s\n", static_cast<unsigned long>(g.va), g.what);
			check(ok, "guard matches the retail executable");
		}
        for (const auto& g : query_guards) check(bytes(g.va) && limits_rules::matches(bytes(g.va), g.hex), "query slot/body retail bytes");
        for (const auto& e : query_entries) check(bytes(e.va) && limits_rules::matches(bytes(e.va), e.retail), "query bridge retail bytes");

		const auto* entry = bytes(lookup_va);
		check(entry && std::memcmp(entry, retail_entry.data(), retail_entry.size()) == 0, "the hook replaces exactly push esi; mov esi,[esp+8]");
		// The handler names really are absent from the game's registration (otherwise the hook would shadow them).
		const std::string blob(raw.begin(), raw.end());
        check(blob.find(std::string("ch_roguedecide") + '\0') == std::string::npos, "retail has no Rogue decide handler");
    }
}
int main(int argc,char** argv){rules();const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");
 if(path && *path)retail(path);else std::puts("SKIP retail bytes: no executable supplied");
 std::printf("%s rogue decide: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
