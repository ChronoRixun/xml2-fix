// V-TBD: exercise the engine capture API and the real restore consumer, with real mod lookup.
// Only invented packages/files; the child runs in an owned temporary folder, never a game.
#include "internal.hpp"
#include "mod_loader.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <map>
using namespace lcsave;
using namespace lcsave::detail;
namespace fs = std::filesystem;
int failures = 0, checks = 0;
std::string error;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cout << "FAIL line " << __LINE__ << ": " #x " error=" << error << "\n"; } } while(false)
	struct E
	{
		std::string tag;
		std::vector<std::pair<std::string, std::string>> attrs;
		std::vector<E> children;
		E& add(E e)
		{
			children.push_back(std::move(e));
			return children.back();
		}
	};

	// xmlb.encode: nodes depth first, then the string table (strings interned in first-use order)
	std::vector<std::uint8_t> encode_xmlb(const E& root)
	{
		std::vector<const E*> order;
		std::function<void(const E&)> collect = [&](const E& e)
		{
			order.push_back(&e);
			for (const auto& c : e.children) collect(c);
		};
		collect(root);
		std::map<const E*, std::uint32_t> offsets;
		std::uint32_t pos = 8;
		for (const auto* e : order)
		{
			offsets[e] = pos;
			pos += 16 + 8 * static_cast<std::uint32_t>(e->attrs.size());
		}
		std::map<std::string, std::uint32_t> strings;
		std::string table;
		auto s = [&](const std::string& text)
		{
			const auto f = strings.find(text);
			if (f != strings.end()) return f->second;
			const auto off = pos + static_cast<std::uint32_t>(table.size());
			strings[text] = off;
			table += text;
			table.push_back('\0');
			return off;
		};
		std::vector<std::uint8_t> out;
		auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k))); };
		u32(0x11B1);
		u32(1);
		std::function<void(const E&, const E*)> emit = [&](const E& e, const E* sibling)
		{
			const auto name = s(e.tag);
			std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
			for (const auto& [k, v] : e.attrs)
			{
				const auto ks = s(k);
				pairs.emplace_back(ks, s(v));
			}
			u32(name);
			u32(sibling ? offsets[sibling] : 0xFFFFFFFF);
			u32(e.children.empty() ? 0xFFFFFFFF : offsets[&e.children[0]]);
			u32(static_cast<std::uint32_t>(pairs.size()));
			for (const auto& [k, v] : pairs)
			{
				u32(k);
				u32(v);
			}
			for (std::size_t i = 0; i < e.children.size(); ++i) emit(e.children[i], i + 1 < e.children.size() ? &e.children[i + 1] : nullptr);
		};
		emit(root, nullptr);
		out.insert(out.end(), table.begin(), table.end());
		return out;
	}

fs::path root;
const std::string zone = "invented/room";
const fs::path relative = "Packages/generated/maps/invented/room.pkgb";
void put(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
 fs::create_directories(path.parent_path());
 { std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); }
 CHECK(fs::file_size(path)==bytes.size());
}
void text(const fs::path& path,const std::string& bytes) { put(path,{bytes.begin(),bytes.end()}); }
std::vector<std::uint8_t> package(const char* name,const char* cosmetic="one")
{
 return encode_xmlb(E{"packagedef",{{"invented-cosmetic",cosmetic}},{E{"xml",{{"filename",std::string("conversations/")+name}}, {}}}});
}
Tables fixture()
{
 auto data=std::make_shared<TablesData>();data->build=std::make_unique<Build>();data->build->data_root=root.wstring();Tables t;t.data=data;return t;
}
void reset()
{
 for(const auto* name:{"Conversations","Packages","mods"}) fs::remove_all(root/name);
 text(root/"Conversations/inventeda.engb","invented base conversation");
 text(root/"Conversations/inventedz.engb","invented alternate conversation");
 put(root/relative,package("inventeda"));
 mod_loader::install({},false);
}
ConversationSources sources(int& calls)
{
 ConversationSources s;s.lookup_root=mod_loader::game_root();s.mod_files=mod_loader::effective_files();
 s.resolve=[&calls](const std::wstring& path){++calls;return mod_loader::resolve_file(path);};
 const auto generation=mod_loader::generation();s.unchanged=[generation]{return generation!=0 && mod_loader::generation()==generation && mod_loader::matches_build(root.wstring());};return s;
}
std::vector<Hash> identities(const char* name)
{
 std::vector<Hash> result;for(int lane=0;lane<4;++lane)result.push_back(key_hash({name,std::to_string(lane)},Enc::latin1));return result;
}
Hash expected(const char* name) {return conversation_package_digest(key_hash1(zone,Enc::latin1),identities(name));}
Value record(const Hash& digest)
{
 auto heard=Value::list();for(int k=0;k<5;++k)heard.push(Value::integer(k==0?1:0));
 return Value::dict({{"conversation",heard},{"conversation_digest",Value::bytes(digest.data(),digest.size())}});
}
bool restore(const Tables& tables,const Hash& digest,u32 (&words)[5],std::vector<std::string>& notes)
{
 try{restore_conversation(record(digest),zone,*tables.data,words,notes);return true;}
 catch(const Error& e){std::cout<<"restore refusal: "<<e.message<<"\n";return false;}
}
void frame_refuses(const Tables& tables, const Hash& loaded_digest = expected("inventeda"))
{
 // Windows change completion is asynchronous. Until it arrives, captured marks
 // still belong to the already-loaded pinned package; never label them with new disk bytes.
 Hash digest{};std::string error;bool refused=false;const auto end=GetTickCount64()+2000;
 do
 {
  if (!prepared_conversation_digest(tables,zone,digest,error)) {refused=true;break;}
  CHECK(digest==loaded_digest);Sleep(1);
 } while(GetTickCount64()<end);
 CHECK(refused);CHECK(!error.empty());
 u32 words[5]{};std::vector<std::string> notes;CHECK(!restore(tables,expected("inventeda"),words,notes));
}

struct Locked
{
 HANDLE h;
 explicit Locked(const fs::path& p):h(CreateFileW(p.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr)){CHECK(h!=INVALID_HANDLE_VALUE);}
 ~Locked(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
int child()
{
 Hash digest{};
 // Before preparation the native capture API refuses without touching a package.
 reset();auto unprepared=fixture();
 {Locked lock(root/relative);CHECK(!prepared_conversation_digest(unprepared,zone,digest,error));CHECK(error.find("not prepared")!=std::string::npos);}
 // A base build prepares, then both called paths work even while package data is exclusively locked.
 {
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));const int prepared_calls=calls;
  Locked lock(root/relative);
  CHECK(prepared_conversation_digest(t,zone,digest,error));CHECK(digest==expected("inventeda"));
  CHECK(conversation_digest(t,zone,digest,error));CHECK(digest==expected("inventeda"));
  u32 words[5]{};std::vector<std::string> notes;CHECK(restore(t,digest,words,notes));CHECK(words[0]==1);CHECK(calls==prepared_calls);
  Hash historical{};CHECK(conversation_digest(fixture(),historical,error));CHECK(historical!=digest);
  CHECK(!conversation_digest(t,historical,error));CHECK(error.find("historical tree digests")!=std::string::npos);
  CHECK(!prepared_conversation_digest(t,"invented/unpinned",digest,error));CHECK(error.find("not pinned")!=std::string::npos);
 }
 // Actual mod_loader index/lookup: the later enabled package wins; restore must use that ranking too.
 reset();put(root/"mods/Early"/relative,package("inventeda"));put(root/"mods/Late"/relative,package("inventedz"));text(root/"mods/load-order.txt","+Early\n+Late\n");mod_loader::install({},false);
 {
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));
  CHECK(mod_loader::resolve_file((root/relative).wstring())==(root/"mods/Late"/relative).lexically_normal().wstring());
  CHECK(t.data->build->conversations->packages.at(zone).effective_path==(root/"mods/Late"/relative).lexically_normal().wstring());
  const int prepared_calls=calls;Locked base(root/relative),early(root/"mods/Early"/relative),late(root/"mods/Late"/relative);
  CHECK(prepared_conversation_digest(t,zone,digest,error));CHECK(digest==expected("inventedz"));
  u32 words[5]{};std::vector<std::string> notes;CHECK(restore(t,digest,words,notes));CHECK(words[0]==1);
  std::const_pointer_cast<TablesData>(t.data)->conversation_tables[{expected("inventeda"),key_hash1(zone,Enc::latin1)}]=identities("inventeda");
  CHECK(restore(t,expected("inventeda"),words,notes));CHECK(words[0]==0);CHECK(!notes.empty());CHECK(calls==prepared_calls);
 }
 // Ranking mutation outside Conversations: old provenance refuses, a fresh preparation uses the new package.
 reset();{
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));put(root/relative,package("inventedz"));frame_refuses(t);
  auto fresh=fixture();CHECK(prepare_conversation_packages(fresh,sources(calls),error));CHECK(prepared_conversation_digest(fresh,zone,digest,error));CHECK(digest==expected("inventedz"));
 }
 // A package-byte mutation with unchanged ranking is still invalidated.
 reset();{
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));put(root/relative,package("inventeda","two"));frame_refuses(t);
 }
 // Creating a new mod root/package is detected, including when no mods existed at pin time.
 reset();{
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));put(root/"mods/New"/relative,package("inventedz"));frame_refuses(t);
  mod_loader::install({},false);auto fresh=fixture();CHECK(prepare_conversation_packages(fresh,sources(calls),error));CHECK(prepared_conversation_digest(fresh,zone,digest,error));CHECK(digest==expected("inventedz"));
 }
 // Mod order changes refuse immediately, before re-index; a fresh index/table pins the new winner.
 reset();put(root/"mods/Early"/relative,package("inventeda"));put(root/"mods/Late"/relative,package("inventedz"));text(root/"mods/load-order.txt","+Early\n+Late\n");mod_loader::install({},false);
 {
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));text(root/"mods/load-order.txt","+Late\n+Early\n");frame_refuses(t,expected("inventedz"));
  mod_loader::install({},false);auto fresh=fixture();CHECK(prepare_conversation_packages(fresh,sources(calls),error));CHECK(prepared_conversation_digest(fresh,zone,digest,error));CHECK(digest==expected("inventeda"));
 }
 // Changing loader generation without changing disk is also a fail-closed boundary.
 reset();{
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));mod_loader::install({},false);frame_refuses(t);
 }
 // Corrupt or inaccessible packages fail preparation and never restore through the offline reader.
 reset();text(root/relative,"invented invalid binary package");{
  int calls=0;auto t=fixture();CHECK(!prepare_conversation_packages(t,sources(calls),error));Locked lock(root/relative);frame_refuses(t);
 }
 reset();{
  int calls=0;auto t=fixture();Locked lock(root/relative);CHECK(!prepare_conversation_packages(t,sources(calls),error));frame_refuses(t);
 }
 // Unsupported conversation replacement topology is explicitly refused.
 reset();text(root/"mods/Override/Conversations/inventeda.engb","unsupported invented replacement");mod_loader::install({},false);{
  int calls=0;auto t=fixture();CHECK(!prepare_conversation_packages(t,sources(calls),error));CHECK(error.find("branch topology")!=std::string::npos);frame_refuses(t);
 }
 // Reject a resolver whose effective package is outside every watched dependency root.
 reset();{
  int calls=0;auto t=fixture();auto s=sources(calls);s.resolve=[](const auto&){return (root/"Unwatched/package.pkgb").wstring();};CHECK(!prepare_conversation_packages(t,s,error));CHECK(error.find("outside watched")!=std::string::npos);frame_refuses(t);
 }
 // A mismatched executable/table root or working directory cannot use cached provenance.
 reset();{
  int calls=0;auto t=fixture();auto s=sources(calls);s.lookup_root=(root/"different-root").wstring();
  CHECK(!prepare_conversation_packages(t,s,error));CHECK(error.find("different game folders")!=std::string::npos);frame_refuses(t);
 }
 reset();{
  int calls=0;auto t=fixture();CHECK(prepare_conversation_packages(t,sources(calls),error));
  fs::current_path(root/"Conversations");frame_refuses(t);fs::current_path(root);
 }
 // A mutation during synchronous preparation must not publish a completed cache.
 reset();{
  int calls=0;auto t=fixture();auto s=sources(calls);s.resolve=[](const auto& path){text(root/"mods/load-order.txt","+Invented\n");return path;};CHECK(!prepare_conversation_packages(t,s,error));CHECK(error.find("changed during preparation")!=std::string::npos);frame_refuses(t);
 }
 reset();{
  auto t=fixture();ConversationSources missing;CHECK(!prepare_conversation_packages(t,missing,error));CHECK(error.find("lookup is not ready")!=std::string::npos);frame_refuses(t);
 }
 std::cout<<"pinned package capture/restore: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
int main(int argc,char** argv)
{
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,std::size(path));
 if(argc==2 && std::string(argv[1])=="--prepare-root")
 {
  root=fs::path(path).parent_path();mod_loader::install({},false);int calls=0;auto tables=fixture();
  const auto start=GetTickCount64();const bool ok=prepare_conversation_packages(tables,sources(calls),error);
  std::cout<<"owned build package preparation: "<<ok<<", packages "<<tables.data->build->conversations->packages.size()<<", "<<GetTickCount64()-start<<" ms; "<<error<<"\n";
  if(!ok)return 1;
  const auto before=calls;
  for(const auto& [name,pkg]:tables.data->build->conversations->packages)
  {
   Hash out{};if(!prepared_conversation_digest(tables,name,out,error) || out!=pkg.digest)return 1;
   u32 words[5]{};std::vector<std::string> notes;
   try{restore_conversation(record(pkg.digest),name,*tables.data,words,notes);}catch(const Error& e){std::cerr<<e.message<<"\n";return 1;}
  }
  std::cout<<"all prepared capture/restore paths passed; additional resolver calls "<<calls-before<<"\n";
  return calls==before?0:1;
 }

 if(argc==2 && std::string(argv[1])=="--child")
 {
  root=fs::path(path).parent_path();
  FILE* stream=nullptr;freopen_s(&stream,"behavior.log","w",stdout);std::cout<<std::unitbuf;
  try{return child();}catch(const Error& e){std::cerr<<e.message<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
 }
 // Use the real loader's executable-root semantics in a disposable, owned process.
 const auto folder=fs::temp_directory_path()/("xml2-conversation-pin-"+std::to_string(GetCurrentProcessId()));
 if(fs::exists(folder))return 1;fs::create_directories(folder);const auto exe=folder/"pin-test.exe";fs::copy_file(path,exe);
 auto command=L"\""+exe.wstring()+L"\" --child";STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder.c_str(),&startup,&process))return 1;
 DWORD result=1;if(WaitForSingleObject(process.hProcess,60000)==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&result);else{TerminateProcess(process.hProcess,2);WaitForSingleObject(process.hProcess,5000);}
 CloseHandle(process.hThread);CloseHandle(process.hProcess);
 std::ifstream log(folder/"behavior.log");std::cout<<log.rdbuf();log.close();
 // Verified owned temporary root; no game directory is involved.
 CHECK(fs::equivalent(folder.parent_path(),fs::temp_directory_path()));fs::remove_all(folder);
 std::cout<<"pinned package process: "<<result<<"\n";return static_cast<int>(result);
}
