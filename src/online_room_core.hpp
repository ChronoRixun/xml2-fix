#pragma once

// [Online] Host=broker / JoinCode (docs/online-rooms.md): the game-independent half of joining over the internet -
// the broker client protocol's lines (net030-broker SPEC.md v2: one flat JSON object per LF-terminated line, binary
// fields in standard base64), the authentication message, identities, join codes, the virtual addresses the game sees
// for room members, and the certificate blob GNS's SetCertificate takes. No Windows, GNS or game calls here, so
// test/online_room_test.cpp drives it directly.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace online_room_core
{
	// ---- base64 (standard alphabet, padded) ----
	inline std::string base64(const std::uint8_t* data, std::size_t size)
	{
		static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((size + 2) / 3 * 4);
		for (std::size_t i = 0; i < size; i += 3)
		{
			const std::uint32_t n = (std::uint32_t(data[i]) << 16) | (i + 1 < size ? std::uint32_t(data[i + 1]) << 8 : 0) | (i + 2 < size ? data[i + 2] : 0);
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += i + 1 < size ? table[(n >> 6) & 63] : '=';
			out += i + 2 < size ? table[n & 63] : '=';
		}
		return out;
	}
	inline std::string base64(const std::vector<std::uint8_t>& data) { return base64(data.data(), data.size()); }
	// Whitespace is skipped; anything else outside the alphabet, or a broken length, fails.
	inline bool unbase64(const std::string& text, std::vector<std::uint8_t>& out)
	{
		out.clear();
		std::uint32_t bits = 0;
		int count = 0, padding = 0;
		for (const char c : text)
		{
			int v;
			if (c >= 'A' && c <= 'Z') v = c - 'A';
			else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
			else if (c >= '0' && c <= '9') v = c - '0' + 52;
			else if (c == '+') v = 62;
			else if (c == '/') v = 63;
			else if (c == '=') { ++padding; continue; }
			else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
			else return false;
			if (padding) return false;
			bits = (bits << 6) | std::uint32_t(v);
			if (++count == 4)
			{
				out.push_back(std::uint8_t(bits >> 16));
				out.push_back(std::uint8_t(bits >> 8));
				out.push_back(std::uint8_t(bits));
				bits = 0;
				count = 0;
			}
		}
		if (count == 1 || padding > 2) return false;
		if (count == 2) out.push_back(std::uint8_t(bits >> 4));
		if (count == 3) { out.push_back(std::uint8_t(bits >> 10)); out.push_back(std::uint8_t(bits >> 2)); }
		return true;
	}

	// ---- one broker line: a flat JSON object (strings, numbers, booleans, null, arrays of strings) ----
	struct value
	{
		enum class type { missing, string, number, boolean, null, list, other } t = type::missing;
		std::string text;               // string contents, or a number / boolean as written
		std::vector<std::string> items; // list of strings
	};
	struct line
	{
		bool valid = false;
		std::map<std::string, value> fields;
		const value& operator[](const std::string& key) const
		{
			static const value none;
			const auto it = fields.find(key);
			return it == fields.end() ? none : it->second;
		}
		std::string text(const std::string& key) const { const auto& v = (*this)[key]; return v.t == value::type::string ? v.text : std::string(); }
		long long number(const std::string& key, long long fallback = 0) const
		{
			const auto& v = (*this)[key];
			return v.t == value::type::number ? std::strtoll(v.text.c_str(), nullptr, 10) : fallback;
		}
	};
	namespace detail
	{
		inline void space(const std::string& s, std::size_t& i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i; }
		inline void utf8(std::string& out, std::uint32_t c)
		{
			if (c < 0x80) out += char(c);
			else if (c < 0x800) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
			else if (c < 0x10000) { out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
			else { out += char(0xf0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
		}
		inline bool hex4(const std::string& s, std::size_t i, std::uint32_t& c)
		{
			if (i + 4 > s.size()) return false;
			c = 0;
			for (std::size_t k = 0; k < 4; ++k)
			{
				const char h = s[i + k];
				c <<= 4;
				if (h >= '0' && h <= '9') c |= std::uint32_t(h - '0');
				else if (h >= 'a' && h <= 'f') c |= std::uint32_t(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F') c |= std::uint32_t(h - 'A' + 10);
				else return false;
			}
			return true;
		}
		inline bool string(const std::string& s, std::size_t& i, std::string& out)
		{
			if (i >= s.size() || s[i] != '"') return false;
			++i;
			out.clear();
			while (i < s.size())
			{
				const char c = s[i++];
				if (c == '"') return true;
				if (c != '\\') { out += c; continue; }
				if (i >= s.size()) return false;
				const char e = s[i++];
				switch (e)
				{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					std::uint32_t c1;
					if (!hex4(s, i, c1)) return false;
					i += 4;
					if (c1 >= 0xd800 && c1 < 0xdc00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
					{
						std::uint32_t c2;
						if (hex4(s, i + 2, c2) && c2 >= 0xdc00 && c2 < 0xe000) { c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00); i += 6; }
					}
					utf8(out, c1);
					break;
				}
				default: return false;
				}
			}
			return false;
		}
		// Skips one nested value it does not keep (an object or a list of non-strings), bracket-balanced.
		inline bool skip(const std::string& s, std::size_t& i)
		{
			int depth = 0;
			std::string ignored;
			while (i < s.size())
			{
				const char c = s[i];
				if (c == '"') { if (!string(s, i, ignored)) return false; if (!depth) return true; continue; }
				if (c == '{' || c == '[') ++depth;
				else if (c == '}' || c == ']') { if (--depth < 0) return false; if (!depth) { ++i; return true; } }
				else if (!depth && (c == ',')) return true;
				++i;
			}
			return false;
		}
	}
	inline line parse(const std::string& s)
	{
		using namespace detail;
		line out;
		std::size_t i = 0;
		space(s, i);
		if (i >= s.size() || s[i] != '{') return out;
		++i;
		space(s, i);
		if (i < s.size() && s[i] == '}') { out.valid = true; return out; }
		while (i < s.size())
		{
			std::string key;
			space(s, i);
			if (!string(s, i, key)) return out;
			space(s, i);
			if (i >= s.size() || s[i] != ':') return out;
			++i;
			space(s, i);
			if (i >= s.size()) return out;
			value v;
			const char c = s[i];
			if (c == '"') { v.t = value::type::string; if (!string(s, i, v.text)) return out; }
			else if (c == '[')
			{
				const std::size_t start = i;
				++i;
				space(s, i);
				v.t = value::type::list;
				bool strings = true;
				if (i < s.size() && s[i] == ']') ++i;
				else
					while (i < s.size())
					{
						space(s, i);
						if (i < s.size() && s[i] == '"')
						{
							std::string item;
							if (!string(s, i, item)) return out;
							v.items.push_back(item);
						}
						else { strings = false; break; }
						space(s, i);
						if (i < s.size() && s[i] == ',') { ++i; continue; }
						if (i < s.size() && s[i] == ']') { ++i; break; }
						return out;
					}
				if (!strings) { i = start; if (!skip(s, i)) return out; v = value{}; v.t = value::type::other; }
			}
			else if (c == '{') { const std::size_t start = i; if (!skip(s, i)) return out; v.t = value::type::other; v.text = s.substr(start, i - start); } // raw object text (parse() it again for its fields)
			else
			{
				const std::size_t start = i;
				while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ' ' && s[i] != '\r' && s[i] != '\n' && s[i] != '\t') ++i;
				v.text = s.substr(start, i - start);
				if (v.text == "true" || v.text == "false") v.t = value::type::boolean;
				else if (v.text == "null") v.t = value::type::null;
				else if (!v.text.empty() && (v.text[0] == '-' || std::isdigit(static_cast<unsigned char>(v.text[0])))) v.t = value::type::number;
				else return out;
			}
			out.fields[key] = std::move(v);
			space(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
			if (i < s.size() && s[i] == '}') { out.valid = true; return out; }
			return out;
		}
		return out;
	}
	// A JSON string literal (quotes included), control characters escaped.
	inline std::string quote(const std::string& s)
	{
		std::string out = "\"";
		for (const char c : s)
		{
			if (c == '"' || c == '\\') { out += '\\'; out += c; }
			else if (c == '\n') out += "\\n";
			else if (c == '\r') out += "\\r";
			else if (c == '\t') out += "\\t";
			else if (static_cast<unsigned char>(c) < 0x20) { char e[8]; std::snprintf(e, sizeof(e), "\\u%04x", unsigned(static_cast<unsigned char>(c))); out += e; }
			else out += c;
		}
		return out + "\"";
	}
	// {"op":<op>, <key>:<string>...}\n
	inline std::string request(const std::string& op, const std::vector<std::pair<std::string, std::string>>& strings = {})
	{
		std::string out = "{\"op\":" + quote(op);
		for (const auto& [k, v] : strings) out += "," + quote(k) + ":" + quote(v);
		return out + "}\n";
	}

	// ---- authentication and identities (SPEC.md section 2) ----
	constexpr std::size_t nonce_size = 16, public_key_size = 32, signature_size = 64, seed_size = 32;
	// What the client signs: "LCB1-auth-v2\0" || nonce (16 raw bytes) || public key (32 raw bytes).
	inline std::vector<std::uint8_t> auth_message(const std::vector<std::uint8_t>& nonce, const std::uint8_t (&public_key)[public_key_size])
	{
		static const char prefix[] = "LCB1-auth-v2"; // + its terminating zero byte
		std::vector<std::uint8_t> out(prefix, prefix + sizeof(prefix));
		out.insert(out.end(), nonce.begin(), nonce.end());
		out.insert(out.end(), public_key, public_key + public_key_size);
		return out;
	}
	// The broker derives the identity from the key: "legends:" + the first 8 bytes of the public key, lowercase hex.
	inline std::string identity(const std::uint8_t (&public_key)[public_key_size])
	{
		std::string out = "legends:";
		char hex[3];
		for (int i = 0; i < 8; ++i) { std::snprintf(hex, sizeof(hex), "%02x", unsigned(public_key[i])); out += hex; }
		return out;
	}
	inline bool valid_identity(const std::string& id)
	{
		if (id.size() != 24 || id.compare(0, 8, "legends:") != 0) return false;
		for (std::size_t i = 8; i < id.size(); ++i)
			if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
		return true;
	}
	inline std::string gns_identity(const std::string& id) { return "str:" + id; }

	// ---- join codes: 8 characters of ABCDEFGHJKMNPQRSTUVWXYZ23456789 (no 0/O/1/I/L), case-insensitive ----
	inline std::string join_code(const std::string& typed)
	{
		static const std::string alphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
		std::string out;
		for (const char c : typed)
		{
			if (c == ' ' || c == '-' || c == '\t' || c == '\r' || c == '\n') continue;
			const char u = char(std::toupper(static_cast<unsigned char>(c)));
			if (alphabet.find(u) == std::string::npos) return {};
			out += u;
		}
		return out.size() == 8 ? out : std::string();
	}

#define ONLINE_ROOM_HARDENING_CORE 1
	// ---- virtual addresses: broker order gives every PC the same stable, ungrindable assignment ----
	constexpr std::uint32_t virtual_net = 0x0a4d0000, virtual_mask = 0xffff0000;
	constexpr bool is_virtual(std::uint32_t host_order) { return (host_order & virtual_mask) == virtual_net; }
	constexpr std::uint32_t virtual_address(std::size_t slot) { return slot >= 1 && slot <= 0xffff ? virtual_net | std::uint32_t(slot) : 0; }
	class room_roster
	{
		struct entry { std::string id; std::uint32_t address; bool active = true; };
		std::vector<entry> entries_;
	public:
		bool reset(const std::vector<std::string>& ids)
		{
			if (ids.empty()) return false;
			room_roster next;
			for (const auto& id : ids) if (!next.join(id)) return false;
			*this = std::move(next);
			return true;
		}
		bool join(const std::string& id)
		{
			if (!valid_identity(id)) return false;
			for (auto& e : entries_)
				if (e.id == id)
				{
					if (!e.active && active_size() >= 4) return false;
					e.active = true;
					return true;
				}
			if (active_size() >= 4) return false;
			const auto address = virtual_address(entries_.size() + 1);
			if (!address) return false;
			entries_.push_back({id, address, true});
			return true;
		}
		void leave(const std::string& id) { for (auto& e : entries_) if (e.id == id) e.active = false; }
		void deactivate_all() { for (auto& e : entries_) e.active = false; }
		void clear() { entries_.clear(); }
		bool contains(const std::string& id) const { for (const auto& e : entries_) if (e.id == id && e.active) return true; return false; }
		std::uint32_t address(const std::string& id) const { for (const auto& e : entries_) if (e.id == id) return e.address; return 0; }
		std::size_t active_size() const { std::size_t n = 0; for (const auto& e : entries_) n += e.active; return n; }
		std::vector<std::string> active_ids() const
		{
			std::vector<std::string> out;
			for (const auto& e : entries_) if (e.active) out.push_back(e.id);
			return out;
		}
	};
	inline std::string dotted(std::uint32_t host_order)
	{
		char out[16];
		std::snprintf(out, sizeof(out), "%u.%u.%u.%u", host_order >> 24, (host_order >> 16) & 255, (host_order >> 8) & 255, host_order & 255);
		return out;
	}

	class token_bucket
	{
		double rate_, capacity_, tokens_;
		std::uint32_t at_ = 0;
		bool started_ = false;
	public:
		token_bucket(double per_second, double burst) : rate_(per_second), capacity_(burst), tokens_(burst) {}
		bool allow(std::uint32_t now, std::size_t cost = 1)
		{
			if (!started_) { started_ = true; at_ = now; }
			else
			{
				tokens_ += double(std::uint32_t(now - at_)) * rate_ / 1000.0;
				if (tokens_ > capacity_) tokens_ = capacity_;
				at_ = now;
			}
			if (double(cost) > tokens_) return false;
			tokens_ -= double(cost);
			return true;
		}
	};

	class broker_line_buffer
	{
		static constexpr std::size_t max_line = 16384, max_buffered = 65536;
		std::string buffer_;
	public:
		enum class result { need_more, ready, overflow };
		bool append(const std::string& data)
		{
			if (data.size() > max_buffered || buffer_.size() > max_buffered - data.size()) return false;
			buffer_ += data;
			return true;
		}
		result next(std::string& out)
		{
			const auto nl = buffer_.find('\n');
			if (nl == std::string::npos) return buffer_.size() >= max_line ? result::overflow : result::need_more;
			if (nl + 1 > max_line) return result::overflow;
			out = buffer_.substr(0, nl);
			buffer_.erase(0, nl + 1);
			return result::ready;
		}
	};

	inline bool bounded_append(std::string& buffer, const std::string& data, std::size_t cap)
	{
		if (data.size() > cap || buffer.size() > cap - data.size()) return false;
		buffer += data;
		return true;
	}

	inline bool bounded_printable_field(const std::uint8_t* data, std::size_t size, char (&out)[6])
	{
		if (!data || size < 5) return false;
		bool terminated = false;
		for (int i = 0; i < 5; ++i)
		{
			const auto c = data[i];
			if (terminated)
			{
				if (c) return false;
				out[i] = 0;
			}
			else if (!c) { terminated = true; out[i] = 0; }
			else if (c < 0x20 || c > 0x7e) return false;
			else out[i] = char(c);
		}
		out[5] = 0;
		return true;
	}

	inline bool confine_embedded_address(std::vector<std::uint8_t>& data, std::uint32_t sender, std::uint16_t port)
	{
		if (data.size() < 3 || data[2] != 0x18) return true;
		if (data.size() < 19 || !is_virtual(sender)) return false;
		data[13] = std::uint8_t(sender >> 24);
		data[14] = std::uint8_t(sender >> 16);
		data[15] = std::uint8_t(sender >> 8);
		data[16] = std::uint8_t(sender);
		data[17] = std::uint8_t(port);
		data[18] = std::uint8_t(port >> 8);
		return true;
	}

	struct room_metadata
	{
		std::string name, host_display_name, game_mode, zone, zone_name, version_token;
		int max_players = 4;
		bool public_room = false, joinable = true, in_progress = false;
	};
	inline std::string host_request(const room_metadata& metadata)
	{
		std::string out = "{\"op\":\"host\",\"metadata\":{";
		bool comma = false;
		auto string_field = [&](const char* key, const std::string& value)
		{
			if (value.empty()) return;
			if (comma) out += ',';
			out += quote(key) + ":" + quote(value);
			comma = true;
		};
		string_field("name", metadata.name);
		string_field("host_display_name", metadata.host_display_name);
		string_field("game_mode", metadata.game_mode);
		string_field("zone", metadata.zone);
		string_field("zone_name", metadata.zone_name);
		if (comma) out += ',';
		out += "\"max_players\":" + std::to_string(metadata.max_players);
		comma = true;
		out += ",\"public\":" + std::string(metadata.public_room ? "true" : "false");
		out += ",\"joinable\":" + std::string(metadata.joinable ? "true" : "false");
		out += ",\"in_progress\":" + std::string(metadata.in_progress ? "true" : "false");
		string_field("version_token", metadata.version_token);
		return out + "}}\n";
	}

	// ---- the certificate (SPEC.md 3.1): PEM-armoured CMsgSteamDatagramCertificateSigned ----
	// The base64 body between the BEGIN and END lines, decoded.
	inline bool pem_body(const std::string& pem, std::vector<std::uint8_t>& out)
	{
		std::string b64;
		bool inside = false;
		std::size_t i = 0;
		while (i < pem.size())
		{
			auto end = pem.find('\n', i);
			if (end == std::string::npos) end = pem.size();
			std::string row = pem.substr(i, end - i);
			while (!row.empty() && (row.back() == '\r' || row.back() == ' ')) row.pop_back();
			if (row.rfind("-----BEGIN", 0) == 0) inside = true;
			else if (row.rfind("-----END", 0) == 0) inside = false;
			else if (inside && !row.empty() && row[0] != '#') b64 += row;
			i = end + 1;
		}
		return !b64.empty() && unbase64(b64, out) && !out.empty();
	}
	// SetCertificate's blob: the signed certificate with private_key_data (field 1, bytes) appended - protobuf
	// fields may come in any order, so appending one to the serialized message is the message with that field set.
	inline std::vector<std::uint8_t> certificate_blob(const std::vector<std::uint8_t>& signed_cert, const std::uint8_t (&seed)[seed_size])
	{
		std::vector<std::uint8_t> out = signed_cert;
		out.push_back(0x0a);
		out.push_back(std::uint8_t(seed_size));
		out.insert(out.end(), seed, seed + seed_size);
		return out;
	}

	// ---- TURN / STUN (SPEC.md 3.5): "stun:host:3478", "turn:host:3478?transport=udp" -> host:port lists ----
	struct ice_servers { std::string stun, turn, users, passwords; };
	inline ice_servers ice(const std::vector<std::string>& uris, const std::string& username, const std::string& password)
	{
		ice_servers out;
		for (const auto& uri : uris)
		{
			const bool stun = uri.rfind("stun:", 0) == 0, turn = uri.rfind("turn:", 0) == 0;
			if (!stun && !turn) continue;
			std::string where = uri.substr(5);
			const auto q = where.find('?');
			// GNS takes host:port; the udp and tcp URIs of one server collapse into one entry.
			if (q != std::string::npos) where.resize(q);
			auto& list = stun ? out.stun : out.turn;
			if ((',' + list + ',').find(',' + where + ',') != std::string::npos) continue;
			if (!list.empty()) list += ',';
			list += where;
			if (turn)
			{
				if (!out.users.empty()) { out.users += ','; out.passwords += ','; }
				out.users += username;
				out.passwords += password;
			}
		}
		return out;
	}

	// ---- credentials kept between starts: the 24 h certificate and the 6 h TURN credentials (task 030) ----
	// String fields put in front of a broker line ("{...}"): a cached line says which broker it came from and, when it
	// is replayed, that it came from the cache.
	inline std::string with_fields(const std::string& row, const std::vector<std::pair<std::string, std::string>>& strings)
	{
		if (row.size() < 2 || row[0] != '{') return row;
		std::string head = "{";
		for (const auto& [k, v] : strings) head += quote(k) + ":" + quote(v) + ",";
		std::size_t i = 1;
		detail::space(row, i);
		if (i < row.size() && row[i] == '}') head.pop_back(); // "{}" takes no comma
		return head + row.substr(1);
	}
	// When TURN credentials stop working: the line's "expires", else the coturn REST username "<unix expiry>:<identity>".
	inline long long turn_expires(const line& msg)
	{
		if (const auto e = msg.number("expires", 0); e > 0) return e;
		const auto user = msg.text("username");
		const auto colon = user.find(':');
		if (colon == std::string::npos || colon == 0 || colon > 18) return 0;
		long long v = 0;
		for (std::size_t i = 0; i < colon; ++i)
		{
			if (user[i] < '0' || user[i] > '9') return 0;
			v = v * 10 + (user[i] - '0');
		}
		return v;
	}
	constexpr long long cert_reuse_margin = 3600; // a cached certificate is used while more than an hour is left
	constexpr long long turn_reuse_margin = 1800; // cached TURN credentials while more than 30 minutes are left
	enum class turn_cache { use, malformed, other_broker, rotated, expiring };
	// May a cached turn_ok line (saved with "cached_for": the broker address) be used instead of asking the broker?
	// turn_key is the broker's public id of its TURN secret (auth_ok "turn_key"; empty from a broker without one): a
	// rotated secret makes the cached password fail at the relay, so the line must carry the same id.
	inline turn_cache turn_cache_check(const line& cached, const std::string& broker, const std::string& turn_key, long long now)
	{
		if (!cached.valid || cached.text("op") != "turn_ok" || cached.text("username").empty() || cached.text("password").empty() || cached["uris"].items.empty())
			return turn_cache::malformed;
		if (cached.text("cached_for") != broker) return turn_cache::other_broker;
		if (cached.text("turn_key") != turn_key) return turn_cache::rotated;
		if (turn_expires(cached) <= now + turn_reuse_margin) return turn_cache::expiring;
		return turn_cache::use;
	}
	inline const char* describe(turn_cache c)
	{
		switch (c)
		{
		case turn_cache::use: return "valid";
		case turn_cache::malformed: return "unreadable";
		case turn_cache::other_broker: return "for another broker";
		case turn_cache::rotated: return "for the broker's previous TURN secret";
		case turn_cache::expiring: return "30 minutes or less from expiry";
		}
		return "?";
	}
	inline bool cert_cache_usable(const line& cached, long long now)
	{
		return cached.valid && cached.text("op") == "cert_ok" && cached.number("expires") > now + cert_reuse_margin;
	}
	// A room's members can reach the game's own message handling before admission, so a room starts only when the
	// receive check is installed; a joiner also loads the host's campaign image, so it needs the save core's checked
	// load ([Save] Format) as well. nullptr = allowed, else why not (logged and shown as the status message).
	inline const char* room_guard_refusal(bool hosting, bool receive_validation_installed, bool campaign_load_checked)
	{
		if (!receive_validation_installed)
			return "online rooms need the receive validation installed ([Online] ReceiveValidation on and the retail network code)";
		if (!hosting && !campaign_load_checked)
			return "joining a room needs the LC save format ([Save] Format) installed, which checks the host's campaign before the game loads it";
		return nullptr;
	}
}
