// Offline rule-52 migration gate. Inputs and oracle outputs stay in the caller's workspace.
// Compiled into lcsave_test; LCSAVE_REBUILT_STANDALONE builds the same driver against the committed base library.
#include "internal.hpp"
#include "lcsave.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <sstream>

namespace fs = std::filesystem;
namespace {
std::vector<std::uint8_t> read(const fs::path& path) {
 std::ifstream in(path, std::ios::binary);
 return {std::istreambuf_iterator<char>(in), {}};
}
void write(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
 std::ofstream out(path, std::ios::binary);
 out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
 if (!out) lcsave::detail::fail("cannot write corpus output");
}
}

int run_rebuilt_corpus(const fs::path& cases, const fs::path& build, const fs::path& out, bool enabled) {
 using namespace lcsave;
#ifndef LCSAVE_REBUILT_BASELINE
 set_xml1_economy_verifier(enabled ? +[] { return true; } : nullptr);
#else
 if (enabled) return 2;
#endif
 fs::create_directories(out);
 Tables tables; std::string error;
 if (!load_tables(build.wstring(), tables, error)) { std::printf("FAIL tables: %s\n", error.c_str()); return 1; }
 TypedRegistry registry;
 if (!load_typed_registry(read(build / "Data/lcsave/typed-entity-ids.txt"), registry, error) ||
     !verify_registry(registry, (build / "Scripts").wstring(), error)) {
  std::printf("FAIL registry: %s\n", error.c_str()); return 1;
 }
 std::ifstream list(cases); if (!list) return 1;
 std::ofstream verdicts(out / "verdicts.tsv", std::ios::binary);
 unsigned total=0, loaded=0, refused=0, fixed=0, failures=0;
 std::string line;
 while (std::getline(list,line)) {
  if (line.empty()) continue;
  std::istringstream row(line); std::string name, path;
  if (!std::getline(row,name,'\t') || !std::getline(row,path)) return 1;
  ++total;
  try {
   const auto input=read(cases.parent_path()/path);
   const auto translated=new_to_old_stream(input,tables,w2_layout,&registry);
   if (!translated.ok) { ++refused; verdicts<<name<<"\trefused\t"<<translated.error<<'\n'; continue; }
   ++loaded;
   std::ofstream(out/(name+".ledger"), std::ios::binary) << translated.ledger;
   write(out/(name+".old"),translated.bytes);
   const auto file=detail::read_file(input.data(),input.size(),kind_campaign);
   NewFileContext context;
   context.description=file.data("META")["description"].as_text();
   context.saved_at=0;context.campaign_serial=file.header.campaign_serial;context.save_sequence=2;
   context.retained_file=input;context.require_capture_metadata=true;
   if (!loaded_zone_metadata(input,tables,context.zone_captures,error,w2_layout,&registry)) detail::fail("metadata: "+error);
   const auto saved=old_stream_to_new(translated.bytes,tables,context,w2_layout);
   if (!saved.ok) detail::fail("save: "+saved.error);
   write(out/(name+".lcsave"),saved.bytes);
   const auto again=new_to_old_stream(saved.bytes,tables,w2_layout,&registry);
   // The first native save can normalize unrelated legacy metadata. Require every instance immediately,
   // and then the entire second load/save/load cycle to be a fixed point.
   if (!again.ok) detail::fail("reload: "+again.error);
   const auto a=detail::read_slot(translated.bytes.data(),translated.bytes.size(),w2_layout);
   const auto b=detail::read_slot(again.bytes.data(),again.bytes.size(),w2_layout);
   if (detail::model_demand(a).instances!=detail::model_demand(b).instances) detail::fail("reload instance count");
   // Exact serialized instance bytes are checked by the Python driver on .old and the reload output.
   write(out/(name+".reload.old"),again.bytes);
   std::ofstream(out/(name+".reload.ledger"), std::ios::binary) << again.ledger;
   NewFileContext next=context;next.retained_file=saved.bytes;
   if (!loaded_zone_metadata(saved.bytes,tables,next.zone_captures,error,w2_layout,&registry)) detail::fail("reload metadata: "+error);
   const auto saved2=old_stream_to_new(again.bytes,tables,next,w2_layout);
   if (!saved2.ok) detail::fail("second save: "+saved2.error);
   const auto third=new_to_old_stream(saved2.bytes,tables,w2_layout,&registry);
   if (!third.ok || third.bytes!=again.bytes) detail::fail("native load-save-load is not a fixed point");
   ++fixed;verdicts<<name<<"\tloaded\tfixed\n";
  } catch (const detail::Error& e) { ++failures;verdicts<<name<<"\tfailed\t"<<e.message<<'\n';std::printf("FAIL %s: %s\n",name.c_str(),e.message.c_str()); }
 }
#ifndef LCSAVE_REBUILT_BASELINE
 set_xml1_economy_verifier(nullptr);
#endif
 std::printf("rebuilt corpus %s: %u total, %u loaded, %u refused, %u fixed points, %u failures\n",enabled?"ON":"OFF",total,loaded,refused,fixed,failures);
 return failures?1:0;
}
#ifdef LCSAVE_REBUILT_STANDALONE
int main(int argc,char** argv) { return argc==5 ? run_rebuilt_corpus(argv[1],argv[2],argv[3],std::string(argv[4])=="1") : 2; }
#endif
