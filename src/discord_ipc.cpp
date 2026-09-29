#include "discord_ipc.hpp"

#include <string>

namespace discord_ipc
{
	namespace
	{
		// Waits for an overlapped operation; cancels it after `timeout_ms`.
		bool finish(const HANDLE pipe, OVERLAPPED& overlapped, const BOOL started, DWORD& transferred, const DWORD timeout_ms)
		{
			if (started)
			{
				return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
			}
			if (GetLastError() != ERROR_IO_PENDING)
			{
				return false;
			}
			if (WaitForSingleObject(overlapped.hEvent, timeout_ms) != WAIT_OBJECT_0)
			{
				CancelIoEx(pipe, &overlapped);
				GetOverlappedResult(pipe, &overlapped, &transferred, TRUE); // the cancel's completion
				return false;
			}
			return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
		}
	}

	connection::~connection()
	{
		close();
		if (event_)
		{
			CloseHandle(event_);
		}
	}

	bool connection::is_open() const
	{
		return pipe_ != INVALID_HANDLE_VALUE;
	}

	bool connection::open(const std::string_view client_id, std::string& pipe, std::string& error, const DWORD timeout_ms)
	{
		close();
		if (!event_)
		{
			event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!event_)
			{
				error = "can't create an event (error " + std::to_string(GetLastError()) + ")";
				return false;
			}
		}
		DWORD last_error = ERROR_FILE_NOT_FOUND;
		for (int i = 0; i < 10 && pipe_ == INVALID_HANDLE_VALUE; ++i)
		{
			const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
			pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (pipe_ != INVALID_HANDLE_VALUE)
			{
				pipe = "discord-ipc-" + std::to_string(i);
			}
			else if (GetLastError() != ERROR_FILE_NOT_FOUND)
			{
				last_error = GetLastError(); // busy, or not ours to open: try the next
			}
		}
		if (pipe_ == INVALID_HANDLE_VALUE)
		{
			error = last_error == ERROR_FILE_NOT_FOUND ? "Discord isn't running (no \\\\.\\pipe\\discord-ipc-0..9)"
			                                           : "Discord's pipe can't be opened (error " + std::to_string(last_error) + ")";
			return false;
		}

		if (!send(discord_rules::op_handshake, discord_rules::handshake_json(client_id)))
		{
			error = "Discord didn't take the handshake on " + pipe;
			close();
			return false;
		}
		const DWORD deadline = GetTickCount() + timeout_ms;
		for (;;)
		{
			const DWORD now = GetTickCount();
			const auto reply = wait(static_cast<LONG>(deadline - now) > 0 ? deadline - now : 0);
			if (!reply)
			{
				error = is_open() ? "Discord didn't answer the handshake on " + pipe : "Discord closed " + pipe + " during the handshake";
				close();
				return false;
			}
			if (reply->op == discord_rules::op_close)
			{
				const auto code = discord_rules::json_value(reply->json, "code").value_or("?");
				const auto message = discord_rules::json_value(reply->json, "message").value_or("no reason given");
				error = "Discord refused the connection (" + code + ": " + message + ")";
				close();
				return false;
			}
			if (reply->op == discord_rules::op_ping)
			{
				send(discord_rules::op_pong, reply->json);
				continue;
			}
			if (reply->op == discord_rules::op_frame && discord_rules::json_value(reply->json, "evt") == "READY")
			{
				return true;
			}
			if (reply->op == discord_rules::op_frame && discord_rules::json_value(reply->json, "evt") == "ERROR")
			{
				error = "Discord answered the handshake with an error (" + discord_rules::json_value(reply->json, "message").value_or("no message") + ")";
				close();
				return false;
			}
		}
	}

	bool connection::send(const std::uint32_t op, const std::string_view json, const DWORD timeout_ms)
	{
		if (!is_open())
		{
			return false;
		}
		const auto bytes = discord_rules::encode(op, json);
		std::size_t written = 0;
		while (written < bytes.size())
		{
			OVERLAPPED overlapped{};
			overlapped.hEvent = event_;
			ResetEvent(event_);
			DWORD done = 0;
			const BOOL started = WriteFile(pipe_, bytes.data() + written, static_cast<DWORD>(bytes.size() - written), &done, &overlapped);
			if (!finish(pipe_, overlapped, started, done, timeout_ms) || done == 0)
			{
				close();
				return false;
			}
			written += done;
		}
		return true;
	}

	bool connection::read_available()
	{
		for (;;)
		{
			DWORD available = 0;
			if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr))
			{
				return false;
			}
			if (available == 0)
			{
				return true;
			}
			std::string chunk(std::min<DWORD>(available, 16 * 1024), '\0');
			OVERLAPPED overlapped{};
			overlapped.hEvent = event_;
			ResetEvent(event_);
			DWORD done = 0;
			const BOOL started = ReadFile(pipe_, chunk.data(), static_cast<DWORD>(chunk.size()), &done, &overlapped);
			if (!finish(pipe_, overlapped, started, done, 1000))
			{
				return false;
			}
			reader_.feed(std::string_view(chunk.data(), done));
			while (auto m = reader_.next())
			{
				queued_.push_back(std::move(*m));
			}
			if (reader_.broken())
			{
				return false; // a frame no Discord sends: the stream can't be trusted any more
			}
		}
	}

	bool connection::receive(std::vector<discord_rules::message>& out)
	{
		if (!is_open())
		{
			return false;
		}
		if (!read_available())
		{
			close();
			return false;
		}
		for (auto& m : queued_)
		{
			out.push_back(std::move(m));
		}
		queued_.clear();
		return true;
	}

	std::optional<discord_rules::message> connection::wait(const DWORD timeout_ms)
	{
		const DWORD start = GetTickCount();
		for (;;)
		{
			if (!is_open())
			{
				return std::nullopt;
			}
			if (!read_available())
			{
				close();
				return std::nullopt;
			}
			if (!queued_.empty())
			{
				auto m = std::move(queued_.front());
				queued_.erase(queued_.begin());
				return m;
			}
			if (GetTickCount() - start >= timeout_ms)
			{
				return std::nullopt;
			}
			Sleep(15);
		}
	}

	void connection::close()
	{
		if (pipe_ != INVALID_HANDLE_VALUE)
		{
			CancelIoEx(pipe_, nullptr);
			CloseHandle(pipe_);
			pipe_ = INVALID_HANDLE_VALUE;
		}
		reader_.clear();
		queued_.clear();
	}
}
