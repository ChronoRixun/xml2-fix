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
