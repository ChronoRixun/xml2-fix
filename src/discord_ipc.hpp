#pragma once

// A connection to the running Discord client's local RPC pipe (\\.\pipe\discord-ipc-0..9), as its
// SDKs make one, without them: frames of [opcode][length][JSON] (discord_rules.hpp). Every read and
// write has a timeout, so a Discord that stops answering never holds its caller for long. Used by the
// presence's own thread in the game (discord_presence.cpp) and by xml2_test --discord-live.

#include "discord_rules.hpp"

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace discord_ipc
{
	class connection
	{
	public:
		connection() = default;
		~connection();
		connection(const connection&) = delete;
		connection& operator=(const connection&) = delete;

		// Opens the first of Discord's pipes that is there and introduces itself as the application
		// `client_id`, waiting up to `timeout_ms` for Discord's READY. On success `pipe` names the pipe;
		// otherwise `error` says why (no Discord, or Discord's refusal, e.g. an unknown application).
		bool open(std::string_view client_id, std::string& pipe, std::string& error, DWORD timeout_ms = 5000);

		bool is_open() const;

		// Sends one frame, waiting at most `timeout_ms` for the pipe to take it.
		bool send(std::uint32_t op, std::string_view json, DWORD timeout_ms = 2000);

		// The frames Discord has sent since the last call, without waiting. False once the pipe is gone.
		bool receive(std::vector<discord_rules::message>& out);

		// The next frame, waiting up to `timeout_ms`; nullopt on a timeout or when the pipe is gone
		// (is_open() says which).
		std::optional<discord_rules::message> wait(DWORD timeout_ms);

		void close();

	private:
		HANDLE pipe_ = INVALID_HANDLE_VALUE;
		HANDLE event_ = nullptr;
		discord_rules::frame_reader reader_;
		std::vector<discord_rules::message> queued_;

		bool read_available(); // false when the pipe is gone
	};
}
