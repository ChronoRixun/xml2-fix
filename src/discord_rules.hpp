#pragma once

// Discord Rich Presence's decisions, kept apart from the pipe and the game's memory so xml2_test can
// check them without Discord or the game: the [Discord] keys, which game this is (the X-Men Legends I
// port or X-Men Legends II) and so which Discord application, the activity's text for what the game
// is doing, the JSON and the frames of Discord's local RPC pipe, the pacing of updates, and where
// XMen2.exe keeps what the presence shows, read through a "memory" (the game's own, SEH-guarded, in
// discord_presence.cpp; blocks of the test's own in xml2_test).
//
// Every address below was read from the retail XMen2.exe (image base 0x400000, no relocations) for
// this code and is in `guards`:
//
//   Zone      The zone manager is a static at 0x72a578 (its getter 0x484990 builds it there; vtable
//             0x68878c). +0x1e0: the current zone's path, 0x40 bytes, named when its load is asked for
//             (vt+0x5c 0x483f30). +0x1a0: that zone's zoneinfo savename, 0x40 bytes - the name the save
//             screen shows, cleared when a load starts and copied from the zoneinfo entry whose name
//             matches +0x1e0 (0x484f60, 0x4850c2). +0x220 & 3: a load is pending (vt+0x24 0x483e90).
//             The main menu is the zone menu/main_back, in both games.
//   Act       The game object, a static at 0x729960 (getter 0x46dce0, vtable 0x686e1c): setCurrentAct
//             stores the act as a byte at +0x5e0 (game vt+0x278, 0x469c40). XML2's zone scripts and
//             the port's (which inject setCurrentAct) keep it current.
//   Party     The same object's +0x14: four name handles, 0 for an empty slot (0x46c883). A handle is
//             an index into the game's string pool, a static at 0xa0a820 (0x602140): the text is at
//             pool + 0x8008 + [pool + 4 + (handle & 0xffffff) * 4] (the reader 0x425bc0).
//   Heroes    The stats registry, [0x71770c] (getter 0x44b8f0, vtable 0x68544c): the herostat heroes'
//             stats indices, shorts at +0x120dc, [+0x12330] of them (vt+0x48 / +0x4c); entry k at
//             +0x9b28 + k * 0x1c: +0 its name, +4 its charactername (the name the game shows), both
//             pool handles (vt+0x60 / +0x64), +0x10 its stats handle, +0x14 its level before its stats
//             exist, +0x15 its team (0x1d = hero; the port's hidden placeholders have none, vt+0x70),
//             +0x18 bit 0 = from herostat. A hero's level (vt+0x88, 0x449d20): with the bit and a
//             handle, byte +0x1c of the stats at registry + 4 + (handle & [+0x9b20]) * 0x4f8
//             (0x4b87c0), else the entry's byte.
//   Danger    A static at 0x782728; its first byte is 0xff outside the Danger Room (0x4c87a0,
//   Room      getDangerRoomActive; reset at start-up and whenever a zone loads outside it), 0xfe or
//             0xfd for its free-play modes, else the course's index: course i's record is at 0x784e00
//             + i * 0x78 (0x4c9c10), its title a pool handle at +0x40 (the loader 0x4d4474/0x4d450c).
//   Online    The network manager, a static at 0xa3df68 (0x60b210): +0x1c0 is 1 from Play Online on and
//             0 again at the main menu (0x5caf56, 0x5c9289). The session, a static at 0xa53058
//             (0x612be0, vtable 0x6a481c): +0x420 a game is set up (host or join, 0x610d20), +0x421 1
//             when this PC hosts it (0x609f5f) and 0 when it joined one (0x60a123) - both through its
//             setter 0x611050, read by its getter 0x610d10 - +0x3dc the players in it (copied from its
//             peer list, 0x61204e), +0x3dd the most it takes (4; 0x615910).
//   Screens   The menu manager ([0x8aff18]): a movie plays, the loading screen is up, or a menu is (the
//             team menu, the pause menu: the game's own test, 0x5d8870) - as the frame limiter reads
//             them (frame_rate_rules.hpp, its screen_guards).
//
// Discord's side: a named pipe \\.\pipe\discord-ipc-0..9 of the running Discord client; each frame is
// [uint32 opcode][uint32 length][JSON], little-endian. HANDSHAKE (0) {"v":1,"client_id":...} answered
// by a READY dispatch, then FRAME (1) commands such as SET_ACTIVITY with a nonce, answered in kind;
// CLOSE (2) ends it, PING (3) wants a PONG (4) with the same body. Discord clears a connection's
// activity when its pipe closes, and allows about five updates in 20 seconds. Whatever is on the
// other end of the pipe is held to what Discord sends: frames of at most 64 KiB, a few at a time.
//
// The art: both Discord applications have the same Rich Presence assets - "logo", the game's large
// image, and the small badges "menu", "cutscene", "dangerroom" and "online" for the mode.

#include "frame_rate_rules.hpp" // the menu manager's movie and loading-screen reads, and their guards
#include "limits_rules.hpp"     // guard, matches, image_base

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace discord_rules
{
	using limits_rules::guard;

	// ---- Settings ---------------------------------------------------------------------------------

	// The two Discord applications (public ids).
	inline constexpr std::string_view xml1_client_id = "1554317674606235738";
	inline constexpr std::string_view xml2_client_id = "1554317812661493791";

	enum class game
	{
		xml1, // the X-Men Legends I port, running on XMen2.exe
		xml2,
	};

	inline std::string lowercase(std::string_view text)
	{
		std::string out(text);
		for (auto& c : out)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		}
		return out;
	}

	inline std::string_view trim(std::string_view text)
	{
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
		while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
		return text;
	}

	// An ini value without a comment after it ("1   ; on by default" -> "1") or the spaces and tabs around
	// it. GetPrivateProfileString hands the comment over with the value; the launcher cuts it the same way.
	inline std::string_view value_text(std::string_view text)
	{
		const auto comment = text.find(';');
		if (comment != std::string_view::npos)
		{
			text = text.substr(0, comment);
		}
		return trim(text);
	}

	// A yes/no key of [Discord] (Enabled, ShowZone, ShowParty): 1/0, true/false, yes/no, on/off, in any
	// case, spaces and a comment after it ignored. Absent, empty or anything else: `fallback` - the
	// launcher shows a switch as on unless its key says 0 or false.
	inline bool parse_switch(const std::optional<std::string_view> value, const bool fallback)
	{
		if (!value)
		{
			return fallback;
		}
		const auto text = lowercase(value_text(*value));
		if (text == "1" || text == "true" || text == "yes" || text == "on") return true;
		if (text == "0" || text == "false" || text == "no" || text == "off") return false;
		return fallback;
	}

	// What says which game this is. The port is XML1's content on XMen2.exe: its scripts are in
	// Scripts\x1, and its xml2-fix.ini names its own save folder and a postgame script under x1/.
	struct game_clues
	{
		std::string override_value;  // [Discord] Game: xml1 or xml2
		bool x1_scripts = false;     // Scripts\x1 in the game folder or in a mod that loads (load-order.txt)
		std::string postgame_script; // [Game] PostgameScript
		std::string save_folder;     // [Game] SaveFolder
	};

	struct game_choice
	{
		game which = game::xml2;
		std::string why;
		std::string note; // a [Discord] Game the rules refused, for the log
	};

	inline game_choice choose_game(const game_clues& clues)
	{
		game_choice choice;
		const auto wanted = lowercase(value_text(clues.override_value));
		if (wanted == "xml1" || wanted == "xml2")
		{
			choice.which = wanted == "xml1" ? game::xml1 : game::xml2;
			choice.why = "[Discord] Game=" + wanted;
			return choice;
		}
		if (!wanted.empty())
		{
			choice.note = "[Discord] Game=" + std::string(value_text(clues.override_value)) + " isn't xml1 or xml2 - ignored";
		}
		const auto postgame = lowercase(value_text(clues.postgame_script));
		if (clues.x1_scripts)
		{
			choice.which = game::xml1;
			choice.why = "Scripts\\x1 is here";
		}
		else if (postgame.rfind("x1/", 0) == 0)
		{
			choice.which = game::xml1;
			choice.why = "[Game] PostgameScript=" + postgame;
		}
		else if (lowercase(value_text(clues.save_folder)) == "x-men legends")
		{
			choice.which = game::xml1;
			choice.why = "[Game] SaveFolder=X-Men Legends";
		}
		else
		{
			choice.which = game::xml2;
			choice.why = "no X-Men Legends I port here";
		}
		return choice;
	}

	inline std::string_view client_id_for(const game which)
	{
		return which == game::xml1 ? xml1_client_id : xml2_client_id;
	}

	inline std::string_view title_of(const game which)
	{
		return which == game::xml1 ? "X-Men Legends" : "X-Men Legends II";
	}

	// [Discord] ClientId, for testing with an application of one's own: a snowflake, 15 to 20 digits.
	inline bool valid_client_id(const std::string_view id)
	{
		return id.size() >= 15 && id.size() <= 20 && std::all_of(id.begin(), id.end(), [](const char c) { return c >= '0' && c <= '9'; });
	}

	// [Discord] LargeImage / SmallImage: an art asset's key in the Discord application (or an image URL),
	// printable ASCII without spaces or quotes, at most 256 characters.
	inline bool valid_asset(const std::string_view key)
	{
		return !key.empty() && key.size() <= 256 &&
		       std::all_of(key.begin(), key.end(), [](const char c) { return c > ' ' && c < 0x7f && c != '"' && c != '\\'; });
	}

	// The art both applications have: the game's large image, and a small badge for each mode.
	inline constexpr std::string_view large_image_key = "logo";
	inline constexpr std::string_view badge_menu = "menu";
	inline constexpr std::string_view badge_cutscene = "cutscene";
	inline constexpr std::string_view badge_danger_room = "dangerroom";
	inline constexpr std::string_view badge_online = "online";

	// The images the activity shows, from [Discord] LargeImage and SmallImage.
	struct images
	{
		std::string large{large_image_key}; // "": no images at all
		std::optional<std::string> badge;   // SmallImage: nullopt, each mode's own badge; "": no badge; else this key for every badge
		std::string note;                   // a value the rules refused, for the log
	};

	// LargeImage: absent, the game's logo; empty or none, no images at all; else that asset. SmallImage:
	// absent, the mode's badge; empty or none, no badge; else that asset wherever a badge shows. A value
	// that isn't an asset key is ignored (as if absent).
	inline images choose_images(const std::optional<std::string_view> large_value, const std::optional<std::string_view> small_value)
	{
		images chosen;
		const auto pick = [&](const std::optional<std::string_view> value, const char* key) -> std::optional<std::string>
		{
			if (!value)
			{
				return std::nullopt;
			}
			const auto text = value_text(*value);
			if (text.empty() || lowercase(text) == "none")
			{
				return std::string();
			}
			if (valid_asset(text))
			{
				return std::string(text);
			}
			chosen.note += std::string(chosen.note.empty() ? "" : "; ") + "[Discord] " + key + " isn't an asset key (no spaces or quotes, at most 256 characters) - ignored";
			return std::nullopt;
		};
		if (const auto own = pick(large_value, "LargeImage"))
		{
			chosen.large = *own;
		}
		chosen.badge = pick(small_value, "SmallImage");
		if (chosen.large.empty())
		{
			chosen.badge = std::string(); // no images at all
		}
		return chosen;
	}

	// ---- Text -------------------------------------------------------------------------------------

	inline void append_utf8(std::string& out, const std::uint32_t code_point)
	{
		if (code_point < 0x80)
		{
			out += static_cast<char>(code_point);
		}
		else if (code_point < 0x800)
		{
			out += static_cast<char>(0xc0 | (code_point >> 6));
			out += static_cast<char>(0x80 | (code_point & 0x3f));
		}
		else if (code_point < 0x10000)
		{
			out += static_cast<char>(0xe0 | (code_point >> 12));
			out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (code_point & 0x3f));
		}
		else
		{
			out += static_cast<char>(0xf0 | (code_point >> 18));
			out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3f));
			out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (code_point & 0x3f));
		}
	}

	// The game's text (Windows-1252: its data files' names, savenames and course titles) as UTF-8, for
	// Discord. Control characters are dropped; the five codes Windows-1252 leaves undefined become '?'.
	inline std::string utf8_from_game(const std::string_view text)
	{
		static constexpr std::array<std::uint16_t, 32> high{
			0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
			0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
		std::string out;
		for (const char c : text)
		{
			const auto byte = static_cast<unsigned char>(c);
			if (byte < 0x20 || byte == 0x7f)
			{
				continue;
			}
			if (byte >= 0x80 && byte < 0xa0)
			{
				const auto mapped = high[byte - 0x80];
				append_utf8(out, mapped ? mapped : '?');
				continue;
			}
			append_utf8(out, byte);
		}
		return out;
	}

	// A Windows-1252 letter in lower or upper case: A-Z, the Latin-1 letters (À-Þ / à-þ, but × and ÷)
	// and Š Œ Ž / š œ ž, Ÿ / ÿ. Anything else as it is.
	inline char lower_game(const char c)
	{
		const auto b = static_cast<unsigned char>(c);
		if ((b >= 'A' && b <= 'Z') || (b >= 0xc0 && b <= 0xde && b != 0xd7)) return static_cast<char>(b + 0x20);
		if (b == 0x8a || b == 0x8c || b == 0x8e) return static_cast<char>(b + 0x10);
		if (b == 0x9f) return static_cast<char>(0xff);
		return c;
	}

	inline char upper_game(const char c)
	{
		const auto b = static_cast<unsigned char>(c);
		if ((b >= 'a' && b <= 'z') || (b >= 0xe0 && b <= 0xfe && b != 0xf7)) return static_cast<char>(b - 0x20);
		if (b == 0x9a || b == 0x9c || b == 0x9e) return static_cast<char>(b - 0x10);
		if (b == 0xff) return static_cast<char>(0x9f);
		return c;
	}

	// The characters (code points) in UTF-8 text.
	inline std::size_t characters(const std::string_view utf8)
	{
		return static_cast<std::size_t>(std::count_if(utf8.begin(), utf8.end(), [](const char c) { return (static_cast<unsigned char>(c) & 0xc0) != 0x80; }));
	}

	// At most `limit` characters of UTF-8 text; a longer one is cut and ends with an ellipsis. Discord
	// takes 128 in details and state.
	inline std::string clip(const std::string_view utf8, const std::size_t limit = 128)
	{
		if (characters(utf8) <= limit || limit == 0)
		{
			return std::string(utf8);
		}
		std::size_t kept = 0;
		std::size_t end = 0;
		for (std::size_t i = 0; i < utf8.size(); ++i)
		{
			if ((static_cast<unsigned char>(utf8[i]) & 0xc0) != 0x80)
			{
				if (kept == limit - 1)
				{
					end = i;
					break;
				}
				++kept;
			}
		}
		return std::string(utf8.substr(0, end)) + "\xE2\x80\xA6"; // …
	}

	// A JSON string's body for UTF-8 text.
	inline std::string json_escape(const std::string_view utf8)
	{
		std::string out;
		for (const char c : utf8)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					static constexpr char digits[] = "0123456789abcdef";
					out += "\\u00";
					out += digits[(c >> 4) & 0xf];
					out += digits[c & 0xf];
				}
				else
				{
					out += c;
				}
			}
		}
		return out;
	}

	// ---- What the activity says -------------------------------------------------------------------

	inline constexpr std::string_view dot = " \xC2\xB7 "; // " · "

	// "" (before the first zone) or a menu zone (the main menu's menu/main_back).
	inline bool is_menu_zone(const std::string_view zone)
	{
		return zone.empty() || lowercase(zone).rfind("menu/", 0) == 0;
	}

	// A zone path's last part as words, for a zone whose zoneinfo has no savename: "mansion1a_1" ->
	// "Mansion", "danger_room" -> "Danger Room". Each part between underscores is cut at its first digit.
	// The path is the game's Windows-1252 (a mod's folder may be "montaña"); the words come out as UTF-8.
	inline std::string pretty_zone(const std::string_view path)
	{
		const auto words_of = [](const std::string_view leaf)
		{
			std::string out;
			std::string last;
			std::size_t start = 0;
			while (start <= leaf.size())
			{
				const auto end = std::min(leaf.find('_', start), leaf.size());
				auto word = leaf.substr(start, end - start);
				const auto digit = word.find_first_of("0123456789");
				word = word.substr(0, digit);
				if (!word.empty())
				{
					std::string pretty(word);
					for (auto& c : pretty) c = lower_game(c);
					pretty[0] = upper_game(pretty[0]);
					if (pretty != last)
					{
						if (!out.empty()) out += ' ';
						out += pretty;
						last = pretty;
					}
				}
				start = end + 1;
			}
			return out;
		};
		auto rest = path;
		while (!rest.empty() && (rest.back() == '/' || rest.back() == '\\')) rest.remove_suffix(1);
		while (!rest.empty())
		{
			const auto slash = rest.find_last_of("/\\");
			const auto leaf = slash == std::string_view::npos ? rest : rest.substr(slash + 1);
			const auto words = words_of(leaf);
			if (!words.empty())
			{
				return utf8_from_game(words);
			}
			if (slash == std::string_view::npos) break;
			rest = rest.substr(0, slash);
		}
		return "";
	}

	// Where the players are: "Act 1 · East Manhattan". The savename, else the path's words; the act when
	// the game has one (1 and up).
	inline std::string zone_line(const int act, const std::string_view title, const std::string_view path)
	{
		const std::string name = !title.empty() ? std::string(title) : pretty_zone(path);
		if (act >= 1 && act <= 99)
		{
			return "Act " + std::to_string(act) + (name.empty() ? "" : std::string(dot) + name);
		}
		return name;
	}

	struct hero
	{
		std::string name; // UTF-8, as the game shows it (its charactername)
		int level = 0;    // 0: unknown
		bool operator==(const hero&) const = default;
	};

	// "Wolverine Lv 3 · Cyclops Lv 1"; three or four heroes: "Wolverine, Cyclops +2 · Lv 3-5".
	inline std::string party_text(const std::vector<hero>& party)
	{
		std::vector<const hero*> named;
		for (const auto& h : party)
		{
			if (!h.name.empty()) named.push_back(&h);
		}
		const auto with_level = [](const hero& h) { return h.level > 0 ? h.name + " Lv " + std::to_string(h.level) : h.name; };
		switch (named.size())
		{
		case 0: return "";
		case 1: return with_level(*named[0]);
		case 2: return with_level(*named[0]) + std::string(dot) + with_level(*named[1]);
		default: break;
		}
		std::string text = named[0]->name + ", " + named[1]->name + " +" + std::to_string(named.size() - 2);
		int low = 0;
		int high = 0;
		for (const auto* h : named)
		{
			if (h->level <= 0) continue;
			low = low ? std::min(low, h->level) : h->level;
			high = std::max(high, h->level);
		}
		if (high > 0)
		{
			text += std::string(dot) + "Lv " + std::to_string(low) + (low != high ? "-" + std::to_string(high) : "");
		}
		return text;
	}

	// What the game is doing, as read from its memory. Text is UTF-8.
	struct snapshot
	{
		std::string zone;       // the zone path ("" before the first one)
		std::string zone_title; // its zoneinfo savename ("" when it has none)
		int act = 0;            // the game's current act (0: none yet)
		bool loading = false;   // a zone load is pending, or the loading screen is up
		bool movie = false;
		bool menu = false;      // a menu is up (the game's own test): the team menu, the pause menu, the main menu
		bool danger_room = false;
		std::string course; // the Danger Room course's title ("" in its free-play modes)
		std::vector<hero> party;
		bool online_menus = false; // Play Online chosen (its menus, before a game is set up)
		bool session = false;      // an online or LAN game is set up, hosted or joined
		bool hosting = false;
		int players = 0;
		int max_players = 0;
		bool operator==(const snapshot&) const = default;
	};

	// [Discord] ShowZone and ShowParty.
	struct display
	{
		bool zone = true;
		bool party = true;
	};

	struct activity
	{
		std::string details; // Discord's first line under the game's name
		std::string state;   // the second
		int party_size = 0;  // with party_max: "(2 of 4)" after the state
		int party_max = 0;
		std::string small_image; // the mode's badge (badge_menu...), "" in plain single-player play
		std::string small_text;  // its tooltip: the mode
		bool operator==(const activity&) const = default;
	};

	inline activity in_the_menus()
	{
		return {"In the menus", "", 0, 0, std::string(badge_menu), "In the menus"};
	}

	// The small badge and its tooltip for what `s` is doing: a cutscene, an online game (its lobby or a
	// zone), the menus, the Danger Room; none in plain single-player play.
	inline void set_badge(activity& a, const snapshot& s)
	{
		const auto badge = [&](const std::string_view key, const std::string_view text)
		{
			a.small_image = std::string(key);
			a.small_text = std::string(text);
		};
		if (s.movie) badge(badge_cutscene, "Watching a cutscene");
		else if (s.session) badge(badge_online, "Online co-op");
		else if (is_menu_zone(s.zone)) badge(badge_menu, "In the menus");
		else if (s.danger_room) badge(badge_danger_room, "Danger Room");
	}

	// The activity for `s`; nullopt while a zone loads (the last one stays up: a load is a few seconds,
	// and each update counts against Discord's rate limit).
	inline std::optional<activity> build(const snapshot& s, const display& show)
	{
		activity a;
		if (is_menu_zone(s.zone))
		{
			if (s.movie)
			{
				a.details = "Watching a cutscene";
			}
			else if (s.session)
			{
				a.details = "Online lobby";
				a.state = s.hosting ? "Hosting" : "Joined";
			}
			else
			{
				a.details = "In the menus";
				a.state = s.online_menus ? "Play Online" : "";
			}
		}
		else if (s.loading)
		{
			return std::nullopt;
		}
		else
		{
			std::string where;
			if (s.danger_room)
			{
				where = "Danger Room" + (show.zone && !s.course.empty() ? std::string(dot) + s.course : std::string());
			}
			else
			{
				where = show.zone ? zone_line(s.act, s.zone_title, s.zone) : std::string();
				if (where.empty()) where = "Playing";
			}
			if (s.movie)
			{
				a.details = "Watching a cutscene";
				a.state = show.zone || s.danger_room ? where : "";
			}
			else if (s.session)
			{
				a.details = where;
				a.state = std::string("Online co-op") + std::string(dot) + (s.hosting ? "hosting" : "joined");
			}
			else
			{
				a.details = where;
				a.state = show.party ? party_text(s.party) : "";
			}
		}
		if (s.session && s.players > 0 && s.max_players >= s.players)
		{
			a.party_size = s.players;
			a.party_max = s.max_players;
		}
		set_badge(a, s);
		a.details = clip(a.details);
		a.state = clip(a.state);
		return a;
	}

	// While a menu is up in a zone the party the game holds can be one still being chosen - the team menu
	// seats a hero as soon as he's picked, before Accept or Back - so the party last read with no menu up,
	// in that zone, stands until the menu closes. A zone the party hasn't been read in yet shows it as read.
	class party_hold
	{
	public:
		void apply(snapshot& s)
		{
			if (is_menu_zone(s.zone))
			{
				held_.reset();
				return;
			}
			if (s.menu)
			{
				if (held_ && held_->zone == s.zone)
				{
					s.party = held_->party;
				}
				return;
			}
			if (!s.loading) // a load reseats the party as it goes
			{
				held_ = held{s.zone, s.party};
			}
		}

	private:
		struct held
		{
			std::string zone;
			std::vector<hero> party;
		};
		std::optional<held> held_;
	};

	// One line for the log: "Act 1 · East Manhattan | Wolverine Lv 3 (2 of 4)".
	inline std::string describe(const activity& a)
	{
		std::string text = a.details.empty() ? "(no details)" : a.details;
		if (!a.state.empty()) text += " | " + a.state;
		if (a.party_max > 0) text += " (" + std::to_string(a.party_size) + " of " + std::to_string(a.party_max) + ")";
		return text;
	}

	// ---- Pacing -------------------------------------------------------------------------------------

	inline constexpr std::uint64_t sample_ms = 1000;       // the game's state is read once a second,
	inline constexpr std::uint64_t min_update_ms = 5000;   // an update sent at most every 5 s (Discord: ~5 per 20 s),
	inline constexpr std::uint64_t retry_ms = 20000;       // and Discord looked for every 20 s while it isn't there
	inline constexpr std::uint64_t first_connect_ms = 2000; // after the game has started

	// Updates at most one per min_update_ms: the latest activity waits its turn and replaces any older
	// one still waiting (the caller keeps only the latest).
	class gate
	{
	public:
		bool may_send(const std::uint64_t now_ms) const
		{
			return !sent_ || now_ms - last_ms_ >= min_update_ms;
		}
		void sent(const std::uint64_t now_ms)
		{
			last_ms_ = now_ms;
			sent_ = true;
		}
		void reset()
		{
			sent_ = false;
		}

	private:
		std::uint64_t last_ms_ = 0;
		bool sent_ = false;
	};

	// An activity counts once it has been read twice in a row, a sample apart: a zone load names the
	// zone, clears and sets its savename and reseats the party one after another, and the reads race the
	// game. A load (nullopt) starts over.
	class settle
	{
	public:
		std::optional<activity> seen(const std::optional<activity>& now)
		{
			if (!now)
			{
				candidate_.reset();
				count_ = 0;
				return std::nullopt;
			}
			if (candidate_ && *candidate_ == *now)
			{
				++count_;
			}
			else
			{
				candidate_ = now;
				count_ = 1;
			}
			return count_ >= 2 ? candidate_ : std::nullopt;
		}

	private:
		std::optional<activity> candidate_;
		int count_ = 0;
	};

	// ---- Frames and JSON ----------------------------------------------------------------------------

	enum opcode : std::uint32_t
	{
		op_handshake = 0,
		op_frame = 1,
		op_close = 2,
		op_ping = 3,
		op_pong = 4,
	};

	inline constexpr std::uint32_t max_frame = 64 * 1024;   // no frame of Discord's is near this long
	inline constexpr std::size_t max_read_per_poll = 64 * 1024; // bytes read from the pipe in one go; the rest waits for the next
	inline constexpr std::size_t max_queued = 64;               // frames waiting to be handled: Discord answers each command
	                                                            // once and pings now and then - more is a flood, not Discord

	inline std::string encode(const std::uint32_t op, const std::string_view json)
	{
		std::string out(8, '\0');
		const auto length = static_cast<std::uint32_t>(json.size());
		for (int i = 0; i < 4; ++i)
		{
			out[i] = static_cast<char>((op >> (8 * i)) & 0xff);
			out[4 + i] = static_cast<char>((length >> (8 * i)) & 0xff);
		}
		out.append(json);
		return out;
	}

	struct message
	{
		std::uint32_t op = 0;
		std::string json;
	};

	// Frames out of the bytes read from the pipe, however they were split. Each frame costs its own
	// length, not the buffer's: what was handed out is dropped once per feed.
	class frame_reader
	{
	public:
		void feed(const std::string_view bytes)
		{
			if (start_ > 0)
			{
				buffer_.erase(0, start_);
				start_ = 0;
			}
			buffer_.append(bytes);
		}
		std::optional<message> next()
		{
			if (broken_ || buffer_.size() - start_ < 8)
			{
				return std::nullopt;
			}
			const auto u32 = [&](const std::size_t at)
			{
				std::uint32_t value = 0;
				for (int i = 3; i >= 0; --i) value = (value << 8) | static_cast<unsigned char>(buffer_[start_ + at + static_cast<std::size_t>(i)]);
				return value;
			};
			const auto op = u32(0);
			const auto length = u32(4);
			if (length > max_frame)
			{
				broken_ = true;
				return std::nullopt;
			}
			if (buffer_.size() - start_ < 8 + static_cast<std::size_t>(length))
			{
				return std::nullopt;
			}
			message m{op, buffer_.substr(start_ + 8, length)};
			start_ += 8 + static_cast<std::size_t>(length);
			if (start_ == buffer_.size())
			{
				buffer_.clear();
				start_ = 0;
			}
			return m;
		}
		bool broken() const
		{
			return broken_;
		}
		void clear()
		{
			buffer_.clear();
			start_ = 0;
			broken_ = false;
		}

	private:
		std::string buffer_;
		std::size_t start_ = 0; // the first byte not handed out yet
		bool broken_ = false;
	};

	// The frames read from the pipe, waiting to be handled - held to what Discord sends. A frame longer
	// than max_frame, or more than max_queued frames waiting at once, means whatever is on the other end
	// isn't Discord (or is broken): the stream can't be trusted any more, everything waiting is dropped,
	// and nothing more is taken or handed out until clear() - the caller closes the pipe.
	class inbox
	{
	public:
		bool take(const std::string_view bytes)
		{
			if (problem_)
			{
				return false;
			}
			reader_.feed(bytes);
			while (auto m = reader_.next())
			{
				if (queued_.size() >= max_queued)
				{
					fail("more than 64 frames at once");
					return false;
				}
				queued_.push_back(std::move(*m));
			}
			if (reader_.broken())
			{
				fail("a frame longer than 64 KiB");
				return false;
			}
			return true;
		}
		std::optional<message> pop()
		{
			if (problem_ || queued_.empty())
			{
				return std::nullopt;
			}
			auto m = std::move(queued_.front());
			queued_.pop_front();
			return m;
		}
		std::size_t waiting() const
		{
			return queued_.size();
		}
		// Why the stream can't be trusted; null while it can.
		const char* problem() const
		{
			return problem_;
		}
		void clear()
		{
			reader_.clear();
			queued_.clear();
			problem_ = nullptr;
		}

	private:
		void fail(const char* why)
		{
			problem_ = why;
			queued_.clear();
			reader_.clear();
		}

		frame_reader reader_;
		std::deque<message> queued_;
		const char* problem_ = nullptr;
	};

	inline std::string handshake_json(const std::string_view client_id)
	{
		return "{\"v\":1,\"client_id\":\"" + json_escape(client_id) + "\"}";
	}

	// The online party's id, as Discord shows it to whoever sees the activity: "xml2fix-" and 16 hex
	// digits of `high` and `low` - random ones (random_party_id), so it says nothing about this PC.
	inline std::string party_id_of(const std::uint32_t high, const std::uint32_t low)
	{
		char text[32]{};
		std::snprintf(text, sizeof(text), "xml2fix-%08x%08x", static_cast<unsigned>(high), static_cast<unsigned>(low));
		return text;
	}

	// A new party id for this run of the game: the OS's random numbers (rand_s), nothing else. Throws
	// when there are none; call it off the loader lock (it may load the system's random number DLL).
	inline std::string random_party_id()
	{
		std::random_device random;
		const auto high = static_cast<std::uint32_t>(random());
		return party_id_of(high, static_cast<std::uint32_t>(random()));
	}

	struct extras
	{
		std::int64_t start = 0;                 // the elapsed timer's start, Unix seconds; 0: none
		std::string large_image;                // "": no images at all
		std::string large_text;                 // its tooltip: the game's title
		std::optional<std::string> small_image; // [Discord] SmallImage: nullopt, each mode's badge; "": none; else it
		std::string party_id;                   // this run's party, when online; "": no party size
	};

	inline std::string set_activity_json(const DWORD pid, const activity& a, const extras& x, const unsigned nonce)
	{
		std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":{";
		std::string fields;
		const auto add = [&](const std::string& field)
		{
			if (!fields.empty()) fields += ',';
			fields += field;
		};
		// Discord wants at least two characters in details and state.
		if (characters(a.details) >= 2) add("\"details\":\"" + json_escape(a.details) + "\"");
		if (characters(a.state) >= 2) add("\"state\":\"" + json_escape(a.state) + "\"");
		if (x.start > 0) add("\"timestamps\":{\"start\":" + std::to_string(x.start) + "}");
		std::string assets;
		const auto add_asset = [&](const std::string& field)
		{
			if (!assets.empty()) assets += ',';
			assets += field;
		};
		if (!x.large_image.empty())
		{
			add_asset("\"large_image\":\"" + json_escape(x.large_image) + "\"");
			if (characters(x.large_text) >= 2) add_asset("\"large_text\":\"" + json_escape(x.large_text) + "\"");
			// The mode's badge, or SmallImage's key in its place; nothing in plain single-player play.
			const std::string& badge = x.small_image && !a.small_image.empty() ? *x.small_image : a.small_image;
			if (!badge.empty())
			{
				add_asset("\"small_image\":\"" + json_escape(badge) + "\"");
				if (characters(a.small_text) >= 2) add_asset("\"small_text\":\"" + json_escape(clip(a.small_text)) + "\"");
			}
		}
		if (!assets.empty()) add("\"assets\":{" + assets + "}");
		if (a.party_max > 0 && !x.party_id.empty())
		{
			add("\"party\":{\"id\":\"" + json_escape(x.party_id) + "\",\"size\":[" + std::to_string(a.party_size) + "," + std::to_string(a.party_max) + "]}");
		}
		json += fields + "}},\"nonce\":\"" + std::to_string(nonce) + "\"}";
		return json;
	}

	// No activity: Discord clears this connection's.
	inline std::string clear_activity_json(const DWORD pid, const unsigned nonce)
	{
		return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
	}

	// The first value of `key` in Discord's JSON: a string unescaped, anything else as written (null,
	// numbers). Enough for evt, cmd, code and message; not a JSON parser.
	inline std::optional<std::string> json_value(const std::string_view json, const std::string_view key)
	{
		const std::string quoted = "\"" + std::string(key) + "\"";
		std::size_t at = 0;
		while ((at = json.find(quoted, at)) != std::string_view::npos)
		{
			auto i = at + quoted.size();
			while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
			if (i >= json.size() || json[i] != ':')
			{
				at = i; // "key" was a value, not a key
				continue;
			}
			++i;
			while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
			if (i >= json.size())
			{
				return std::nullopt;
			}
			if (json[i] != '"')
			{
				const auto end = json.find_first_of(",}] \t\r\n", i);
				return std::string(json.substr(i, end == std::string_view::npos ? json.size() - i : end - i));
			}
			std::string out;
			for (++i; i < json.size(); ++i)
			{
				const char c = json[i];
				if (c == '"')
				{
					return out;
				}
				if (c != '\\' || i + 1 >= json.size())
				{
					out += c;
					continue;
				}
				const char e = json[++i];
				switch (e)
				{
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'u':
					if (i + 4 < json.size())
					{
						std::uint32_t code = 0;
						bool ok = true;
						for (int d = 1; d <= 4; ++d)
						{
							const char h = json[i + static_cast<std::size_t>(d)];
							code <<= 4;
							if (h >= '0' && h <= '9') code |= static_cast<std::uint32_t>(h - '0');
							else if (h >= 'a' && h <= 'f') code |= static_cast<std::uint32_t>(h - 'a' + 10);
							else if (h >= 'A' && h <= 'F') code |= static_cast<std::uint32_t>(h - 'A' + 10);
							else ok = false;
						}
						if (ok)
						{
							append_utf8(out, code);
							i += 4;
						}
					}
					break;
				default: out += e; break; // \" \\ \/
				}
			}
			return std::nullopt; // unterminated
		}
		return std::nullopt;
	}

	// ---- Where the game keeps it -------------------------------------------------------------------

	constexpr DWORD zones = 0x72a578;
	constexpr DWORD zones_vtable = 0x68878c;
	constexpr DWORD zone_path = 0x1e0;
	constexpr DWORD zone_savename = 0x1a0;
	constexpr DWORD zone_loading = 0x220; // & 3
	constexpr std::size_t zone_text_size = 0x40;

	constexpr DWORD game_object = 0x729960;
	constexpr DWORD game_vtable = 0x686e1c;
	constexpr DWORD game_party = 0x14; // four pool handles
	constexpr DWORD game_act = 0x5e0;  // byte

	constexpr DWORD pool = 0xa0a820;
	constexpr DWORD pool_offsets = 4;
	constexpr DWORD pool_text = 0x8008;
	constexpr DWORD pool_slots = 0x2000;
	constexpr std::size_t name_text_size = 64;

	constexpr DWORD registry_cell = 0x71770c;
	constexpr DWORD registry_vtable = 0x68544c;
	constexpr DWORD registry_entries = 0x9b28;
	constexpr DWORD entry_size = 0x1c;
	constexpr DWORD entry_name = 0x00;
	constexpr DWORD entry_character = 0x04;
	constexpr DWORD entry_stats = 0x10;
	constexpr DWORD entry_level = 0x14;
	constexpr DWORD entry_team = 0x15;
	constexpr DWORD entry_flags = 0x18;
	constexpr std::uint8_t team_hero = 0x1d;
	constexpr DWORD registry_heroes = 0x120dc; // shorts
	constexpr DWORD registry_hero_count = 0x12330;
	constexpr int registry_hero_max = 298;     // the list's room
	constexpr std::uint16_t registry_names_max = 0x129; // stats indices: the loader takes 296 names (0x44c1a7)
	constexpr DWORD registry_stats_mask = 0x9b20;
	constexpr DWORD registry_stats = 4;
	constexpr DWORD stats_size = 0x4f8;
	constexpr DWORD stats_level = 0x1c;

	constexpr DWORD danger_room_state = 0x782728; // byte
	constexpr std::uint8_t danger_room_off = 0xff;
	constexpr std::uint8_t danger_room_free_play = 0xfd; // 0xfd and 0xfe: no course
	constexpr DWORD courses = 0x784e00;
	constexpr DWORD course_size = 0x78;
	constexpr DWORD course_title = 0x40;

	constexpr DWORD network = 0xa3df68;
	constexpr DWORD network_mode = 0x1c0; // 1: Play Online
	constexpr DWORD session = 0xa53058;
	constexpr DWORD session_vtable = 0x6a481c;
	constexpr DWORD session_active = 0x420;  // byte
	constexpr DWORD session_hosting = 0x421; // byte
	constexpr DWORD session_players = 0x3dc; // signed byte
	constexpr DWORD session_max = 0x3dd;     // signed byte

	constexpr DWORD menu_manager_vtable = 0x6a236c;

	// Every byte the reads rely on, from the retail build. Checked once in the game: when one doesn't
	// match, nothing is read and the presence shows only the game's name.
	inline constexpr std::array<guard, 44> guards{{
		// The zone manager.
		{0x4849ba, "b978a57200c744240800000000e824f2ffff68d0db6700e848d71e0083c4048b0c24b878a57200", "the zone manager is built at 0x72a578 and handed out (0x484990)"},
		{0x483c1f, "c7068c876800", "its constructor sets the vtable 0x68878c (0x483c1f)"},
		{0x483f30, "8d81e0010000c3", "zones vt+0x5c: the current zone's path at +0x1e0 (0x483f30)"},
		{0x6887e8, "303f4800", "zones vt+0x5c = 0x483f30 (0x6887e8)"},
		{0x483e90, "f6812002000003", "zones vt+0x24: a load is pending, [+0x220] & 3 (0x483e90)"},
		{0x6887b0, "903e4800", "zones vt+0x24 = 0x483e90 (0x6887b0)"},
		{0x484f60, "8d9da0010000686819680053e85f6ef8ff", "a load clears the savename at +0x1a0 (0x484f60)"},
		{0x48508c, "8d85e0010000", "the zoneinfo entry named as +0x1e0 (0x48508c)"},
		{0x4850c2, "686819680068486d68008d4c2450e89bfa0d006a405053e8f26cf8ff", "gives +0x1a0 its savename, 0x40 bytes (0x4850c2)"},
		// The game object: act and party.
		{0x46dd0a, "b960997200", "the game object is built at 0x729960 (0x46dd0a)"},
		{0x46dd2c, "b860997200", "and handed out (0x46dd2c)"},
		{0x468e6d, "c7061c6e6800", "its constructor sets the vtable 0x686e1c (0x468e6d)"},
		{0x469c40, "0fb681e0050000", "setCurrentAct's act byte at +0x5e0 (0x469c40)"},
		{0x687094, "409c4600", "game vt+0x278 = 0x469c40 (0x687094)"},
		{0x46c883, "8b74bb14", "the party's name handles at +0x14 (0x46c883)"},
		// The string pool.
		{0x425bc0, "568b3185f67507b8681968005ec3e86dc51d0081e6ffffff008b4cb0048d8401088000005ec3", "a handle's text: pool + 0x8008 + [pool + 4 + index * 4] (0x425bc0)"},
		{0x60218e, "b820a8a000c3", "the pool is the static 0xa0a820 (0x60218e)"},
		// The stats registry.
		{0x44b8fe, "a10c777100", "the registry is [0x71770c] (0x44b8fe)"},
		{0x44b541, "c7064c546800", "its constructor sets the vtable 0x68544c (0x44b541)"},
		{0x44b6b0, "8b8130230100c3", "registry vt+0x48: the heroes' count at +0x12330 (0x44b6b0)"},
		{0x44b6e0, "8b442404668b8441dc200100c20400", "registry vt+0x4c: their stats indices, shorts at +0x120dc (0x44b6e0)"},
		{0x44b700, "0fbf4424046bc01c8d8408289b0000c20400", "registry vt+0x60: entry k at +0x9b28 + k * 0x1c, its name (0x44b700)"},
		{0x44b720, "0fbf4424046bc01c8d84082c9b0000c20400", "registry vt+0x64: its charactername at +4 (0x44b720)"},
		{0x44b780, "0fbf4424046bc01c0fb684083d9b0000c20400", "registry vt+0x70: its team at +0x15 (0x44b780)"},
		{0x449d20, "0fbf4424046bc01c8a9408409b0000f6c2018d8408289b000074228b501085d2741b8d41048b881c9b000023ca69c9f804000003c8e866ea0600c204008a4014c204",
		 "registry vt+0x88: a hero's level - its stats' or the entry's +0x14 (0x449d20)"},
		{0x685494, "b0b64400", "registry vt+0x48 = 0x44b6b0 (0x685494)"},
		{0x6854d4, "209d4400", "registry vt+0x88 = 0x449d20 (0x6854d4)"},
		{0x4b87c0, "5153578bf9660fb65f1c", "the stats' level byte at +0x1c (0x4b87c0)"},
		// The Danger Room.
		{0x4c87a0, "8a0d2827780033c080f9ff0f95c0c3", "the Danger Room is on unless [0x782728] is 0xff (0x4c87a0)"},
		{0x4c9c10, "0fb64424046bc07805004e7800c3", "course i's record at 0x784e00 + i * 0x78 (0x4c9c10)"},
		{0x4d4474, "8b352c7178008b4f1c414689352c7178006bf6788db6884d7800", "the loader's records, 0x78 bytes (0x4d4474)"},
		{0x4d450c, "894640", "the course's title at +0x40 (0x4d450c)"},
		// Online.
		{0x60b23a, "b968dfa300", "the network manager is built at 0xa3df68 (0x60b23a)"},
		{0x60a770, "8b4424048981c0010000c20400", "its mode at +0x1c0 (0x60a770)"},
		{0x5caf56, "6a01e8b30204008bc8e80cf80300", "Play Online sets it to 1 (0x5caf56)"},
		{0x612c0a, "b95830a500", "the session is built at 0xa53058 (0x612c0a)"},
		{0x612136, "c7061c486a00", "its constructor sets the vtable 0x6a481c (0x612136)"},
		{0x610d20, "8a8120040000c3", "a game is set up: +0x420 (0x610d20)"},
		{0x609f5f, "6a01e87a8c00008bc8e8e3700000", "hosting sets +0x421 to 1 through 0x611050 (0x609f5f)"},
		{0x60a123, "6a00e8b68a00008bc8e81f6f0000", "joining sets +0x421 to 0 through 0x611050 (0x60a123)"},
		{0x611050, "8a44240484c0568bf1888621040000", "0x611050 stores its argument at +0x421 (0x611050)"},
		{0x610d10, "8a8121040000c3", "this PC hosts: +0x421 (0x610d10)"},
		{0x615910, "53e8cad2ffff8a98dd0300008a88dc03000005a403000033d23acb0f9cc2", "its players at +0x3dc, at most +0x3dd (0x615910)"},
		{0x61204e, "8a8f78020000888fdc030000", "+0x3dc is the peer list's count (0x61204e)"},
	}};

	// The first guard `image` (XMen2.exe's image base) doesn't match - these and the frame limiter's
	// screen guards - or null.
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex))
			{
				return &g;
			}
		}
		return frame_rate_rules::first_screen_mismatch(image);
	}

	// ---- Reading it ---------------------------------------------------------------------------------

	// Reads XMen2.exe's state through `memory`: bool read(DWORD va, void* out, std::size_t size), false
	// when the bytes can't be read. nullopt when what it reads doesn't hold together (a torn read, a
	// fault): the caller keeps the last activity.
	template <typename Memory>
	class game_reader
	{
	public:
		explicit game_reader(Memory& memory) : memory_(memory)
		{
		}

		std::optional<snapshot> read()
		{
			snapshot s;
			const auto zone_vt = u32(zones);
			if (!zone_vt)
			{
				return std::nullopt;
			}
			if (*zone_vt == 0)
			{
				return s; // the game is starting: no zone manager yet
			}
			if (*zone_vt != zones_vtable)
			{
				return std::nullopt;
			}
			const auto path = text(zones + zone_path, zone_text_size);
			const auto title = text(zones + zone_savename, zone_text_size);
			const auto loading = u32(zones + zone_loading);
			if (!path || !title || !loading || !valid_path(*path))
			{
				return std::nullopt;
			}
			s.zone = *path;
			s.zone_title = utf8_from_game(trim(*title));
			s.loading = (*loading & 3) != 0;

			read_screens(s);
			read_game(s);
			read_danger_room(s);
			read_online(s);
			return s;
		}

	private:
		Memory& memory_;

		std::optional<std::uint32_t> u32(const DWORD va)
		{
			std::uint32_t value = 0;
			return memory_.read(va, &value, sizeof(value)) ? std::optional<std::uint32_t>(value) : std::nullopt;
		}

		std::optional<std::uint8_t> u8(const DWORD va)
		{
			std::uint8_t value = 0;
			return memory_.read(va, &value, sizeof(value)) ? std::optional<std::uint8_t>(value) : std::nullopt;
		}

		// A C string of at most `size` bytes: read whole, or byte by byte when the whole block can't be (the
		// string may end just before an unreadable page).
		std::optional<std::string> text(const DWORD va, const std::size_t size)
		{
			std::string bytes(size, '\0');
			if (memory_.read(va, bytes.data(), size))
			{
				bytes.resize(std::strlen(bytes.c_str()));
				return bytes;
			}
			std::string out;
			for (std::size_t i = 0; i < size; ++i)
			{
				char c = 0;
				if (!memory_.read(va + static_cast<DWORD>(i), &c, 1))
				{
					return std::nullopt;
				}
				if (!c) break;
				out += c;
			}
			return out;
		}

		// A string pool handle's text ("" for 0).
		std::optional<std::string> pooled(const std::uint32_t handle)
		{
			if (!handle)
			{
				return std::string();
			}
			const DWORD index = handle & 0xffffff;
			if (index >= pool_slots)
			{
				return std::nullopt;
			}
			const auto offset = u32(pool + pool_offsets + index * 4);
			if (!offset || *offset >= 0x1000000)
			{
				return std::nullopt;
			}
			return text(pool + pool_text + *offset, name_text_size);
		}

	public:
		// A zone path has no control characters; one that has any is a read that raced a load. Bytes of
		// 0x80 and up are the game's code page, Windows-1252 (a mod's "montaña"): pretty_zone turns them
		// into UTF-8, and they never fail the read.
		static bool valid_path(const std::string_view path)
		{
			return std::none_of(path.begin(), path.end(), [](const char c)
			{
				const auto b = static_cast<unsigned char>(c);
				return b < 0x20 || b == 0x7f;
			});
		}

	private:
		void read_screens(snapshot& s)
		{
			const auto manager = u32(frame_rate_rules::menu_manager_cell);
			if (!manager || !*manager || u32(*manager) != menu_manager_vtable)
			{
				return;
			}
			if (const auto flags = u8(*manager + frame_rate_rules::menu_flags))
			{
				s.movie = (*flags & frame_rate_rules::menu_movie_bit) != 0;
			}
			const auto current = u32(*manager + frame_rate_rules::menu_current);
			if (current && *current)
			{
				s.loading = s.loading || u32(*current) == frame_rate_rules::loading_menu_vtable;
			}
			// A menu is up as the game tests it (0x5d8870): a stack, its first entry, and a current menu or
			// the name of one about to open.
			const auto count = u32(*manager + frame_rate_rules::menu_stack_count);
			const auto first = u32(*manager + frame_rate_rules::menu_stack_first);
			const auto pending = u8(*manager + frame_rate_rules::menu_pending);
			s.menu = count && first && current && pending && static_cast<std::int32_t>(*count) > 0 && *first != 0 && (*current != 0 || *pending != 0);
		}

		void read_game(snapshot& s)
		{
			if (u32(game_object) != game_vtable)
			{
				return; // not built yet
			}
			if (const auto act = u8(game_object + game_act))
			{
				s.act = *act;
			}
			const auto registry = u32(registry_cell);
			if (!registry || !*registry || u32(*registry) != registry_vtable)
			{
				return;
			}
			const auto count = u32(*registry + registry_hero_count);
			const auto mask = u32(*registry + registry_stats_mask);
			if (!count || !mask || static_cast<int>(*count) < 0 || static_cast<int>(*count) > registry_hero_max)
			{
				return;
			}
			for (DWORD slot = 0; slot < 4; ++slot)
			{
				const auto handle = u32(game_object + game_party + slot * 4);
				if (!handle || !*handle)
				{
					continue;
				}
				const auto id = pooled(*handle);
				if (!id || id->empty())
				{
					continue;
				}
				if (auto h = hero_named(*registry, *count, *mask, lowercase(*id)))
				{
					s.party.push_back(std::move(*h));
				}
			}
		}

		// The herostat hero named `id` (the party keeps names lowercased), as the game shows it.
		std::optional<hero> hero_named(const DWORD registry, const std::uint32_t count, const std::uint32_t mask, const std::string& id)
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				std::uint16_t index = 0;
				if (!memory_.read(registry + registry_heroes + i * 2, &index, sizeof(index)) || index == 0 || index > registry_names_max)
				{
					continue;
				}
				const DWORD entry = registry + registry_entries + index * entry_size;
				const auto name_handle = u32(entry + entry_name);
				if (!name_handle)
				{
					continue;
				}
				const auto name = pooled(*name_handle);
				if (!name || lowercase(*name) != id)
				{
					continue;
				}
				if (u8(entry + entry_team) != team_hero)
				{
					return std::nullopt; // a hidden placeholder, not a hero
				}
				hero h;
				const auto character = u32(entry + entry_character);
				const auto shown = character ? pooled(*character) : std::nullopt;
				h.name = utf8_from_game(trim(shown && !shown->empty() ? *shown : *name));
				const auto flags = u8(entry + entry_flags);
				const auto stats = u32(entry + entry_stats);
				std::optional<std::uint8_t> level;
				if (flags && stats && (*flags & 1) && *stats)
				{
					level = u8(registry + registry_stats + (*stats & mask) * stats_size + stats_level);
				}
				else
				{
					level = u8(entry + entry_level);
				}
				h.level = level ? *level : 0;
				return h;
			}
			return std::nullopt;
		}

		void read_danger_room(snapshot& s)
		{
			const auto state = u8(danger_room_state);
			if (!state || *state == danger_room_off)
			{
				return;
			}
			s.danger_room = true;
			if (*state < danger_room_free_play)
			{
				if (const auto title = u32(courses + *state * course_size + course_title))
				{
					if (const auto course = pooled(*title))
					{
						s.course = utf8_from_game(trim(*course));
					}
				}
			}
		}

		void read_online(snapshot& s)
		{
			s.online_menus = u32(network + network_mode) == 1u;
			if (u32(session) != session_vtable)
			{
				return; // no online game this run
			}
			const auto active = u8(session + session_active);
			if (!active || !*active)
			{
				return;
			}
			s.session = true;
			s.hosting = u8(session + session_hosting).value_or(0) != 0;
			s.players = static_cast<std::int8_t>(u8(session + session_players).value_or(0));
			s.max_players = static_cast<std::int8_t>(u8(session + session_max).value_or(0));
		}
	};
}
