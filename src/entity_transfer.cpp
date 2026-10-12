#include "entity_transfer.hpp"
#include "entity_capacity_layout.hpp"

#include <cstring>

namespace entity_transfer
{
	using namespace entity_capacity_rules;
	using entity_capacity_layout::native;

	namespace
	{
		const std::uint32_t native_615810 = 0x615810, native_55d7e0 = 0x55d7e0, native_612be0 = 0x612be0, native_610f30 = 0x610f30;
		using free_buffer_t = void(__cdecl*)(void*);
		using pool_alloc_t = void*(__cdecl*)(std::uint32_t, std::uint32_t);
		using difficulty_getter_t = void*(__cdecl*)();
		using difficulty_t = void(__fastcall*)(void*, void*, std::uint32_t);
	}

	// The retail acknowledgement 0x60872c..0x60875a on a 0x14-byte local header: the header is zeroed by 0x615810 with
	// ECX = the header (a thiscall that clears bytes 0..0x11 except 0x10); ECX then becomes the event's sender (+0x10);
	// two of the three arguments of 0x610f30 (flags 0, then the header) are pushed first; opcode 0x4a goes to +0x10 and
	// 1 to +0xe; 0x55d7e0 returns the sender's endpoint (+4, no stack arguments) and that endpoint is pushed as the
	// send's FIRST argument; 0x612be0 is the cdecl, argument-free getter of the session static 0xa53058 (it leaves the
	// pushed endpoint alone); 0x610f30 is the session's thiscall send(endpoint, header, flags) and cleans all three
	// (ret 0xc). The sequence below is byte-for-byte that order, so the stack balances exactly as in retail.
	__declspec(naked) void send_ack(std::uint8_t* /*event*/)
	{
		__asm
		{
			push ebp
			mov ebp, esp
			sub esp, 0x14
			push esi
			push edi
			lea ecx, [ebp - 0x14]
			call dword ptr [native_615810]
			mov eax, dword ptr [ebp + 8]
			mov ecx, dword ptr [eax + 0x10]
			push 0
			lea edx, [ebp - 0x14]
			push edx
			mov byte ptr [ebp - 0x14 + 0x10], 0x4a
			mov byte ptr [ebp - 0x14 + 0x0e], 1
			call dword ptr [native_55d7e0]
			push eax
			call dword ptr [native_612be0]
			mov ecx, eax
			call dword ptr [native_610f30]
			pop edi
			pop esi
			mov esp, ebp
			pop ebp
			ret
		}
	}

	std::uint32_t receive(std::uint8_t* event, const transfer_profile& profile, state& s, const observer& o)
	{
		auto* context = *reinterpret_cast<std::uint8_t**>(event + 0xc);
		const auto opcode = event[0];
		if (opcode == 'G')
		{
			s.chunks = 0;
			if (const auto callback = *reinterpret_cast<void(__cdecl**)(int, std::uint32_t, int)>(context + 0x2298))
				callback(7, *reinterpret_cast<std::uint32_t*>(context + 0x229c), 0);
			if (auto* old = *reinterpret_cast<void**>(context + 0x2250))
			{
				native<free_buffer_t>(0x560700)(old);
				*reinterpret_cast<void**>(context + 0x2250) = nullptr;
			}
			if (context[0x2294]) context[0x2294] = 0;
			auto* wrapper = static_cast<std::uint8_t*>(native<pool_alloc_t>(0x5606e0)(profile.wrapper, 0x2d));
			if (wrapper)
			{
				wrapper[profile.description] = 0;
				wrapper[profile.flag] = 0;
				*reinterpret_cast<std::uint8_t**>(wrapper + profile.payload) = wrapper;
			}
			else if (o.no_wrapper) o.no_wrapper(profile.wrapper);
			*reinterpret_cast<std::uint8_t**>(context + 0x2250) = wrapper;
			const auto* body = *reinterpret_cast<const std::uint32_t**>(event + 4);
			auto* game = native<difficulty_getter_t>(0x46dce0)();
			auto** vt = *reinterpret_cast<void***>(game);
			reinterpret_cast<difficulty_t>(vt[0x26c / 4])(game, nullptr, body[0]);
			context[0x2294] = static_cast<std::uint8_t>(body[1]);
			return 0;
		}
		if (opcode == 'H')
		{
			const auto length = *reinterpret_cast<const std::uint16_t*>(event + 8);
			auto* wrapper = *reinterpret_cast<std::uint8_t**>(context + 0x2250);
			const auto verdict = transfer_chunk(s.chunks, length, profile.payload, wrapper != nullptr);
			if (verdict != transfer_verdict::accept && verdict != transfer_verdict::complete)
			{
				if (o.refused) o.refused(verdict, s.chunks, length, profile.payload);
				return 0;
			}
			const auto* body = *reinterpret_cast<const std::uint8_t**>(event + 4);
			std::memcpy(wrapper + s.chunks * transfer_chunk_size, body, length);
			++s.chunks;
			if (verdict == transfer_verdict::complete) send_ack(event);
		}
		return 0;
	}
}
