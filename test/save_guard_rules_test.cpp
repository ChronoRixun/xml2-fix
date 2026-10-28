// Original synthetic fixtures; no game data.
#include "save_guard_rules.hpp"
#include "save_guard_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace save_format_rules;
using namespace save_guard_rules;
int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while (false)

std::vector<std::uint8_t> fixture(std::uint8_t kind, std::uint32_t version)
{
	// A minimal framed META plus the required sections, using the existing rule-test fixture producer.
	std::vector<std::uint8_t> b(lc_header_size, 0);
	std::copy(lc_magic.begin(), lc_magic.end(), b.begin());
	b[8] = kind; b[9] = 0x11; b[10] = 1;
	auto u32 = [&](std::size_t p, std::uint32_t v) { for (unsigned i=0; i<4; ++i) b[p+i] = static_cast<std::uint8_t>(v >> (i*8)); };
	u32(0x24, version);
	u32(0x18, static_cast<std::uint32_t>(b.size()));
	// Unknown major: intact header needs-newer, without assuming that future sections can be decoded.
	b[10] = 2;
	u32(0x84, crc32(b.data(), 0x84));
	return b;
}
void put(const std::string& p, const std::vector<std::uint8_t>& b)
{ std::ofstream s(p, std::ios::binary); s.write(reinterpret_cast<const char*>(b.data()), b.size()); }
std::vector<std::uint8_t> get(const std::string& p)
{ std::vector<std::uint8_t> b; std::string e; save_format_rules::read_file(p,b,e); return b; }

int main()
{
    auto future=fixture(kind_campaign,writer_version(1,5,0,writer_kind_fix));
    future[10]=lc_format_major;
    const auto set_word=[&](unsigned at,std::uint32_t value){for(unsigned n=0;n<4;++n)future[at+n]=static_cast<std::uint8_t>(value>>(n*8));};
    set_word(0x10,8);set_word(0x84,crc32(future.data(),0x84));
    auto newer=inspect(future.data(),future.size(),kind_campaign);
    CHECK(newer.status==file_status::needs_newer);
    CHECK(newer_save_message(newer)=="This save was made by a newer Legends Classic build - update to load it");
    CHECK(newer.why=="required feature bits 8");
    const auto player_message=message_with_reason("Unable to load",newer_save_message(newer),256);
    CHECK(player_message.find("update to load it")!=std::string::npos&&player_message.find("feature bits")==std::string::npos);
    newer.needs_layout=true;newer.why="enable [Limits] SaveLayout=w2";
    CHECK(newer_save_message(newer)==newer.why);

    // Opt-in settings switched off (082f 13d): a save needing only bits this build has behind a setting names the
    // setting; bits this build does not know keep the newer-build text. Each combination: player text + slot label.
    {
        const std::string generic="This save was made by a newer Legends Classic build - update to load it";
        const std::string actors="This save was made with 96 actors turned on. Turn on Actors=96 ([Limits] in xml2-fix.ini) and restart to load it.";
        const std::string actors_w2="This save was made with 96 actors and the W2 save layout turned on. Turn on Actors=96 and SaveLayout=w2 ([Limits] in xml2-fix.ini) and restart to load it.";
        const std::string failed="This save was made with 96 actors turned on. Actors=96 is in xml2-fix.ini but did not start (missing prerequisite [Limits] ActorPools=124); fix that and restart to load it.";
        const std::string failed_w2="This save was made with 96 actors and the W2 save layout turned on. Actors=96 is in xml2-fix.ini but did not start (missing prerequisite [Limits] ActorPools=124); fix that, turn on SaveLayout=w2 ([Limits] in xml2-fix.ini) and restart to load it.";
        const std::string w2="this save needs the W2 save layout ([Limits] SaveLayout=w2); enable it or use the build that wrote it";
        struct row { std::uint32_t known, settings, required; bool requested; std::string text, label; };
        const std::vector<row> rows{
            {3,4,7,false,actors,"(needs Actors=96) "},             // 082f W2, Actors line absent: the live 13d case
            {1,4,5,false,actors,"(needs Actors=96) "},             // E10A2 build, E10A2 + Actors save
            {1,4,7,false,actors_w2,"(needs Actors=96 + the W2 save layout) "},
            {3,4,7,true,failed,"(needs Actors=96) "},              // Actors=96 set, refused at startup
            {1,4,7,true,failed_w2,"(needs Actors=96 + the W2 save layout) "},
            {1,4,3,false,w2,"(needs the W2 save layout) "},        // W2 alone: the named W2 refusal, unchanged
            {5,4,7,false,w2,"(needs the W2 save layout) "},        // Actors on, W2 off
            {3,4,0xf,false,generic,"(needs a newer version) "},    // bit 3 is unknown here: a newer build
            {3,4,0xb,false,generic,"(needs a newer version) "},
            {3,4,0x13,false,generic,"(needs a newer version) "},
            {0,4,7,false,generic,"(needs a newer version) "},      // retail layout: bit 0 is not a setting here
            {0,4,4,false,generic,"(needs a newer version) "},
            {3,0,7,false,generic,"(needs a newer version) "},      // a build without the E4 core: 0x4 is a newer build's
        };
        const auto known_before=lc_known_required_features;const auto settings_before=lc_setting_required_features;
        for(const auto& r:rows)
        {
            lc_known_required_features=r.known;lc_setting_required_features=r.settings;
            lc_actor_capacity96_requested=r.requested;lc_actor_capacity96_failure=r.requested?"missing prerequisite [Limits] ActorPools=124":"";
            auto file=future;set_word(0x10,r.required);set_word(0x84,crc32(future.data(),0x84));std::swap(file,future);
            const auto i=inspect(file.data(),file.size(),kind_campaign);
            const auto text=newer_save_message(i);
            const auto label=slot_text(i,false);
            const bool ok=i.status==file_status::needs_newer&&text==r.text&&label==r.label;
            CHECK(ok);
            if(!ok) std::cerr<<"  known "<<r.known<<" settings "<<r.settings<<" required "<<r.required<<": "<<text<<" | "<<label<<" | "<<i.why<<"\n";
            const auto dialog=message_with_reason("Load failed!",text,card_message_size);
            CHECK(dialog=="Load failed! - "+r.text&&dialog.size()+1<=card_message_size&&dialog.find("feature bits")==std::string::npos);
            CHECK(r.text==w2 ? i.why==w2 : i.why.find("required feature bits "+std::to_string(r.required))==0); // the log line keeps the bits (W2: its named reason, unchanged)
        }
        // A save this build can apply is not refused (the header-only fixture then fails its section table).
        for(const std::uint32_t known:{7u,5u})
        {
            lc_known_required_features=known;lc_setting_required_features=4;lc_actor_capacity96_requested=false;
            auto file=future;set_word(0x10,known);set_word(0x84,crc32(future.data(),0x84));std::swap(file,future);
            CHECK(inspect(file.data(),file.size(),kind_campaign).status!=file_status::needs_newer);
        }
        // The refused file is untouched: reading and naming it changes nothing, and a save over it keeps it as .newer.
        lc_known_required_features=3;lc_setting_required_features=4;lc_actor_capacity96_requested=false;lc_actor_capacity96_failure.clear();
        const auto dir=std::filesystem::temp_directory_path()/("lc-actors-"+std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        auto sections=guard_fixture::campaign_sections("synthetic 96-actor save");
        const auto current=guard_fixture::container(kind_campaign,sections);
        auto actor_save=current;
        std::memcpy(actor_save.data()+0x10,guard_fixture::le32(7).data(),4);
        std::memcpy(actor_save.data()+0x84,guard_fixture::le32(crc32(actor_save.data(),0x84)).data(),4);
        const auto path=(dir/"saveslot9.lcsave").string();
        put(path,actor_save);
        std::vector<std::uint8_t> read;std::string error;
        CHECK(save_format_rules::read_file(path,read,error)&&read==actor_save);
        const auto refused=inspect(read.data(),read.size(),kind_campaign);
        CHECK(refused.status==file_status::needs_newer&&newer_save_message(refused)==actors&&get(path)==actor_save&&!std::filesystem::exists(path+".newer"));
        CHECK(protected_source(refused,inspect(current.data(),current.size(),kind_campaign)));
        lc_known_required_features=3;
        CHECK(write_lc_atomically(path,current,error)&&get(path+".newer")==actor_save&&get(path)==current);
        std::filesystem::remove_all(dir);
        lc_known_required_features=known_before;lc_setting_required_features=settings_before;
        lc_actor_capacity96_requested=false;lc_actor_capacity96_failure.clear();
    }

	const auto old_writer = writer_version(1,4,0,writer_kind_fix);
	CHECK(newer_writer(writer_version(1,5,0,writer_kind_fix), old_writer));
	CHECK(newer_writer(writer_version(1,5,0,writer_kind_import), old_writer));
	CHECK(!newer_writer(writer_version(1,4,0,writer_kind_import), old_writer));
	CHECK(!newer_writer(writer_version(1,3,9,writer_kind_fix), old_writer));
	CHECK(newer_writer(writer_version(1,3,9,99), old_writer));
	refusal_latches l;
	l.refuse("campaign", "unknown identity");
	l.refuse("profile", "needs newer");
	CHECK(!l.reason("campaign").empty() && !l.reason("profile").empty() && l.reason("other").empty());
	l.successful_load("other");
	CHECK(!l.reason("campaign").empty());
	l.explicit_delete("campaign");
	CHECK(l.reason("campaign").empty() && !l.reason("profile").empty());
	l.successful_load("profile");
	CHECK(l.reason("profile").empty());
	CHECK(!startup_allowed(true,false) && startup_allowed(true,true) && startup_allowed(false,false));
	CHECK(!online_allowed(true, false) && online_allowed(true, true) && online_allowed(false, false));
	CHECK(registry_full(101,0,0) && registry_full(0,101,0) && registry_full(0,0,101));
	CHECK(registry_full(100,100,100));
	CHECK(!registry_full(99,99,99));
	const auto folder = std::filesystem::temp_directory_path() / ("lc-guards-" + std::to_string(GetCurrentProcessId()));
	std::filesystem::create_directories(folder);
	const auto count_dir = (folder / "count").string() + "\\";
	std::filesystem::create_directories(count_dir);
	CHECK(count_lc_slots(count_dir) == 0);
	put(count_dir + slot_file(0), fixture(kind_campaign,old_writer));
	put(count_dir + slot_file(0) + ".bak", fixture(kind_campaign,old_writer));
	put(count_dir + slot_file(1) + ".bak", fixture(kind_campaign,old_writer));
	put(count_dir + slot_file(2) + ".newer", fixture(kind_campaign,old_writer));
	put(count_dir + profile_file, fixture(kind_profile,old_writer));
	CHECK(count_lc_slots(count_dir) == 2);
	for (const auto kind : {kind_campaign, kind_profile})
	{
		const auto p = (folder / (kind == kind_profile ? "profile" : "campaign")).string();
		const auto src = fixture(kind, writer_version(1,5,0,writer_kind_fix));
		const auto different = fixture(kind, writer_version(1,6,0,writer_kind_fix));
		std::string error;
		put(p,src);
		// Direct preservation tests: the final writer requires a fully decoded current container.
		CHECK(preserve_newer(p,src,error) && get(p+".newer") == src && get(p) == src);
		CHECK(preserve_newer(p,src,error));
		CHECK(!preserve_newer(p,different,error) && get(p+".newer") == src && get(p) == src);
		std::filesystem::remove(p+".newer");
		for (auto point : {failure_point::create, failure_point::write, failure_point::flush, failure_point::verify, failure_point::newer_publish})
		{
			CHECK(!preserve_newer(p,src,error,point));
			CHECK(get(p) == src && !std::filesystem::exists(p+".newer"));
		}
		CHECK(preserve_newer(p,src,error));
		put(p,different);
		CHECK(!preserve_newer(p,different,error) && get(p) == different && get(p+".newer") == src);
	}
	for (const auto kind : {kind_campaign, kind_profile})
	{
		const auto p = (folder / (kind == kind_profile ? "profile-write" : "campaign-write")).string();
		auto sections = guard_fixture::campaign_sections("synthetic guarded save");
		if (kind == kind_profile)
		{
			sections.clear();
			for (auto tag : profile_required)
				sections.push_back({std::string(tag),1,0,section_required,
									tag == "META" ? guard_fixture::meta("synthetic profile",0,0) : guard_fixture::empty_section()});
		}
		const auto current = guard_fixture::container(kind,sections);
		auto newer = current;
		std::memcpy(newer.data()+0x24,guard_fixture::le32(writer_version(1,5,0,writer_kind_fix)).data(),4);
		std::memcpy(newer.data()+0x84,guard_fixture::le32(crc32(newer.data(),0x84)).data(),4);
		auto newer_incompatible = newer;
		newer_incompatible[10] = 2;
		std::memcpy(newer_incompatible.data()+0x84,guard_fixture::le32(crc32(newer_incompatible.data(),0x84)).data(),4);
		std::string error;
		put(p,newer);
		CHECK(write_lc_atomically(p,current,error));
		CHECK(get(p+".newer") == newer && get(p+".bak") == newer && get(p) == current);
		CHECK(write_lc_atomically(p,current,error));
		CHECK(get(p+".newer") == newer && get(p+".bak") == current);
		put(p,newer_incompatible);
		CHECK(!write_lc_atomically(p,current,error)); // collision with the earlier intact newer source
		CHECK(get(p) == newer_incompatible && get(p+".newer") == newer);
		std::filesystem::remove(p+".newer");
		CHECK(write_lc_atomically(p,current,error) && get(p+".newer") == newer_incompatible);
		std::filesystem::remove(p+".newer");
		for (auto point : {failure_point::create, failure_point::write, failure_point::flush, failure_point::verify,
						   failure_point::newer_publish, failure_point::backup_publish, failure_point::replace})
		{
			put(p,newer);
			put(p+".bak",current);
			std::filesystem::remove(p+".newer");
			CHECK(!write_lc_atomically(p,current,error,point));
			CHECK(error.find("injected") != std::string::npos);
			CHECK(get(p) == newer);
			if (point == failure_point::backup_publish || point == failure_point::replace)
				CHECK(get(p+".newer") == newer);
		}
		// A content update may keep the same DLL version; rollback must still preserve the source build.
		for (const auto offset : {0x28,0x2c})
		{
			std::filesystem::remove(p+".newer");
			put(p+".bak",current);
			auto later_build = current;
			const auto value = rd32(later_build.data()+offset)+1;
			std::memcpy(later_build.data()+offset,guard_fixture::le32(value).data(),4);
			std::memcpy(later_build.data()+0x84,guard_fixture::le32(crc32(later_build.data(),0x84)).data(),4);
			put(p,later_build);
			CHECK(write_lc_atomically(p,current,error) && get(p+".newer") == later_build);
			CHECK(write_lc_atomically(p,current,error) && get(p+".newer") == later_build);
		}
		// Recover protection when an earlier writer left a newer original only in .bak.
		std::filesystem::remove(p+".newer");
		put(p,current); put(p+".bak",newer);
		CHECK(write_lc_atomically(p,current,error) && get(p+".newer") == newer);
		auto wrong_kind = newer;
		wrong_kind[8] = kind == kind_profile ? kind_campaign : kind_profile;
		std::memcpy(wrong_kind.data()+0x84,guard_fixture::le32(crc32(wrong_kind.data(),0x84)).data(),4);
		put(p,wrong_kind);
		CHECK(!write_lc_atomically(p,current,error) && get(p) == wrong_kind);
		// A blocked backup publication or final rename leaves original filename and bytes in place.
		put(p,current);
		const HANDLE locked = CreateFileA(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
		CHECK(locked != INVALID_HANDLE_VALUE && !write_lc_atomically(p,newer,error));
		CHECK(get(p) == current);
		CloseHandle(locked);
	}
	std::filesystem::remove_all(folder);
	std::cout << "Guards: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
