#include "gameplay_agreement.hpp"
#include "gameplay_agreement_rules.hpp"
#include "log.hpp"
#include <cstring>

namespace gameplay_agreement
{
	namespace
	{
		using namespace gameplay_agreement_rules;
		std::uint32_t advertised = wire(false);
		bool local_rule = false;
		const std::uint32_t request_resume = request_next, reply_resume = reply_next, info_resume = info_next;
		using callback = int(__cdecl*)(void*);
		bool read_word(void* event, std::size_t offset, std::size_t size, std::uint32_t& value)
		{
			// Native callbacks receive their decoded fixed-size body at event+4.
			// The parser supplies the decoded body length as a ushort at event+8.
			__try {
				const auto* body = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(event)+4);
				if (!body || *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(event)+8) != size) return false;
				std::memcpy(&value,body+offset,4);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		int receive(void* event, std::size_t offset, std::size_t size, std::uint32_t native, const char* role)
		{
			std::uint32_t peer = 0;
			const bool readable = read_word(event,offset,size,peer);
			if (!readable || !agrees(local_rule,peer))
			{
				const auto why = readable ? refusal(local_rule,peer) : std::string_view("invalid native join body");
				logger::write("online: %s refused: %.*s (local installed rule=%u, peer agreement=0x%08lX); peers must agree on the successfully installed rule",
							  role,static_cast<int>(why.size()),why.data(),local_rule ? 1u : 0u,static_cast<unsigned long>(peer));
				return 0; // native callback convention; admission/state mutation is never called
			}
			return reinterpret_cast<callback>(static_cast<std::uintptr_t>(native))(event);
		}
		__declspec(naked) void request_bridge()
		{
			__asm { push eax }
			__asm { mov eax, dword ptr [advertised] }
			__asm { mov [esp + 0x84], eax }
			__asm { pop eax }
			__asm { mov [esp + 0x84], eax }
			__asm { jmp dword ptr [request_resume] }
		}
		__declspec(naked) void reply_bridge()
		{
			__asm { push eax }
			__asm { mov eax, dword ptr [advertised] }
			__asm { mov [esp + 0x58], eax }
			__asm { pop eax }
			__asm { mov [esp + 0x58], ebp }
			__asm { jmp dword ptr [reply_resume] }
		}
		__declspec(naked) void info_bridge()
		{
			__asm { push ecx }
			__asm { mov ecx, dword ptr [advertised] }
			__asm { mov [eax + 0x408], ecx }
			__asm { pop ecx }
			__asm { add eax, 0x3e8 }
			__asm { jmp dword ptr [info_resume] }
		}
		void __fastcall send_ack(void* session, void*, void* transport, const std::uint8_t* header, int flags)
		{
			// The native send synchronously copies the header/body into its queue.
			// Keep the original eight bytes; append the full rule tag in owned locals.
			std::array<std::uint8_t,0x14> copy{};
			std::array<std::uint8_t,ack_size> body{};
			std::memcpy(copy.data(),header,copy.size());
			const auto* original = *reinterpret_cast<const std::uint8_t* const*>(header);
			std::memcpy(body.data(),original,8);
			std::memcpy(body.data()+ack_word,&advertised,4);
			const auto* pointer = body.data();
			std::memcpy(copy.data(),&pointer,4);
			const std::uint16_t length = ack_size;
			std::memcpy(copy.data()+8,&length,2);
			using send = void(__thiscall*)(void*,void*,const std::uint8_t*,int);
			reinterpret_cast<send>(static_cast<std::uintptr_t>(native_send))(session,transport,copy.data(),flags);
		}
		const limits_rules::guard* mismatch()
		{
			__try { return first_mismatch(reinterpret_cast<const std::uint8_t*>(limits_rules::image_base)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &guards[0]; }
		}
	}
	int receive_request(void* event) { return receive(event,request_word,request_size,native_request,"host join request"); }
	int receive_reply(void* event) { return receive(event,reply_word,reply_size,native_reply,"client join reply"); }

	int receive_info(void* event) { return receive(event,info_word,info_size,native_info,"client host-info reply"); }

	int receive_ack(void* event) { return receive(event,ack_word,ack_size,native_ack,"host follow-up reply"); }

	bool install(const HMODULE game, const bool installed_rule)
	{
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || mismatch())
		{
			logger::write("online: gameplay agreement does not match retail; host/join must stay blocked");
			return false;
		}
		const std::array<std::uint32_t,8> sites{request_writer,reply_writer,info_writer,ack_writer,request_registration+1,reply_registration+1,info_registration+1,ack_registration+1};
		const std::array<std::size_t,8> sizes{14,8,5,5,4,4,4,4};
		std::array<DWORD,8> protections{};
		for (std::size_t i=0;i<sites.size();++i)
		{
			if (!VirtualProtect(reinterpret_cast<void*>(sites[i]),sizes[i],PAGE_EXECUTE_READWRITE,&protections[i]))
			{
				for (std::size_t j=i;j-- > 0;) { DWORD unused; VirtualProtect(reinterpret_cast<void*>(sites[j]),sizes[j],protections[j],&unused); }
				logger::write("online: gameplay agreement cannot unprotect 0x%08lX; host/join must stay blocked",sites[i]);
				return false;
			}
		}
		local_rule = installed_rule;
		advertised = wire(installed_rule);
		const auto jump = [](std::uint32_t site, const void* target, std::size_t length) {
			auto* code = reinterpret_cast<std::uint8_t*>(site);
			std::memset(code,0x90,length);code[0]=0xe9;
			const auto rel=static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target)-site-5);
			std::memcpy(code+1,&rel,4);
		};
		jump(request_writer,&request_bridge,14);jump(reply_writer,&reply_bridge,8);jump(info_writer,&info_bridge,5);jump(ack_writer,&send_ack,5);
		*reinterpret_cast<std::uint8_t*>(ack_writer)=0xe8; // replace CALL, not JMP; fastcall adapter cleans the native three stack arguments
		const auto ack_target = reinterpret_cast<std::uintptr_t>(&receive_ack);
		const auto request_target = reinterpret_cast<std::uintptr_t>(&receive_request);
		const auto info_target = reinterpret_cast<std::uintptr_t>(&receive_info);
		const auto reply_target = reinterpret_cast<std::uintptr_t>(&receive_reply);
		std::memcpy(reinterpret_cast<void*>(request_registration+1),&request_target,4);
		std::memcpy(reinterpret_cast<void*>(reply_registration+1),&reply_target,4);
		std::memcpy(reinterpret_cast<void*>(info_registration+1),&info_target,4);
		std::memcpy(reinterpret_cast<void*>(ack_registration+1),&ack_target,4);
		bool verified = true;
		for (std::size_t i=0;i<sites.size();++i)
		{
			verified = FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(sites[i]),sizes[i]) && verified;
			DWORD unused; VirtualProtect(reinterpret_cast<void*>(sites[i]),sizes[i],protections[i],&unused);
		}
		std::uint32_t request_readback = 0, reply_readback = 0, info_readback = 0, ack_readback = 0;
		std::memcpy(&request_readback,reinterpret_cast<const void*>(request_registration+1),4);
		std::memcpy(&reply_readback,reinterpret_cast<const void*>(reply_registration+1),4);
		std::memcpy(&info_readback,reinterpret_cast<const void*>(info_registration+1),4);
		std::memcpy(&ack_readback,reinterpret_cast<const void*>(ack_registration+1),4);
		const auto jump_verified = [](std::uint32_t site, const void* target, std::size_t size, std::uint8_t opcode=0xe9) {
			const auto* code = reinterpret_cast<const std::uint8_t*>(site);
			std::int32_t delta = 0; std::memcpy(&delta,code+1,4);
			if (code[0]!=opcode || site+5+delta!=reinterpret_cast<std::uintptr_t>(target)) return false;
			for (std::size_t i=5;i<size;++i) if (code[i]!=0x90) return false;
			return true;
		};
		verified = verified && request_readback == request_target && reply_readback == reply_target && info_readback == info_target && ack_readback == ack_target &&
				   jump_verified(request_writer,&request_bridge,14) && jump_verified(reply_writer,&reply_bridge,8) && jump_verified(info_writer,&info_bridge,5) && jump_verified(ack_writer,&send_ack,5,0xe8);
		logger::write("online: ExplosiveFirstHit agreement %s, installed rule=%u; release identity unchanged",verified ? "installed" : "failed",installed_rule ? 1u : 0u);
		return verified;
	}
}
