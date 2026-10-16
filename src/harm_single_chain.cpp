#include "harm_single_chain.hpp"
#include "harm_single_chain_bridge.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cstring>

namespace harm_single_chain
{
	namespace
	{
		std::atomic<int> state{-1};
		const harm_single_chain_rules::guard* mismatch()
		{
			__try { return harm_single_chain_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(0x400000)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &harm_single_chain_rules::guards[0]; }
		}
	}
	void install(HMODULE game)
	{
		using namespace harm_single_chain_rules;
		const auto setting = ini::text(L"Game",L"HarmSingleChain").value_or("");
		if (setting.empty() || setting == "off") return;
		state = 0;
		// A requested policy (rule 23, separate from BoxHarmOrigin rule 21) is registered
		// by exports.cpp with its verified installed state: a refused or invalid request
		// stays unverified and blocks admission.
		if (setting != "single")
		{
			logger::write("harm single chain: invalid [Game] HarmSingleChain - native chain behavior retained");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || mismatch())
		{
			logger::write("harm single chain: retail guards refused - native chain behavior retained");
			return;
		}
		const auto jump=[](std::uint32_t site, const auto& bytes, const void* bridge) {
			xml1_hook_transaction::patch patch{site,{bytes.begin(),bytes.end()},std::vector<unsigned char>(bytes.size(),0x90)};
			patch.after[0]=0xe9;
			const auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bridge))-(site+5);
			std::memcpy(patch.after.data()+1,&relative,4);
			return patch;
		};
		const std::vector<xml1_hook_transaction::patch> patches{
			jump(tail_site,tail_original,&harm_single_chain_bridge::tail),
			jump(request_site,request_original,&harm_single_chain_bridge::request)};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory,patches,&safe))
		{
			state = 1;
			logger::write("harm single chain: single-chain policy installed; authored repeat delay and native area query retained");
		}
		else if (safe)
			logger::write("harm single chain: transaction refused - native chain behavior retained");
		else
		{
			game_version::refuse_startup();
			logger::write("harm single chain: rollback/protection verification failed; startup refused");
		}
	}
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
	bool verified() { return !requested() || installed(); }
}
