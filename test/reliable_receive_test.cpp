// Execute the native datagram parser and reliable receiver against synthetic message pools.
// The local executable supplies the instructions; no executable bytes or game content are embedded.
#include "entity_transfer.hpp"
#include "patch_transaction.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" std::uint8_t fixture_space[0x700000];
namespace
{
int failures = 0, acknowledgements = 0, header_calls = 0, body_calls = 0, queued = 0;
std::vector<std::uint8_t> binary, manager(0x13800), peer(0x30c0), cache(0x149ac), node_cache(0x3dac);
std::array<std::uint8_t, 20> node{}, header{};
std::array<std::uint8_t, 325> body{};
std::uint16_t queued_sequence = 0;
void check(bool ok, const char* text)
{
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", text);
	failures += !ok;
}
void* at(std::uint32_t va)
{
	return reinterpret_cast<void*>(va);
}
template <class T> T& field(std::vector<std::uint8_t>& v, std::size_t o)
{
	return *reinterpret_cast<T*>(v.data() + o);
}
void jump(std::uint32_t va, const void* target)
{
	auto* p = static_cast<std::uint8_t*>(at(va));
	p[0] = 0xe9;
	const auto rel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (va + 5);
	std::memcpy(p + 1, &rel, 4);
}
std::size_t offset(std::uint32_t va)
{
	auto u16 = [](std::size_t o) {
		if (o > binary.size() || binary.size() - o < 2)
			std::exit(2);
		std::uint16_t n;
		std::memcpy(&n, binary.data() + o, 2);
		return n;
	};
	auto u32 = [](std::size_t o) {
		if (o > binary.size() || binary.size() - o < 4)
			std::exit(2);
		std::uint32_t n;
		std::memcpy(&n, binary.data() + o, 4);
		return n;
	};
	const auto pe = u32(0x3c);
	const auto sections = std::size_t(pe) + 24 + u16(std::size_t(pe) + 20);
	if (u16(0) != 0x5a4d || u32(pe) != 0x4550 || sections + std::size_t(u16(std::size_t(pe) + 6)) * 40 > binary.size())
		std::exit(2);
	for (unsigned i = 0; i < u16(std::size_t(pe) + 6); ++i)
	{
		const auto row = sections + i * 40, rva = std::size_t(u32(row + 12));
		if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16))
			return u32(row + 20) + va - 0x400000 - rva;
	}
	return binary.size();
}
void copy(std::uint32_t va, std::size_t n)
{
	const auto o = offset(va);
	if (o > binary.size() || n > binary.size() - o)
		std::exit(2);
	std::memcpy(at(va), binary.data() + o, n);
}
void* __cdecl nodes_get() { return node_cache.data(); }
void* __cdecl cache_get()
{
	return cache.data();
}
void* __fastcall header_alloc(void*, void*)
{
	++header_calls;
	auto& used = field<std::uint32_t>(cache, 0x48d0);
	if (used >= 768)
		return nullptr;
	++used;
	header.fill(0);
	return header.data();
}
void* __fastcall body_alloc(void*, void*, unsigned n)
{
	++body_calls;
	if (n > 325)
		return nullptr;
	if (n > 60)
	{
		auto& used = field<std::uint32_t>(cache, 0x149a8);
		if (used >= 50)
			return nullptr;
		++used;
	}
	body.fill(0);
	return body.data();
}
bool __fastcall enqueue(void*, void*, void*, unsigned sequence)
{
	++queued;
	queued_sequence = static_cast<std::uint16_t>(sequence);
	return true;
}
bool __fastcall connected(void*, void*)
{
	return true;
}
void __fastcall cookie(unsigned)
{
}
int __fastcall dispatch(void*, void*, std::uint8_t* event)
{
	if (event[0] == 0)
		return reinterpret_cast<int(__cdecl*)(void*)>(at(0x6100b0))(event);
	if (event[0] == 0x0a)
		++acknowledgements;
	return 0;
}
void packet(unsigned expected, unsigned sequence, unsigned large, unsigned headers, unsigned bytes = 200,
			unsigned messages = 1)
{
	std::fill(manager.begin(), manager.end(), std::uint8_t{});
	std::fill(peer.begin(), peer.end(), std::uint8_t{});
	std::fill(cache.begin(), cache.end(), std::uint8_t{});
	node.fill(0);
	acknowledgements = header_calls = body_calls = queued = 0;
	field<std::uint32_t>(cache, 0x149a8) = large;
	field<std::uint32_t>(cache, 0x48d0) = headers;
	field<std::uint16_t>(peer, 0x308c) = static_cast<std::uint16_t>(expected);
	field<void*>(manager, 0x124b8) = node.data();
	field<unsigned>(manager, 0x124c4) = 1;
	*reinterpret_cast<void**>(node.data() + 0x10) = peer.data();
	auto* p = peer.data() + 0x52e;
	for (unsigned message = 0; message < messages; ++message)
	{
		*p++ = 0xc8; // reliable H, variable length, native sequence word, synthetic body
		*reinterpret_cast<std::uint16_t*>(p) = static_cast<std::uint16_t>(bytes + 2);
		p += 2;
		*reinterpret_cast<std::uint16_t*>(p) = static_cast<std::uint16_t>(sequence + message);
		p += 2;
		for (unsigned i = 0; i < bytes; ++i)
			*p++ = static_cast<std::uint8_t>(i * 13 + 7 + message);
	}
	*p++ = 0x0a;
	*reinterpret_cast<std::uint16_t*>(p) = 17;
	p += 2;
	field<void*>(peer, 0x1e98) = p;
	reinterpret_cast<void(__thiscall*)(void*)>(at(0x60dca0))(manager.data());
}

// Persistent fixtures below execute retail allocation, sorted insertion, ordered dispatch and freeing.
// Only the application handler, endpoint lookup and UI/session boundary are synthetic.
std::vector<std::uint8_t> second_peer(0x30c0), failure_context(0x22c4);
std::array<std::uint8_t, 20> second_node{}, manager_node{};
std::array<std::uint8_t, 16> manager_list{};
unsigned delivered[2]{}, bad_payload = 0, refusal_lines = 0, session_failures = 0, unsafe_cleanup = 0;
bool replying = false, inside_handler = false, reply_ok = false;
void* __fastcall node_destroy(void* p, void*, unsigned) { return p; }
void __fastcall statistics(void*, void*, unsigned, unsigned) {}
void* __fastcall endpoint(void*, void*, void* p) { return p; }
void* __cdecl campaign_context() { return failure_context.data(); }
void __cdecl failure_line(const char*, entity_capacity_rules::message_pools) { ++refusal_lines; }
void __cdecl application_failure(int code, unsigned, int) { session_failures += code == 9; }
void release_queue(std::vector<std::uint8_t>& who, unsigned offset)
{
 while (auto* n = field<std::uint8_t*>(who, offset))
 {
  auto* h = *reinterpret_cast<std::uint8_t**>(n + 0x10);
  reinterpret_cast<void(__thiscall*)(void*,void*,unsigned)>(at(0x60e980))(
   cache.data(), *reinterpret_cast<void**>(h), *reinterpret_cast<std::uint16_t*>(h + 8));
  reinterpret_cast<void(__thiscall*)(void*,void*)>(at(0x60e8f0))(cache.data(),h);
  reinterpret_cast<bool(__thiscall*)(void*,void*)>(at(0x611e90))(who.data()+offset,n);
 }
}
void __fastcall cleanup(void*, void*)
{
 unsafe_cleanup += inside_handler;
 for (auto* p : {&peer, &second_peer}) { release_queue(*p,0x3090); release_queue(*p,0x30a0); }
}
bool send_message(std::vector<std::uint8_t>& who, unsigned bytes, bool reliable=false, bool implicit=false)
{
 std::vector<std::uint8_t> payload(bytes, 0x35);
 std::array<std::uint8_t,20> h{};
 *reinterpret_cast<void**>(h.data())=payload.data();
 *reinterpret_cast<std::uint16_t*>(h.data()+8)=static_cast<std::uint16_t>(implicit?0:bytes);
 h[0x10]=0x1b;h[0xe]=reliable;
 if(implicit)*reinterpret_cast<std::uint16_t*>(at(0xa3e490+0x1b*2))=static_cast<std::uint16_t>(bytes);
 return reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(at(0x60f620))(manager.data(),who.data(),h.data());
}
int __fastcall persistent_dispatch(void*, void*, std::uint8_t* event)
{
 if(event[0]==0)return reinterpret_cast<int(__cdecl*)(void*)>(at(0x6100b0))(event);
 if(event[0]==0xa){++acknowledgements;return 0;}
 if(event[0]=='H' || event[0]==0x1a)
 {
  const auto index=*reinterpret_cast<void**>(event+0x10)==peer.data()?0:1;
  ++delivered[index];
  auto* bytes=*reinterpret_cast<std::uint8_t**>(event+4);
  for(unsigned i=0;i<*reinterpret_cast<std::uint16_t*>(event+8);++i)
   bad_payload += bytes[i]!=static_cast<std::uint8_t>(i*13+7);
  if(replying && event[0]==0x1a)
  {
   inside_handler=true;
   reply_ok=send_message(index?second_peer:peer,116);
   inside_handler=false;
  }
 }
 return 0;
}
void init_pools()
{
 std::fill(cache.begin(),cache.end(),std::uint8_t{});
 std::fill(node_cache.begin(),node_cache.end(),std::uint8_t{});
 for(unsigned i=0;i<768;++i)
 {
  field<unsigned>(cache,0x3c60+4*i)=i;
  field<unsigned>(cache,0x48d4+0xb404+4*i)=i;
 }
 for(unsigned i=0;i<50;++i)field<unsigned>(cache,0x1094c+0x3f7c+4*i)=i;
 for(unsigned i=0;i<650;++i)field<unsigned>(node_cache,0x331c+4*i)=i;
 field<unsigned>(cache,0x486c)=768;
 field<unsigned>(cache,0x48d4+0xc010)=768;
 field<unsigned>(cache,0x1094c+0x4050)=50;
 field<unsigned>(node_cache,0x3d50)=650;
}
void persistent_reset()
{
 // Retire any previous pressure tracking without touching synthetic application state.
 entity_transfer::transport_maintenance(nullptr);
 init_pools();
 for(auto* p:{&peer,&second_peer}) {std::fill(p->begin(),p->end(),std::uint8_t{});(*p)[0x19]=1;}
 std::fill(manager.begin(),manager.end(),std::uint8_t{});
 node.fill(0);second_node.fill(0);manager_list.fill(0);manager_node.fill(0);
 *reinterpret_cast<void**>(node.data()+0x10)=peer.data();
 *reinterpret_cast<void**>(node.data()+8)=second_node.data();
 *reinterpret_cast<void**>(second_node.data()+0x10)=second_peer.data();
 field<void*>(manager,0x124b8)=node.data();field<unsigned>(manager,0x124c4)=2;
 *reinterpret_cast<void**>(manager_list.data())=manager_node.data();
 *reinterpret_cast<unsigned*>(manager_list.data()+0xc)=1;
 *reinterpret_cast<void**>(manager_node.data()+0x10)=manager.data();
 field<void*>(failure_context,0x2298)=reinterpret_cast<void*>(&application_failure);
 acknowledgements=0;delivered[0]=delivered[1]=bad_payload=refusal_lines=session_failures=unsafe_cleanup=0;
 replying=inside_handler=reply_ok=false;
}
void persistent_packet(std::vector<std::uint8_t>& who,unsigned sequence,unsigned bytes=8,unsigned opcode='H')
{
 auto* p=who.data()+0x52e;
 *p++=static_cast<std::uint8_t>(opcode|0x80);
 *reinterpret_cast<std::uint16_t*>(p)=static_cast<std::uint16_t>(bytes+2);p+=2;
 *reinterpret_cast<std::uint16_t*>(p)=static_cast<std::uint16_t>(sequence);p+=2;
 for(unsigned i=0;i<bytes;++i)*p++=static_cast<std::uint8_t>(i*13+7);
 *p++=0xa;*reinterpret_cast<std::uint16_t*>(p)=17;p+=2;
 field<void*>(who,0x1e98)=p;
 reinterpret_cast<void(__thiscall*)(void*)>(at(0x60dca0))(manager.data());
}
void drain(std::vector<std::uint8_t>& who)
{
 reinterpret_cast<void(__thiscall*)(void*,void*)>(at(0x60ff00))(manager.data(),who.data());
}
bool null_copy_negative()
{
 __try { drain(peer); }
 __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
 { inside_handler=false;return true; }
 return false;
}
void persistent_tests()
{
 for(const auto r:std::array<std::pair<unsigned,unsigned>,13>{{
  {0x605740,0x62},{0x608a50,0x8e},{0x60d7f0,0x49},{0x60e090,0x400},
  {0x60e740,0x8b0},{0x60ff00,0x1aa},{0x611e90,0x88},{0x615810,0xc2},
  {0x60f620,0x313},{0x609ad0,0xa7},{0x60a880,0x29},{0x60f0a0,0x76},{0x6106d0,0x2a}}})copy(r.first,r.second);
 jump(0x60db20,&endpoint);jump(0x60d710,&statistics);jump(0x60dbe0,&persistent_dispatch);
 *reinterpret_cast<void**>(at(0x6a4778))=reinterpret_cast<void*>(&node_destroy);
 jump(0x6097b0,&campaign_context);jump(0x6097f0,&cleanup);
 jump(0x609b30,at(0x609b6f)); // presentation is outside this native transport fixture
 entity_transfer::observe_failure(&failure_line);
 copy(0x6100e8,5);
 persistent_reset();
 for(unsigned i=1;i<=325;++i){persistent_packet(peer,i);persistent_packet(second_peer,i);}
 const auto before=acknowledgements;
 for(unsigned i=0;i<3;++i){persistent_packet(peer,0);persistent_packet(second_peer,0);drain(peer);drain(second_peer);}
 check(field<unsigned>(node_cache,0x3da8)==650 && field<unsigned>(cache,0x48d0)==650 &&
       field<unsigned>(cache,0x149a8)==0 && delivered[0]+delivered[1]==0 && acknowledgements==before+6,
       "negative control: two persistent native receive queues fill 650 shared nodes; retransmitted gaps are dropped and ACKed repeatedly");
 persistent_reset();
 jump(0x6100e8,&entity_transfer::reliable_receive_room);
 for(unsigned i=1;i<=325;++i){persistent_packet(peer,i);persistent_packet(second_peer,i);}
 check(field<unsigned>(node_cache,0x3da8)==649 && acknowledgements==649,
       "two persistent peers leave the 650th native queue node available; deferred message is not ACKed");
 persistent_packet(peer,0);drain(peer);
 persistent_packet(second_peer,325);persistent_packet(second_peer,0);drain(second_peer);
 check(delivered[0]==326 && delivered[1]==326 && !bad_payload && field<unsigned>(node_cache,0x3da8)==0 &&
       field<unsigned>(cache,0x48d0)==0 && field<unsigned>(cache,0x10948)==0,
       "both gaps close, exact small payloads drain through retail insertion/dispatch/free, and all three pools return to zero");

 persistent_reset();
 for(unsigned i=1;i<=49;++i)persistent_packet(peer,i,116);
 persistent_packet(peer,0,116,0x1a);replying=true;
 check(null_copy_negative(),"negative control: 49 queued bodies plus host-info delivery cause the native 116-byte reply to copy through null");
 persistent_reset();
 jump(0x60f642,&entity_transfer::outgoing_room);
 for(unsigned i=1;i<=49;++i)persistent_packet(peer,i,116);
 persistent_packet(peer,0,116,0x1a);replying=true;drain(peer);
 check(!reply_ok && delivered[0]==50 && !bad_payload && refusal_lines==1 && session_failures==0 &&
       field<unsigned>(cache,0x149a8)==0 && field<unsigned>(node_cache,0x3da8)==0,
       "crowded host-info reply refuses before allocation; native drain still frees its live received body safely");
 send_message(second_peer,116);send_message(peer,116);
 check(refusal_lines==1,"further sends during pending failure are refused without another log line");
 jump(0x60a880,&entity_transfer::transport_maintenance);
 reinterpret_cast<void(__cdecl*)(void*)>(at(0x60a880))(manager_list.data());
 check(session_failures==1 && unsafe_cleanup==0,"maintenance dispatches native failure callback 9 and cleanup once, after application dispatch unwinds");

 persistent_reset();
 for(unsigned i=1;i<=47;++i)persistent_packet(peer,i,116);
 persistent_packet(peer,0,116,0x1a);replying=true;drain(peer);
 check(reply_ok && delivered[0]==48 && field<unsigned>(cache,0x149a8)==1 &&
       field<unsigned>(peer,0x30ac)==1 && !refusal_lines,"crowded but affordable handshake reply queues normally and survives inbound drain");
 release_queue(peer,0x30a0);
 check(field<unsigned>(cache,0x149a8)==0 && field<unsigned>(node_cache,0x3da8)==0,"shared outgoing body and node retire normally");

 persistent_reset();
 for(unsigned i=1;i<=49;++i)persistent_packet(peer,i,116);
 persistent_packet(peer,0,116);
 check(send_message(second_peer,8) && field<unsigned>(cache,0x149a8)==50 && !refusal_lines,
       "small control traffic still sends while a queued gap temporarily occupies the last large body");
 drain(peer);release_queue(second_peer,0x30a0);
 check(delivered[0]==50 && !field<unsigned>(cache,0x48d0) && !field<unsigned>(node_cache,0x3da8),
       "control send does not block native large-backlog drain or consume its bodies");
 for(const auto bytes : {301u,359u,360u,600u})
 {
  persistent_reset();
  const bool small_tail=bytes==301 || bytes==359;
  check(send_message(peer,bytes,true) && field<unsigned>(cache,0x10948)==(small_tail?2u:1u) &&
        field<unsigned>(cache,0x149a8)==(small_tail?1u:2u) && field<unsigned>(peer,0x30ac)==3,
        "native fragment descriptor/tag accounting agrees at small/large and exact-multiple boundaries");
  release_queue(peer,0x30a0);
  check(!field<unsigned>(cache,0x48d0) && !field<unsigned>(cache,0x149a8) &&
        !field<unsigned>(cache,0x10948) && !field<unsigned>(node_cache,0x3da8),
        "all fragmented body classes, headers and nodes retire back to zero");
 }
 persistent_reset();
 check(send_message(peer,14460,true) && field<unsigned>(cache,0x149a8)==49 &&
       field<unsigned>(cache,0x10948)==1 && field<unsigned>(peer,0x30ac)==50,
       "native fragmented send preflights the descriptor and every tagged chunk, preserving one large-body slot");
 check(!send_message(second_peer,116) && refusal_lines==1,"another peer cannot consume the receive reserve with an outgoing body");
 entity_transfer::transport_maintenance(manager_list.data());
 check(session_failures==1 && field<unsigned>(node_cache,0x3da8)==0 && field<unsigned>(cache,0x48d0)==0,
       "fragmented outgoing pressure ends cleanly and frees the existing outgoing fragments");
 persistent_reset();
 check(!send_message(peer,15000,true) && field<unsigned>(cache,0x149a8)==0 && field<unsigned>(cache,0x48d0)==0,
       "oversized fragmented send refuses atomically before even its descriptor is allocated");
 entity_transfer::transport_maintenance(manager_list.data());
 persistent_reset();
 for(unsigned i=0;i<768;++i)reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(at(0x60e910))(cache.data(),8);
 persistent_packet(peer,0,8);
 check(!field<unsigned>(cache,0x48d0) && !acknowledgements,"a full shared small-body pool defers without leaking a header or ACKing");
 check(send_message(second_peer,116) && !refusal_lines,"large traffic does not require unused small-body headroom");
 release_queue(second_peer,0x30a0);
 check(!send_message(peer,8) && refusal_lines==1,"outgoing small bodies also refuse safely when the small-body pool is full");
 entity_transfer::transport_maintenance(manager_list.data());
 persistent_reset();
 field<unsigned>(cache,0x149a8)=49;
 check(!send_message(peer,116) && refusal_lines==1,"pending failure is raised while the transport is live");
 manager_list.fill(0);entity_transfer::transport_maintenance(manager_list.data());
 const auto dispatched=session_failures;
 init_pools();
 check(!dispatched && send_message(peer,116) && refusal_lines==1,
       "a failure left pending after the transport ended is retired without native dispatch and does not refuse the next session");
 release_queue(peer,0x30a0);
 persistent_reset();
 field<unsigned>(cache,0x149a8)=49;
 check(!send_message(peer,61,false,true) && !field<unsigned>(cache,0x48d0),
       "implicit fixed body length is resolved before preflight, not mistaken for an empty send");
 entity_transfer::transport_maintenance(manager_list.data());
 persistent_reset();
 // A separate native list consumes the entire shared node pool, independent of either peer.
 std::array<std::uint8_t,16> foreign{};
 for(unsigned i=0;i<650;++i)reinterpret_cast<bool(__thiscall*)(void*,void*,unsigned)>(at(0x60e9e0))(foreign.data(),nullptr,i);
 persistent_packet(peer,0,8);
 check(field<unsigned>(node_cache,0x3da8)==650 && !field<unsigned>(cache,0x48d0) && !acknowledgements,
       "a genuinely full shared pool defers without entering native insertion or discarding acknowledged data");
 std::puts("Waiting 30 seconds for the production pressure watchdog (no packets or sends required)...");std::fflush(stdout);
 Sleep(static_cast<DWORD>(entity_capacity_rules::pressure_deadline::limit_ms+50));
 entity_transfer::transport_maintenance(manager_list.data());
 check(session_failures==1 && refusal_lines==1 && !unsafe_cleanup,
       "a persistent full shared pool fails once at maintenance after 30 seconds, even with no subsequent packets");
 while(auto* n=*reinterpret_cast<void**>(foreign.data()))reinterpret_cast<bool(__thiscall*)(void*,void*)>(at(0x611e90))(foreign.data(),n);
 check(!field<unsigned>(node_cache,0x3da8),"session failure leaves unrelated shared-pool users intact and they remain releasable");
 persistent_reset();
 entity_capacity_rules::pressure_deadline deadline;
 deadline.block(65535,100);deadline.block(65535,20000);
 check(!deadline.expired(65535,30099) && deadline.expired(65535,30100),"repeated pressure cannot renew an unchanged gap's deadline");
 check(!deadline.expired(0,30101) && !deadline.active,"sequence progress across wrap clears the pressure deadline");
}

} // namespace

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc != 2)
	{
		std::puts("Supply the locally owned retail executable.");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	binary.assign(std::istreambuf_iterator<char>(f), {});
	if (binary.size() < 4096)
		return 2;
	const auto start = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || start > 0x60d000 ||
		start + sizeof fixture_space < 0xa3e600)
		return 2;
	DWORD old = 0;
	if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old))
		return 2;
	for (const auto& r : entity_capacity_rules::extra_regions)
 {
  copy(r.lo, r.hi_exclusive-r.lo);
  check(patch_transaction::sha256(static_cast<const std::uint8_t*>(at(r.lo)),r.hi_exclusive-r.lo)==patch_transaction::sha256_of(r.sha256),r.name);
  patch_transaction::contribution dependency;dependency.owner="pool dependency mutation";
  dependency.regions.push_back({r.name,r.lo,r.hi_exclusive,patch_transaction::sha256_of(r.sha256)});
  auto* last=static_cast<std::uint8_t*>(at(r.hi_exclusive-1));*last^=1;
  patch_transaction::transaction changed;changed.add(dependency);
  check(changed.commit()==patch_transaction::outcome::refused_region,"dependency final byte mutation refuses (including complete RET operands)");
  *last^=1;
 }
 copy(0x6100b0, 0x6101af - 0x6100b0);
	copy(0x60dca0, 0x60de27 - 0x60dca0);
	copy(0x6157a0, 0x70);
	// The ACK request has the native two-byte fixed body; H carries its variable body length on the wire.
	*reinterpret_cast<std::uint16_t*>(at(0xa3e490 + 0x0a * 2)) = 2;
	for (const auto& r : entity_capacity_rules::extra_regions)
		if (r.lo == 0x6100b0 || r.lo == 0x60dca0)
			check(patch_transaction::sha256(static_cast<const std::uint8_t*>(at(r.lo)), r.hi_exclusive - r.lo) ==
					  patch_transaction::sha256_of(r.sha256),
				  "native receiver/parser matches the production region guard");
	if (failures)
		return 2;
	jump(0x60f240, &cache_get);
	jump(0x6067d0, &nodes_get);
	jump(0x60e740, &header_alloc);
	jump(0x60e910, &body_alloc);
	jump(0x60eac0, &enqueue);
	jump(0x60dbe0, &dispatch);
	jump(0x6106e0, &connected);
	jump(0x672161, &cookie);
	packet(100, 100, 50, 50);
	check(queued == 0 && acknowledgements == 1 && header_calls == 1 && body_calls == 1 &&
			  field<unsigned>(cache, 0x48d0) == 51,
		  "negative control: native full body pool drops a reliable message, leaks its header, then ACKs the datagram");
	packet(100, 101, 49, 49);
	check(queued == 1 && acknowledgements == 1 && field<unsigned>(cache, 0x149a8) == 50,
		  "negative control: native out-of-order message consumes the last body needed by the missing sequence");

	namespace pt = patch_transaction;
	pt::contribution patch;
	patch.owner = "synthetic reliable receive";
	patch.guards.push_back({0x6100e8, pt::bytes_of("53668b5e08"), "native reliable allocation entry"});
	patch.writes.push_back(pt::relocation(
		0x6100e8, "e9", true,
		static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&entity_transfer::reliable_receive_room)), 0,
		"receive room"));
	patch.guards.push_back({0x60f642,pt::bytes_of("8a4f0e84c9"),"native outgoing allocation entry"});
 patch.guards.push_back({0x60a880,pt::bytes_of("8b4c240485c9"),"native maintenance entry"});
 patch.writes.push_back(pt::relocation(0x60f642,"e9",true,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&entity_transfer::outgoing_room)),0,"outgoing room"));
 patch.writes.push_back(pt::relocation(0x60a880,"e9",true,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&entity_transfer::transport_maintenance)),1,"maintenance"));
 for(auto site:{0x60f642u,0x60a880u})
 {
  auto* p=static_cast<std::uint8_t*>(at(site));*p^=1;
  pt::transaction refused;refused.add(patch);
  check(refused.commit()==pt::outcome::refused_guard,"changed send/maintenance entry refuses the complete transaction");
  *p^=1;
 }
 for (const auto& r : entity_capacity_rules::extra_regions)
  if (r.lo == 0x6100b0)
   patch.regions.push_back({r.name, r.lo, r.hi_exclusive, pt::sha256_of(r.sha256)});
	{
		static_cast<std::uint8_t*>(at(0x6100e8))[0] ^= 1;
		pt::transaction refused;
		refused.add(patch);
		check(refused.commit() == pt::outcome::refused_guard && *static_cast<std::uint8_t*>(at(0x6100e8)) == 0x52,
			  "changed entry refuses without writing");
		static_cast<std::uint8_t*>(at(0x6100e8))[0] ^= 1;
		static_cast<std::uint8_t*>(at(0x6100ed))[0] ^= 1;
		pt::transaction changed;
		changed.add(patch);
		check(changed.commit() == pt::outcome::refused_region,
			  "changed receiver body refuses even when the entry signature matches");
		static_cast<std::uint8_t*>(at(0x6100ed))[0] ^= 1;
	}
	{
		pt::hooks fault;
		fault.after_apply = []() { static_cast<std::uint8_t*>(at(0x6100e9))[0] ^= 0xff; };
		pt::transaction t;
		t.add(patch);
		check(t.commit(&fault) == pt::outcome::rolled_back,
			  "forced readback failure restores all three native entries");
		check(*static_cast<std::uint8_t*>(at(0x60f642))==0x8a && *static_cast<std::uint8_t*>(at(0x60a880))==0x8b,"send and maintenance bridge writes rolled back together");
	}
	pt::transaction t;
	t.add(patch);
	check(t.commit() == pt::outcome::committed, "production receive bridge installs with verified readback");
	packet(100, 101, 49, 49);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0 && body_calls == 0 &&
			  field<unsigned>(cache, 0x149a8) == 49 && field<void*>(peer, 0x1e98) == peer.data() + 0x52e,
		  "out-of-order pressure: no allocation, no ACK, pending datagram batch cleared for retransmission");
	packet(100, 101, 48, 48, 200, 2);
	check(queued == 1 && queued_sequence == 101 && acknowledgements == 0 && header_calls == 1 && body_calls == 1 &&
			  field<unsigned>(cache, 0x149a8) == 49 && body[0] == 7 &&
			  body[199] == static_cast<std::uint8_t>(199 * 13 + 7),
		  "pressure midway through a datagram preserves the already queued payload and withholds the trailing ACK");
	packet(100, 100, 49, 49);
	bool exact = true;
	for (unsigned i = 0; i < 200; ++i)
		exact = exact && body[i] == static_cast<std::uint8_t>(i * 13 + 7);
	check(queued == 1 && queued_sequence == 100 && acknowledgements == 1 && exact &&
			  field<unsigned>(cache, 0x149a8) == 50,
		  "the missing in-order message uses the reserved body and preserves its exact bytes");
	packet(101, 101, 0, 0);
	check(queued == 1 && queued_sequence == 101 && acknowledgements == 1,
		  "after draining the gap, a retransmitted next message is admitted normally");
	packet(100, 101, 0, 767, 8);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0,
		  "small out-of-order messages leave one header for the missing sequence");
	packet(100, 100, 0, 767, 8);
	check(queued == 1 && acknowledgements == 1 && field<unsigned>(cache, 0x48d0) == 768,
		  "an in-order small message may use the reserved header");
	packet(100, 100, 50, 50);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0 && body_calls == 0,
		  "a completely full body pool defers before leaking a header or acknowledging data loss");
	packet(100, 100, 0, 768, 8);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0,
		  "a completely full header pool defers before acknowledging data loss");
	packet(100, 101, 50, 10, 60);
	check(queued == 1 && acknowledgements == 1 && field<unsigned>(cache, 0x149a8) == 50,
		  "a 60-byte message uses the small-body pool even when large bodies are full");
	packet(100, 101, 49, 10, 61);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0,
		  "a 61-byte future message preserves the last large-body slot");
	packet(100, 99, 50, 768);
	check(queued == 0 && acknowledgements == 1 && header_calls == 0,
		  "already delivered duplicates retain native ACK behavior without allocation");
	packet(0xffff, 0xffff, 49, 767);
	check(queued == 1 && acknowledgements == 1, "sequence wrap boundary still admits the expected message");
	packet(0, 1, 49, 49);
	check(queued == 0 && acknowledgements == 0, "out-of-order reserve also applies after sequence wrap");
	persistent_tests();
	std::printf("Reliable receive: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
