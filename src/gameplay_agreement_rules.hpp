#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace gameplay_agreement_rules
{
	constexpr std::uint32_t wire_base = 0x45464810; // EFH tag, revision 1, bit 0 = installed rule
	constexpr std::uint32_t wire(bool installed) { return wire_base | (installed ? 1u : 0u); }
	constexpr bool supported(std::uint32_t peer) { return peer == wire(false) || peer == wire(true); }
	constexpr bool agrees(bool installed, std::uint32_t peer) { return supported(peer) && peer == wire(installed); }
	constexpr std::string_view refusal(bool installed, std::uint32_t peer)
	{
		return !supported(peer) ? "peer has no supported ExplosiveFirstHit agreement" :
			   !agrees(installed,peer) ? "effective ExplosiveFirstHit rules differ" : "";
	}
	constexpr std::size_t request_word = 0x64, reply_word = 0x20, info_word = 0x20;
	constexpr std::size_t ack_word = 8, ack_size = 12;
	constexpr std::uint32_t ack_writer = 0x609a5d, native_send = 0x610f30, ack_registration = 0x609dec, native_ack = 0x607810;
	constexpr std::size_t request_size = 0x74, reply_size = 0x50, info_size = 0x30;
	constexpr std::uint32_t request_writer = 0x607cf1, request_next = 0x607cff;
	constexpr std::uint32_t reply_writer = 0x6083b9, reply_next = 0x6083c1;
	constexpr std::uint32_t request_registration = 0x6096a3, reply_registration = 0x609ee2;
	constexpr std::uint32_t info_writer = 0x60778e, info_next = 0x607793, info_registration = 0x609cc3, native_info = 0x609970;
	constexpr std::uint32_t native_request = 0x6090d0, native_reply = 0x6079b0;
	inline constexpr std::array<limits_rules::guard, 13> guards{{
		{ack_writer, "e8ce740000", "follow-up native send call"},
		{native_send, "568bf18b8e1c040000", "native send thiscall entry"},
		{ack_registration, "68107860006a1b", "follow-up receiver registration, opcode 0x1b"},
		{native_ack, "83ec2453558b6c2430", "native follow-up callback cdecl entry"},
		{request_writer, "8984248000000089842484000000", "request reserved compatibility words"},
		{reply_writer, "896c2454896c2458", "reply reserved compatibility words"},
		{request_registration, "68d09060006a18", "request handler registration, opcode 0x18"},
		{reply_registration, "68b07960006a1f", "reply handler registration, opcode 0x1f"},
		{info_writer, "05e8030000", "host-info descriptor address and reserved session word"},
		{info_registration, "68709960006a1a", "host-info receiver registration, opcode 0x1a"},
		{native_info, "83ec1c53578d4c2410", "native host-info callback cdecl entry"},
		{native_request, "558bec6aff68d0926700", "native request callback cdecl entry"},
		{native_reply, "83ec14a1f8386f00538b5c241c", "native reply callback cdecl entry"},
	}};
	inline const limits_rules::guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		return nullptr;
	}
}
