// online-status.json through the released launcher's own backend path (review 1607424 B1): the launcher's vendored
// rapidjson parses the file (default flags, no UTF-8 validation), copies the object into the command response and
// re-serialises the response with Writer<StringBuffer, UTF8<>, ASCII<>> (cef_ui_scheme_handler.cpp). That writer
// stops at a byte sequence that is not valid UTF-8 and the page gets truncated JSON. Every status line this DLL can
// write - including hostile and cut-off roster names - must survive the round trip unchanged.
// Built when XML2_FIX_LAUNCHER_RAPIDJSON names the launcher's rapidjson include folder (read-only).
//   xml2_online_status_launcher_test [samples.jsonl]
#include "online_status_core.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	int passed = 0, failed = 0;
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		(ok ? passed : failed)++;
	}
	// The backend's get-online-status + response serialisation, as released.
	bool round_trip(const std::string& text, std::string& page)
	{
		rapidjson::Document parsed;
		parsed.Parse(text.data(), text.size());
		if (parsed.HasParseError() || !parsed.IsObject()) return false;
		rapidjson::Document response;
		response.SetObject();
		auto& allocator = response.GetAllocator();
		rapidjson::Value status(rapidjson::kNullType);
		status.CopyFrom(parsed, allocator);
		response.AddMember("status", status, allocator);
		response.AddMember("statusUnreadable", false, allocator);
		rapidjson::StringBuffer buffer{};
		rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType, rapidjson::ASCII<>> writer(buffer);
		const bool complete = response.Accept(writer);
		page.assign(buffer.GetString(), buffer.GetLength());
		if (!complete) return false;
		rapidjson::Document back;
		back.Parse(page.data(), page.size());
		return !back.HasParseError() && back.IsObject() && back.HasMember("status") && back["status"] == parsed;
	}
}

int main(int argc, char** argv)
{
	namespace core = online_status_core;
	const core::utc when{2026, 10, 27, 23, 5, 9};
	std::vector<std::pair<std::string, std::string>> names = {
		{"ascii", "Logan"},
		{"valid two-byte", "Ren\xc3\xa9" "e"},
		{"valid four-byte", "Rogue \xf0\x9f\x8e\xae"},
		{"lone Latin-1 byte", "Ren\xe9" "e"},
		{"truncated lead byte", "abc\xc3"},
		{"overlong", "a\xc0\xaf" "b"},
		{"surrogate", "\xed\xa0\x80"},
		{"above U+10FFFF", "\xf4\x90\x80\x80"},
		{"0xff 0xfe", "\xff\xfe"},
		{"quote/backslash/control", "a\"b\\c\x01" "d\x7f"},
	};
	std::string forty;
	for (int i = 0; i < 20; ++i) forty += "\xc3\xa9";
	names.push_back({"40-byte UTF-8 name cut at 31", forty});
	std::string three;
	for (int i = 0; i < 11; ++i) three += "\xe2\x82\xac"; // 33 bytes of euro signs
	names.push_back({"33-byte name of 3-byte characters", three});
	for (const auto& [what, name] : names)
	{
		core::status s;
		s.who = core::role::join;
		s.now = core::state::in_game;
		s.code = "K7QN2X9M";
		s.players = 2;
		s.room_name = name;
		s.message = name;
		s.party.known = true;
		s.party.phase = "in_lobby";
		core::roster_row r;
		r.name = name;
		r.phase = "ready";
		r.ready = 1;
		r.build = "same";
		s.party.roster = {r};
		s.process.pid = 7;
		std::string page;
		check(round_trip(core::json(s, when), page), "launcher backend round trip: roster/room/message name " + what);
	}
	if (argc > 1)
	{
		std::ifstream in(argv[1], std::ios::binary);
		std::string line;
		int n = 0;
		while (std::getline(in, line))
		{
			if (line.empty()) continue;
			std::string page;
			check(round_trip(line, page), "launcher backend round trip: sample line " + std::to_string(++n) + " of the DLL's status output");
		}
		check(n > 0, "samples file had lines");
	}
	std::printf("%d PASS / %d FAIL\n", passed, failed);
	return failed ? 1 : 0;
}
