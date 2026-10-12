// Synthetic overlays and LC key records; no original content.
#include <WinSock2.h>
#include "save_content_guards.hpp"
#include "game_version.hpp"
#include "iat_hook.hpp"
#include "log.hpp"
#include "save_guard_rules.hpp"
#include "lcsave/internal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace lcsave::detail;
int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while (false)
void put(const std::filesystem::path& p) { std::filesystem::create_directories(p.parent_path()); std::ofstream(p) << "synthetic"; }
Value zero(const std::string& type)
{
	if (type == "str") return Value::text("");
	if (type == "f32") return Value::real(0);
	if (type.starts_with("bytes:")) return Value::bytes(std::string(std::stoi(type.substr(6)),0));
	const auto x = type.find('x');
	if (x != std::string::npos)
	{
		auto v = Value::list();
		for (int i=0; i<std::stoi(type.substr(x+1)); ++i) v.push(type.starts_with("f32") ? Value::real(0) : Value::integer(0));
		return v;
	}
	return Value::integer(0);
}
std::vector<std::uint8_t> profile(std::uint32_t id)
{
	std::vector<SectionOut> sections;
	for (const auto tag : save_format_rules::profile_required)
	{
		const auto* schema = section_schema(tag);
		auto data = Value::dict();
		for (std::size_t i=0; i<schema->head.count; ++i) data.set(schema->head.fields[i].name,zero(schema->head.fields[i].type));
		for (std::size_t i=0; i<schema->lists.count; ++i) data.set(schema->lists.fields[i].name,Value::list());
		sections.push_back({std::string(tag),1,section_minor(tag),1,data});
	}
	auto keys = Value::list();
	keys.push(Value::dict({{"id",Value::integer(id)},{"domain",Value::integer(1)},{"key",Value::text("synthetic/unknown")}}));
	sections.push_back({"KEYS",1,0,2,Value::dict({{"keys",keys}})});
	return write_file(save_format_rules::kind_profile,sections,{});
}
int main()
{
	namespace fs = std::filesystem;
	const auto dir = fs::temp_directory_path() / ("lc-content-guards-"+std::to_string(GetCurrentProcessId()));
	fs::create_directories(dir / "Data"); fs::create_directories(dir / "Maps");
	put(dir / "Data/fake.lcbind"); put(dir / "Maps/fictional/room.XMLB");
	std::string error;
	auto verify = [&](const std::wstring& relative) {
		const auto file = dir / "mods" / "Synthetic" / relative;
		put(file);
		return save_content_guards::verify_mod_files(dir,{{relative,file.wstring()}},error);
	};
	CHECK(verify(L"actors/fictional.igb"));
	CHECK(verify(L"ui/menus/fictional.XMLB"));
	CHECK(verify(L"scripts/fictional.py"));
	CHECK(!verify(L"Data/Fake.ENGB") && error.find("bind-covered") != std::string::npos);
	CHECK(error.find("Synthetic") != std::string::npos && error.find("Fake.ENGB") != std::string::npos);
	CHECK(!verify(L"Maps/fictional/room.engb"));
	CHECK(!verify(L"Data/talents/unknown.engb") && error.find("ID-0") != std::string::npos);
	CHECK(!verify(L"Data/lcsave/catalogue.lcc"));
	CHECK(!verify(L"Packages/fictional.pkgb") && error.find("fictional.pkgb") != std::string::npos);
	CHECK(save_content_guards::verify_keys(profile(0x01000001),error) && error.empty());
	CHECK(!save_content_guards::verify_keys(profile(0),error) && error.find("synthetic/unknown") != std::string::npos);
	CHECK(!save_content_guards::verify_keys(profile(0x80000001),error));
	// Exercise the actual game's IAT exclusion implementation on this test process, using its own ini.
	const auto ini = logger::module_dir() / "xml2-fix.ini";
	const bool had_ini = fs::exists(ini);
	std::string previous_ini;
	if (had_ini) { std::ifstream input(ini,std::ios::binary); previous_ini.assign(std::istreambuf_iterator<char>(input),{}); }
	WSADATA ws{};
	CHECK(WSAStartup(MAKEWORD(2,2),&ws) == 0);
	auto* native_socket = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("WS2_32.dll"),"socket"));
	for (const auto token : {"", "1.30", "too-long", "LC99"})
	{
		{ std::ofstream out(ini); out << "[Save]\nFormat=lc\n[Online]\nGameVersion=" << token << "\n"; }
		game_version::install(GetModuleHandleW(nullptr));
		CHECK(game_version::startup_allowed()); // exclusion is installed even though no version can be applied here
		const auto socket_result = ::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		CHECK(socket_result == INVALID_SOCKET && WSAGetLastError() == WSAEACCES);
		CHECK(iat_hook::hook(GetModuleHandleW(nullptr),"WS2_32.dll","socket",23,native_socket) != nullptr);
	}
	if (had_ini) { std::ofstream out(ini,std::ios::binary); out << previous_ini; } else fs::remove(ini);
	WSACleanup();
	fs::remove_all(dir);
	std::cout << "Content guards: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
