// The tables a conversion needs, read from the build: catalogue.lcc (catalogue.py), legacy.lcm (legacy.py's era
// model) and the .lcbind files (binds.py's numbering of the player's build), in the formats the builder writes
// (legends-classic tools/lcsave/lcfiles.py; docs/lcsave-formats.md), with the static verification of SPEC 3.5:
// every file's length and CRC, the SHA-1 of every data file a bind file describes, every bind's id against the
// catalogue's key, positions dense. The placed-entity numbering and the herostat's talent lists are read from the
// data files themselves (xmlb.hpp), as the reference does.

#include "internal.hpp"
#include "xmlb.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace lcsave::detail
{
	namespace
	{
		std::string narrow(const std::wstring& w)
		{
			std::string out;
			for (const wchar_t c : w) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
			return out;
		}

		std::wstring widen(const std::string& s)
		{
			std::wstring out;
			for (const unsigned char c : s) out.push_back(static_cast<wchar_t>(c));      // latin-1 file names
			return out;
		}

		std::string ascii_lower(std::string s)
		{
			for (auto& c : s) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
			return s;
		}

		// an attribute's value, lower case, the name matched in any case (the last one set wins); "" when absent
		std::string attribute_lower(const XNode& e, const char* name)
		{
			std::string out;
			for (const auto& [key, value] : e.attrs)
			{
				if (ascii_lower(key) == name) out = ascii_lower(value);
			}
			return out;
		}

		// a herostat entry's xpexempt attribute (any case of the name; "true" or "1", any case, as the builder writes it)
		bool xpexempt_value(const XNode& e)
		{
			const auto v = attribute_lower(e, "xpexempt");
			return v == "true" || v == "1";
		}

		struct In
		{
			const std::vector<std::uint8_t>& b;
			std::size_t p;
			std::string what;
			void need(std::size_t n) const
			{
				if (p + n > b.size()) fail(what + ": truncated at " + std::to_string(p));
			}
			template <typename T> T get()
			{
				need(sizeof(T));
				T v{};
				std::memcpy(&v, b.data() + p, sizeof v);
				p += sizeof v;
				return v;
			}
			std::string str()
			{
				const auto n = get<u16>();
				need(n);
				std::string v(reinterpret_cast<const char*>(b.data() + p), n);
				p += n;
				return v;
			}
			std::string take(std::size_t n)
			{
				need(n);
				std::string v(reinterpret_cast<const char*>(b.data() + p), n);
				p += n;
				return v;
			}
			bool end() const { return p >= b.size(); }
		};

		// magic, the header's u32 values, file length, CRC-32 of everything after the CRC field (lcfiles._with_header)
		In header(const std::vector<std::uint8_t>& b, const char* magic, std::vector<u32>& vals, std::size_t nvals, const std::string& what)
		{
			In in{b, 0, what};
			if (in.take(8) != std::string(magic, 8)) fail(what + ": bad magic");
			vals.clear();
			for (std::size_t k = 0; k < nvals; ++k) vals.push_back(in.get<u32>());
			const auto length = in.get<u32>();
			const auto crc = in.get<u32>();
			if (length != b.size()) fail(what + ": header length " + std::to_string(length) + " != file size " + std::to_string(b.size()));
			if (crc32(b.data() + in.p, b.size() - in.p) != crc) fail(what + ": CRC mismatch (damaged)");
			return in;
		}

		bool exists(const std::wstring& path)
		{
			const auto a = GetFileAttributesW(path.c_str());
			return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
		}

		struct Row
		{
			u32 group = 0;
			u32 position = 0;
			u32 id = 0;
			std::string key;
		};
		struct BindFile
		{
			std::map<std::string, u32> rules;
			std::vector<std::pair<std::string, std::vector<Row>>> lists;
			const std::vector<Row>& list(const char* tag) const
			{
				static const std::vector<Row> empty;
				for (const auto& l : lists) if (l.first == tag) return l.second;
				return empty;
			}
		};

		Hash hash_of_key(const std::string& canonical)
		{
			return sha256(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
		}

		// reads <bind_root>\Data\<rel>.lcbind, checks its SHA-1 against <data_root>\Data\<rel>.(engb|xmlb|XMLB) and every
		// row's id against the catalogue. false when the build has no such data file (and no bind file is expected).
		bool load_bind(const Build& build, const Catalogue& cat, const std::wstring& rel, BindFile& out, bool required)
		{
			const auto slash = rel.find_last_of(L'\\');
			const auto data = data_file(build.data_root + L"\\Data" + (slash == std::wstring::npos ? L"" : L"\\" + rel.substr(0, slash)),
				slash == std::wstring::npos ? rel : rel.substr(slash + 1));
			const auto what = "Data\\" + narrow(rel) + ".lcbind";
			if (data.empty())
			{
				if (required) fail("Data\\" + narrow(rel) + ": no .engb / .xmlb in the build");
				return false;
			}
			std::vector<std::uint8_t> bytes;
			if (!read_file_bytes(build.bind_root + L"\\Data\\" + rel + L".lcbind", bytes))
			{
				fail(what + ": missing (the build has Data\\" + narrow(rel) + "; its bind file must come with it, SPEC 3.4)");
			}
			std::vector<u32> vals;
			auto in = header(bytes, "LCBIND1\0", vals, 0, what);
			const auto sha = in.take(20);
			std::vector<std::uint8_t> raw;
			if (!read_file_bytes(data, raw)) fail("cannot read " + narrow(data));
			const auto h = sha1(raw.data(), raw.size());
			if (std::memcmp(h.data(), sha.data(), 20) != 0)
			{
				fail(what + ": made for another " + narrow(rel) + " (its SHA-1 " + hex(reinterpret_cast<const std::uint8_t*>(sha.data()), 20) +
					", the data file is " + hex(h) + ")");
			}
			BindFileRead record;
			for (const wchar_t c : rel) record.path.push_back(c == 92 ? '/' : static_cast<char>(c));
			record.path = "Data/" + record.path + ".lcbind";
			record.sha1 = sha;
			while (!in.end())
			{
				auto tag = in.take(4);
				const unsigned dom = domain_number(tag);
				if (!dom) fail(what + ": unknown domain tag " + py_repr(tag, Enc::latin1));
				const auto rule = in.get<u32>();
				if (rule != 1 && !(tag == "TRIV" && rule == 2)) fail(what + ": list " + tag + " has bind rule " + std::to_string(rule) + "; this XML2 Fix knows rule 1 and TRIV rule 2");
				if (!out.rules.emplace(tag, rule).second) fail(what + ": duplicate bind list " + tag);
				const auto n = in.get<u32>();
				std::vector<Row> rows;
				for (u32 k = 0; k < n; ++k)
				{
					Row r;
					r.group = in.get<u32>();
					r.position = in.get<u32>();
					r.id = in.get<u32>();
					r.key = in.str();
					if (!r.id) fail(what + ": unsupported ID-0/unknown keyref " + r.key);
					const u32 expect = r.key.empty() ? 0 : cat.lookup(dom, hash_of_key(r.key));
					if (expect != r.id)
					{
						char b[200];
						std::snprintf(b, sizeof b, ": %s group %u position %u carries id 0x%x, the catalogue gives its key 0x%x", tag.c_str(), r.group,
							r.position, r.id, expect);
						fail(what + b);
					}
					rows.push_back(std::move(r));
				}
				BindList bl;
				bl.tag = tag;
				bl.rule = rule;
				for (const auto& r : rows) bl.rows.push_back(BindRow{r.group, r.position, r.id, r.key});
				record.lists.push_back(std::move(bl));
				out.lists.emplace_back(std::move(tag), std::move(rows));
			}
			bool seen = false;
			for (const auto& f : build.bind_files) seen |= f.path == record.path;
			if (!seen) build.bind_files.push_back(std::move(record));
			return true;
		}

		void dense(const std::vector<Row>& rows, u32 first, const char* tag, const std::string& what)
		{
			for (std::size_t k = 0; k < rows.size(); ++k)
			{
				if (rows[k].position != first + k)
				{
					fail(what + ": " + tag + " positions are not dense from " + std::to_string(first) + " (row " + std::to_string(k) + ")");
				}
			}
		}

		template <typename V> void put_at(std::vector<V>& v, u32 position, V value, const std::string& what)
		{
			if (position > (1u << 20)) fail(what + ": position " + std::to_string(position) + " out of range");
			if (v.size() <= position) v.resize(position + 1);
			v[position] = value;
		}

		const char* release_of(int cv)
		{
			switch (cv)
			{
			case 5: return "0.1.3";
			case 6: return "0.1.4";
			case 7: return "0.1.5";
			case 8: return "0.1.6";
			case 9: return "0.1.7";
			case 11: return "0.1.8";
			case 12: return "0.1.9";
			case 13: return "0.1.10";
			default: return nullptr;
			}
		}
	}

	std::wstring data_file(const std::wstring& folder, const std::wstring& name)
	{
		for (const wchar_t* ext : {L".engb", L".xmlb", L".XMLB"})
		{
			const auto p = folder + L"\\" + name + ext;
			if (exists(p)) return p;
		}
		return {};
	}

	bool read_file_bytes(const std::wstring& path, std::vector<std::uint8_t>& out)
	{
		const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		LARGE_INTEGER size{};
		bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (1ll << 30);
		if (ok)
		{
			out.resize(static_cast<std::size_t>(size.QuadPart));
			DWORD got = 0;
			ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
		}
		CloseHandle(h);
		return ok;
	}

	std::string unescape_key(const std::string& canonical)
	{
		std::string out;
		for (std::size_t k = 0; k < canonical.size(); ++k)
		{
			if (canonical[k] == '%' && k + 2 < canonical.size() + 0 && canonical.compare(k, 3, "%2f") == 0)
			{
				out.push_back('/');
				k += 2;
			}
			else if (canonical[k] == '%' && canonical.compare(k, 3, "%25") == 0)
			{
				out.push_back('%');
				k += 2;
			}
			else
			{
				out.push_back(canonical[k]);
			}
		}
		return out;
	}

	// ------------------------------------------------------------------------------------------------ catalogue
	void Catalogue::index()
	{
		for (auto& m : by_hash) m.clear();
		by_id.clear();
		for (std::size_t k = 0; k < entries.size(); ++k)
		{
			const auto& e = entries[k];
			if (!e.key.empty()) by_hash[e.domain][hash_of_key(e.key)] = k;
			for (const auto& a : e.aliases) if (!a.empty()) by_hash[e.domain][hash_of_key(a)] = k;
			by_id[e.id] = k;
		}
	}

	u32 Catalogue::lookup(unsigned domain, const Hash& h) const
	{
		if (domain < 1 || domain > 16) return 0;
		const auto it = by_hash[domain].find(h);
		if (it == by_hash[domain].end()) return 0;
		const auto& e = entries[it->second];
		if (e.retired && e.successor) return e.successor;
		return e.id;
	}

	const CatEntry* Catalogue::entry_of(u32 id) const
	{
		const auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &entries[it->second];
	}

	bool read_catalogue(const std::vector<std::uint8_t>& b, Catalogue& out, std::string& error)
	{
		try
		{
			std::vector<u32> vals;
			auto in = header(b, "LCCAT2\0\0", vals, 3, "catalogue.lcc");
			out.revision = vals[0];
			out.content_version = vals[1];
			for (u32 d = 0; d < vals[2]; ++d)
			{
				const auto tag = in.take(4);
				const auto number = in.get<u8>();
				if (in.take(3) != std::string(3, '\0') || domain_number(tag) != number || !number)
				{
					fail("catalogue.lcc: bad domain header " + py_repr(tag, Enc::latin1) + " " + std::to_string(number));
				}
				const auto n = in.get<u32>();
				u32 last = 0;
				for (u32 k = 0; k < n; ++k)
				{
					CatEntry e;
					e.domain = number;
					e.id = in.get<u32>();
					e.attributes = in.get<u32>();
					e.retired = (in.get<u16>() & 1) != 0;
					e.successor = in.get<u32>();
					e.key = in.str();
					const auto na = in.get<u16>();
					for (u32 a = 0; a < na; ++a) e.aliases.push_back(in.str());
					if (id_domain(e.id) != number || e.id <= last) fail("catalogue.lcc: " + tag + " entries not ascending ids of the domain");
					last = e.id;
					out.entries.push_back(std::move(e));
				}
			}
			if (!in.end()) fail("catalogue.lcc: " + std::to_string(b.size() - in.p) + " trailing bytes");
			out.index();
			return true;
		}
		catch (const Error& e)
		{
			error = e.message;
			return false;
		}
	}

	// ------------------------------------------------------------------------------------------------ era model
	bool read_legacy(const std::vector<std::uint8_t>& b, TablesData& out, std::string& error)
	{
		try
		{
			std::vector<u32> vals;
			auto in = header(b, "LCLGM1\0\0", vals, 3, "legacy.lcm");
			out.legacy_map_id = vals[0];
			out.legacy_catalogue_revision = vals[1];
			out.eras.clear();
			const std::string what = "legacy.lcm";
			for (u32 k = 0; k < vals[2]; ++k)
			{
				EraTables e;
				e.cv = static_cast<int>(in.get<u32>());
				const char* rel = release_of(e.cv);
				e.release = rel ? rel : std::to_string(e.cv);
				const auto ntables = in.get<u32>();
				for (u32 t = 0; t < ntables; ++t)
				{
					const auto tag = in.take(4);
					const auto nrows = in.get<u32>();
					in.need(std::size_t(nrows) * 12);
					for (u32 r = 0; r < nrows; ++r)
					{
						const auto g = in.get<u32>();
						const auto p = in.get<u32>();
						const auto id = in.get<u32>();
						const std::string w = what + " " + tag;
						if (tag == "STAT") put_at(e.STAT, p, id, w);
						else if (tag == "ITEM")
						{
							put_at(e.ITEM, p, id, w);
							put_at(e.ITEM_cat, p, g, w);
						}
						else if (tag == "ENHC") put_at(e.ENHC, p, id, w);
						else if (tag == "NAMA") put_at(e.NAMA, p, id, w);
						else if (tag == "NAMB") put_at(e.NAMB[g], p, id, w);
						else if (tag == "TRIT") put_at(e.TRIT, p, id, w);
						else if (tag == "TALN")
						{
							if (g == 0) put_at(e.TALN_shared, p, id, w);
							else put_at(e.TALN_hero[g], p, id, w);
						}
						else if (tag == "COST") e.COST[g][p] = id;
						else if (tag == "MSNF") put_at(e.MSNF, p, id, w);
						else if (tag == "OBJV") put_at(e.OBJV, p, id, w);
						else if (tag == "DRCS") put_at(e.DRCS, p, id, w);
						else if (tag == "DRGR") put_at(e.DRGR, p, id, w);
						else if (tag == "REVW") e.REVW[{static_cast<int>(g), static_cast<int>(p)}] = id;
						else if (tag == "XPNT") put_at(e.XPNT, p, id, w);
						else if (tag == "TRIV") e.TRIV[{static_cast<int>(g), static_cast<int>(p)}] = id;
						else if (tag == "ZONE") put_at(e.ZONE, p, id, w);
						else if (tag == "IOWN")
						{
							if (g >= e.ITEM_own.size()) e.ITEM_own.resize(g + 1);
							put_at(e.ITEM_own[g], p, id, w);
						}
						else if (tag == "TOWN")
						{
							if (g >= e.TRIT_own.size()) e.TRIT_own.resize(g + 1);
							put_at(e.TRIT_own[g], p, id, w);
						}
						else fail(what + ": unknown table tag " + py_repr(tag, Enc::latin1));
					}
				}
				if (e.ITEM_own.size() < e.ITEM.size()) e.ITEM_own.resize(e.ITEM.size());
				if (e.TRIT_own.size() < e.TRIT.size()) e.TRIT_own.resize(e.TRIT.size());
				const auto nz = in.get<u32>();
				for (u32 z = 0; z < nz; ++z)
				{
					const auto zid = in.get<u32>();
					EraTables::ZoneDelta d;
					const auto m = in.get<u32>();
					in.need(std::size_t(m) * 4);
					for (u32 j = 0; j < m; ++j) d.map.push_back(in.get<u32>());
					const auto a = in.get<u32>();
					in.need(std::size_t(a) * 4);
					for (u32 j = 0; j < a; ++j) d.ambiguous.push_back(in.get<u32>());
					e.zones[zid] = std::move(d);
				}
				const auto nh = in.get<u32>();
				for (u32 h = 0; h < nh; ++h)
				{
					const auto sid = in.get<u32>();
					const auto m = in.get<u32>();
					in.need(std::size_t(m) * 4);
					auto& l = e.holdable[sid];
					for (u32 j = 0; j < m; ++j) l.push_back(in.get<u32>());
				}
				const auto digest = in.str();
				e.has_digest = !digest.empty();
				if (e.has_digest && !from_hex(digest, e.conversation_digest.data(), 32)) fail(what + ": conversation digest is not 64 hex characters");
				e.hero_count = in.get<u32>();
				for (auto* r : {e.prefix, e.suffix, e.trit, e.own})
				{
					r[0] = in.get<u32>();
					r[1] = in.get<u32>();
				}
				out.eras.push_back(std::move(e));
			}
			out.check_heroes = in.get<u32>();
			out.check_courses = in.get<u32>();
			out.check_grades = in.get<u32>();
			out.check_objectives = in.get<u32>();
			if (!in.end()) fail(what + ": " + std::to_string(b.size() - in.p) + " trailing bytes");
			std::stable_sort(out.eras.begin(), out.eras.end(), [](const EraTables& a, const EraTables& c) { return a.cv > c.cv; });
			const auto* ref = out.by_cv(static_cast<int>(out.legacy_map_id));
			if (!ref) fail(what + ": no era block for the map id (reference content version) " + std::to_string(out.legacy_map_id));
			out.reference = *ref;
			return true;
		}
		catch (const Error& e)
		{
			error = e.message;
			return false;
		}
	}

	// ------------------------------------------------------------------------------------------------ binds
	bool read_binds(const std::wstring& bind_root, const std::wstring& data_root, TablesData& t, std::string& error)
	{
		try
		{
			auto build = std::make_unique<Build>();
			auto& B = *build;
			B.bind_root = bind_root;
			B.data_root = data_root;
			const auto& cat = t.catalogue;
			auto& T = B.T;
			T.cv = static_cast<int>(cat.content_version);
			T.release = "the running build";
			// ---- stats and costumes
			BindFile hs, ns;
			load_bind(B, cat, L"herostat", hs, true);
			load_bind(B, cat, L"npcstat", ns, true);
			const auto& sh = hs.list("STAT");
			const auto& sn = ns.list("STAT");
			dense(sh, 1, "STAT", "Data\\herostat.lcbind");
			dense(sn, static_cast<u32>(1 + sh.size()), "STAT", "Data\\npcstat.lcbind");
			B.stat.push_back(std::string());
			T.STAT.push_back(0);
			for (const auto* l : {&sh, &sn})
			{
				for (const auto& r : *l)
				{
					B.stat.push_back(r.key);
					T.STAT.push_back(r.id);
					if (r.id) t.p_stat[r.id] = r.position;
				}
			}
			B.hero_count = static_cast<u32>(sh.size());
			T.hero_count = B.hero_count;
			for (const auto& r : hs.list("COST"))
			{
				T.COST[r.group][r.position] = r.id;
				if (r.id) t.p_cost[r.id] = r.position;
			}
			// ---- talents
			BindFile st;
			load_bind(B, cat, L"shared_talents", st, true);
			const auto& stl = st.list("TALN");
			dense(stl, 0, "TALN", "Data\\shared_talents.lcbind");
			std::map<std::string, u16> shared_by_key;
			for (const auto& r : stl)
			{
				T.TALN_shared.push_back(r.id);
				if (r.id) t.p_tal[r.id] = r.position;
				shared_by_key.emplace(r.key, static_cast<u16>(r.position));
			}
			std::map<std::pair<u32, std::string>, u16> hero_by_key;
			{
				std::set<std::string> seen;
				for (u32 i = 1; i <= B.hero_count; ++i)
				{
					const auto name = unescape_key(B.stat[i]);
					if (name.empty() || !seen.insert(name).second) continue;
					BindFile tf;
					if (!load_bind(B, cat, L"talents\\" + widen(name), tf, false)) continue;
					const auto& l = tf.list("TALN");
					dense(l, 0, "TALN", "Data\\talents\\" + name + ".lcbind");
					for (const auto& r : l)
					{
						put_at(T.TALN_hero[r.group], r.position, r.id, "TALN");
						const u32 engine_id = (r.group + 1) * 100 + r.position;
						if (r.id) t.p_tal[r.id] = engine_id;
						hero_by_key.emplace(std::make_pair(r.group, r.key), static_cast<u16>(engine_id));
					}
				}
			}
			// ---- items
			BindFile it;
			load_bind(B, cat, L"items", it, true);
			const std::string iw = "Data\\items.lcbind";
			const auto& items = it.list("ITEM");
			dense(items, 0, "ITEM", iw);
			std::map<std::string, u32> item_by_key;
			for (const auto& r : items)
			{
				B.item.push_back(r.key);
				T.ITEM.push_back(r.id);
				T.ITEM_cat.push_back(r.group);
				if (r.id) t.p_item[r.id] = r.position;
				item_by_key.emplace(r.key, r.position);
			}
			const auto& trit = it.list("TRIT");
			dense(trit, 0, "TRIT", iw);
			std::map<std::string, u32> trit_by_key;
			for (const auto& r : trit)
			{
				T.TRIT.push_back(r.id);
				if (r.id) t.p_trit[r.id] = r.position;
				trit_by_key.emplace(r.key, r.position);
			}
			// enhancement records: the kind and owner from the canonical key (p/..., s/..., t/<tag>/e<n>, i/<item>/e<n>)
			const auto& enh = it.list("ENHC");
			dense(enh, 0, "ENHC", iw);
			T.ITEM_own.assign(T.ITEM.size(), {});
			T.TRIT_own.assign(T.TRIT.size(), {});
			u32 counts[4] = {};
			int last_kind = 0;
			for (const auto& r : enh)
			{
				T.ENHC.push_back(r.id);
				if (r.id) t.p_enh[r.id] = r.position;
				const auto slash = r.key.find('/');
				const auto kind_s = r.key.substr(0, slash);
				const int kind = kind_s == "p" ? 0 : kind_s == "s" ? 1 : kind_s == "t" ? 2 : kind_s == "i" ? 3 : -1;
				if (kind < 0 || kind < last_kind) fail(iw + ": ENHC records are not prefixes, suffixes, special items' and items' own in that order");
				last_kind = kind;
				++counts[kind];
				if (kind >= 2)
				{
					const auto rest = r.key.substr(slash + 1);
					const auto owner = rest.substr(0, rest.find('/'));
					const auto& by = kind == 2 ? trit_by_key : item_by_key;
					const auto f = by.find(owner);
					if (f == by.end()) fail(iw + ": ENHC record " + std::to_string(r.position) + " names an owner the file does not have");
					(kind == 2 ? T.TRIT_own : T.ITEM_own)[f->second].push_back(r.id);
				}
			}
			T.prefix[0] = 0;
			T.prefix[1] = counts[0];
			T.suffix[0] = counts[0];
			T.suffix[1] = counts[0] + counts[1];
			T.trit[0] = T.suffix[1];
			T.trit[1] = T.suffix[1] + counts[2];
			T.own[0] = T.trit[1];
			T.own[1] = T.trit[1] + counts[3];
			const auto& nama = it.list("NAMA");
			dense(nama, 0, "NAMA", iw);
			for (const auto& r : nama)
			{
				T.NAMA.push_back(r.id);
				if (r.id) t.p_nama[r.id] = r.position;
			}
			for (const auto& r : it.list("NAMB"))
			{
				put_at(T.NAMB[r.group], r.position, r.id, "NAMB");
				if (r.id) t.p_namb[r.id] = r.position;
			}
			// ---- missions and objectives (rows at their global objective index)
			BindFile ms;
			load_bind(B, cat, L"missions\\missions", ms, true);
			const auto& msnf = ms.list("MSNF");
			dense(msnf, 0, "MSNF", "Data\\missions\\missions.lcbind");
			std::set<std::string> done;
			for (const auto& r : msnf)
			{
				T.MSNF.push_back(r.id);
				const auto file = unescape_key(r.key);
				if (!done.insert(file).second) continue;
				BindFile of;
				load_bind(B, cat, L"missions\\" + widen(file), of, true);
				for (const auto& o : of.list("OBJV"))
				{
					put_at(T.OBJV, o.position, o.id, "OBJV");
					if (o.id) t.p_obj[o.id] = o.position;
				}
			}
			// ---- Danger Room
			BindFile dr;
			load_bind(B, cat, L"dangerroom", dr, true);
			const auto& grades = dr.list("DRGR");
			const auto& courses = dr.list("DRCS");
			dense(grades, 0, "DRGR", "Data\\dangerroom.lcbind");
			dense(courses, 0, "DRCS", "Data\\dangerroom.lcbind");
			for (const auto& r : grades)
			{
				T.DRGR.push_back(r.id);
				if (r.id) t.p_grade[r.id] = r.position;
			}
			for (const auto& r : courses)
			{
				T.DRCS.push_back(r.id);
				B.course.push_back(r.key);
				if (r.id) t.p_course[r.id] = r.position;
			}
			// ---- Review, town centres, trivia
			BindFile rv, xp, tv;
			load_bind(B, cat, L"review_paths", rv, true);
			for (const auto& r : rv.list("REVW"))
			{
				T.REVW[{static_cast<int>(r.group), static_cast<int>(r.position)}] = r.id;
				if (r.id) t.p_review[r.id] = {static_cast<int>(r.group), static_cast<int>(r.position)};
			}
			load_bind(B, cat, L"xtraction_points", xp, true);
			const auto& xl = xp.list("XPNT");
			dense(xl, 0, "XPNT", "Data\\xtraction_points.lcbind");
			for (const auto& r : xl)
			{
				T.XPNT.push_back(r.id);
				if (r.id) t.p_xp[r.id] = r.position;
			}
			load_bind(B, cat, L"trivia", tv, true);
			T.trivia_compact = tv.rules["TRIV"] == 2;
			for (const auto& r : tv.list("TRIV"))
			{
				if (r.group < 1 || r.group > 0x7fffffff || r.position >= 50) fail("invalid trivia act/question position");
				if (!T.TRIV.emplace(std::pair{static_cast<int>(r.group), static_cast<int>(r.position)}, r.id).second) fail("duplicate trivia act/question position");
				if (!t.p_triv.emplace(r.id, std::pair{static_cast<int>(r.group), static_cast<int>(r.position)}).second) fail("duplicate trivia identity in build");
			}
			if (T.trivia_compact && T.TRIV.size() > 250) fail("native trivia budget exceeded (250)");
			int trivia_index = 0;
			for (const auto& [position, id] : T.TRIV)
			{
				const auto bit = T.trivia_compact ? trivia_index++ : (static_cast<std::int64_t>(position.first) - 1) * 50 + position.second;
				T.trivia_bits[position] = bit < 250 ? static_cast<int>(bit) : -1;
			}
			// ---- zones: the catalogue's zone keys (the writing build's zoneinfo)
			for (const auto& e : cat.entries)
			{
				if (e.domain == 15 && !e.key.empty()) t.p_zone[e.id] = unescape_key(e.key);
			}
			// ---- the talents each herostat entry lists (for missing_talents): resolved like the engine ids above
			{
				std::vector<std::uint8_t> raw;
				const auto path = data_file(data_root + L"\\Data", L"herostat");
				if (!read_file_bytes(path, raw)) fail("cannot read Data\\herostat");
				const auto root = decode_xmlb(raw.data(), raw.size());
				B.herostat_talents.assign(B.stat.size(), {});
				B.herostat_xpexempt.assign(B.stat.size(), 0);
				B.herostat_autospend.assign(B.stat.size(), std::string());
				u32 si = 0;
				for (const auto& e : root.children)
				{
					if (e.tag != "stats") continue;
					++si;
					if (si < B.herostat_xpexempt.size()) B.herostat_xpexempt[si] = xpexempt_value(e) ? 1 : 0;
					if (si < B.herostat_autospend.size()) B.herostat_autospend[si] = attribute_lower(e, "autospend");
					const auto* name = e.get("name");
					if (si >= B.herostat_talents.size() || !name) continue;
					for (const auto& c : e.children)
					{
						const auto* tn = c.tag == "talent" ? c.get("name") : nullptr;
						if (!tn) continue;
						const auto hk = canonical_key({*name, *tn}, Enc::latin1);
						const auto f = hero_by_key.find({si, hk});
						if (f != hero_by_key.end())
						{
							B.herostat_talents[si].push_back(f->second);
							continue;
						}
						const auto g = shared_by_key.find(canonical_key({"shared", *tn}, Enc::latin1));
						if (g != shared_by_key.end()) B.herostat_talents[si].push_back(g->second);
					}
				}
			}
			// ---- the name index: every catalogue key the build names, unescaped as the dumps print it
			for (const auto& e : cat.entries)
			{
				if (!e.key.empty()) t.names[e.id] = unescape_key(e.key);
			}
			t.build = std::move(build);
			return true;
		}
		catch (const Error& e)
		{
			error = e.message;
			return false;
		}
	}

	void read_conversation_tables(const std::vector<std::uint8_t>& bytes, TablesData& t)
	{
		std::vector<u32> vals;
		auto in = header(bytes, "LCCONV1\0", vals, 2, "conversation.lcm");
		if (vals[0] != 1) fail("conversation.lcm: unsupported version");
		for (u32 k = 0; k < vals[1]; ++k)
		{
			Hash digest{}, zone{};
			const auto d = in.take(32), z = in.take(32);
			std::memcpy(digest.data(), d.data(), 32); std::memcpy(zone.data(), z.data(), 32);
			const auto n = in.get<u32>();
			if (n > 128 || n % 4) fail("conversation.lcm: invalid bit count");
			std::vector<Hash> bits;
			std::set<Hash> seen;
			for (u32 j = 0; j < n; ++j)
			{
				Hash identity{}; const auto h = in.take(32); std::memcpy(identity.data(), h.data(), 32);
				if (!seen.insert(identity).second) fail("conversation.lcm: duplicate identity or table");
				bits.push_back(identity);
			}
			if (!t.conversation_tables.emplace(std::make_pair(digest, zone), std::move(bits)).second)
				fail("conversation.lcm: duplicate identity or table");
		}
		if (!in.end()) fail("conversation.lcm: trailing bytes");
	}

	// ------------------------------------------------------------------------------------------------ zones
	void read_zone_prefixes(const std::wstring& root, Build& build)
	{
		const auto path = root + L"\\Data\\lcsave\\zone-prefix.lcm";
		if (!exists(path)) return;
		std::vector<u8> raw;
		if (!read_file_bytes(path, raw)) fail("zone-prefix.lcm: cannot read");
		auto bad = [] { fail("zone-prefix.lcm: invalid framing or entry"); };
		if (raw.size() < 24 || std::memcmp(raw.data(), "LCZPFX1\0", 8)) bad();
		In in{raw, 8, "zone-prefix.lcm"};
		const auto version = in.get<u32>(), count = in.get<u32>(), length = in.get<u32>(), crc = in.get<u32>();
		if (version != 2 || count > 4096 || length != raw.size() || crc32(raw.data()+24, raw.size()-24) != crc) bad();
		std::set<std::tuple<Hash, Sha1, Sha1>> seen;
		for (u32 k = 0; k < count; ++k)
		{
			if (raw.size()-in.p < 108) bad();
			ZonePrefix prefix;
			auto take = [&](auto& value) { const auto bytes = in.take(value.size()); std::memcpy(value.data(), bytes.data(), value.size()); };
			take(prefix.zone); take(prefix.old); take(prefix.current); take(prefix.digest);
			const auto n = in.get<u32>();
			if (!n || n > 4096 || raw.size()-in.p < 4*n || prefix.old == prefix.current || !seen.emplace(prefix.zone,prefix.old,prefix.current).second) bad();
			std::set<u32> placements;
			for (u32 j = 0; j < n; ++j)
			{
				const auto p = in.get<u32>();
				if (!placements.insert(p).second) bad();
				prefix.placements.push_back(p);
			}
			std::set<std::pair<Hash,Hash>> aliases;
			for (auto* list : {&prefix.fresh_aliases,&prefix.ambiguous_aliases})
			{
				if (raw.size()-in.p < 4) bad();
				const auto size = in.get<u32>();
				if (size > 4096 || raw.size()-in.p < 64*size) bad();
				for (u32 j=0;j<size;++j)
				{
					std::pair<Hash,Hash> pair; take(pair.first); take(pair.second);
					if (!aliases.insert(pair).second) bad();
					list->push_back(pair);
				}
			}
			build.zone_prefixes.push_back(std::move(prefix));
		}
		if (!in.end()) bad();
	}

	const ZonePrefix* Build::zone_prefix(const std::string& zone, const std::string& old, const ZoneNumbering& current) const
	{
		for (const auto& p : zone_prefixes)
			if (p.zone == key_hash1(zone, Enc::latin1) && old == std::string(reinterpret_cast<const char*>(p.old.data()),20) &&
			    p.current == current.sha1 && p.placements.size() < current.names.size()) return &p;
		return nullptr;
	}

	const ZoneNumbering* Build::zone_numbering(const std::string& zone_in) const
	{
		const auto zone = py_lower(zone_in, Enc::cp1252);
		const auto found = zones.find(zone);
		if (found != zones.end()) return found->second ? &*found->second : nullptr;
		auto& slot = zones[zone];
		std::wstring rel;
		for (const char c : zone) rel.push_back(c == '/' ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
		const auto slash = rel.find_last_of(L'\\');
		const auto path = data_file(data_root + L"\\Maps" + (slash == std::wstring::npos ? L"" : L"\\" + rel.substr(0, slash)),
			slash == std::wstring::npos ? rel : rel.substr(slash + 1));
		if (path.empty()) return nullptr;              // binds.zone_numbering: None when the build has no placement file
		std::vector<std::uint8_t> raw;
		if (!read_file_bytes(path, raw)) fail("cannot read the placement file of " + py_repr(zone, Enc::cp1252));
		ZoneNumbering z;
		z.sha1 = sha1(raw.data(), raw.size());
		std::vector<Numbered> num;
		numbered_entities(decode_xmlb(raw.data(), raw.size()), num, z.problems);
		for (const auto& n : num)
		{
			z.names.emplace_back(key_hash1(n.type, Enc::latin1), key_hash1(n.name, Enc::latin1));
			z.name_text.emplace_back(n.type,n.name);
			z.placement.push_back(n.placement);
		}
		slot = std::move(z);
		return &*slot;
	}

	// ------------------------------------------------------------------------------------------------ helpers
	const std::vector<u32>* EraTables::taln_hero(u32 stats_index) const
	{
		const auto it = TALN_hero.find(stats_index);
		return it == TALN_hero.end() ? nullptr : &it->second;
	}

	const std::map<u32, u32>* EraTables::cost(u32 stats_index) const
	{
		const auto it = COST.find(stats_index);
		return it == COST.end() ? nullptr : &it->second;
	}

	const EraTables::ZoneDelta* EraTables::zone_map(u32 zone) const
	{
		const auto it = zones.find(zone);
		return it == zones.end() ? nullptr : &it->second;
	}

	const EraTables* TablesData::by_cv(int cv) const
	{
		for (const auto& e : eras) if (e.cv == cv) return &e;
		return nullptr;
	}

	const std::string* TablesData::name_of(u32 id) const
	{
		const auto it = names.find(id);
		return it == names.end() ? nullptr : &it->second;
	}
}

namespace lcsave
{
	bool load_tables(const std::wstring& data_dir, const std::wstring& bind_dir, Tables& tables, std::string& error)
	{
		using namespace detail;
		tables.data.reset();
		try
		{
			auto t = std::make_shared<TablesData>();
			std::vector<std::uint8_t> bytes;
			const auto lc = bind_dir + L"\\Data\\lcsave\\";
			if (!read_file_bytes(lc + L"catalogue.lcc", bytes))
			{
				error = "Data\\lcsave\\catalogue.lcc: missing or unreadable";
				return false;
			}
			if (!read_catalogue(bytes, t->catalogue, error)) return false;
			if (!read_file_bytes(lc + L"legacy.lcm", bytes))
			{
				error = "Data\\lcsave\\legacy.lcm: missing or unreadable";
				return false;
			}
			if (!read_legacy(bytes, *t, error)) return false;
			const auto conversation_path = lc + L"conversation.lcm";
			if (GetFileAttributesW(conversation_path.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				if (!read_file_bytes(conversation_path, bytes)) fail("conversation.lcm: unreadable");
				read_conversation_tables(bytes, *t);
			}
			if (t->legacy_catalogue_revision != t->catalogue.revision)
			{
				error = "legacy.lcm was made against catalogue revision " + std::to_string(t->legacy_catalogue_revision) +
					", catalogue.lcc is revision " + std::to_string(t->catalogue.revision);
				return false;
			}
			if (!read_binds(bind_dir, data_dir, *t, error)) return false;
			read_zone_prefixes(bind_dir, *t->build);
			tables.data = std::move(t);
			return true;
		}
		catch (const Error& e)
		{
			error = e.message;
		}
		catch (const std::exception& e)
		{
			error = std::string("internal error: ") + e.what();
		}
		return false;
	}

	bool load_tables(const std::wstring& build_dir, Tables& tables, std::string& error)
	{
		return load_tables(build_dir, build_dir, tables, error);
	}

	std::uint32_t Tables::catalogue_revision() const { return data ? data->catalogue.revision : 0; }
	std::uint32_t Tables::content_version() const { return data ? static_cast<std::uint32_t>(data->reference.cv) : 0; }
	bool Tables::xp_exempt(const std::uint32_t stats_index) const
	{
		return data && data->build && stats_index < data->build->herostat_xpexempt.size() && data->build->herostat_xpexempt[stats_index] != 0;
	}
	std::string Tables::hero_autospend(const std::uint32_t stats_index) const
	{
		return data && data->build && stats_index < data->build->herostat_autospend.size() ? data->build->herostat_autospend[stats_index] : std::string();
	}

	bool read_autospend(const std::wstring& build_dir, std::vector<AutospendClass>& out, std::string& error)
	{
		out.clear();
		try
		{
			std::vector<std::uint8_t> raw;
			const auto path = detail::data_file(build_dir + L"\\Data", L"autospend");
			if (path.empty() || !detail::read_file_bytes(path, raw))
			{
				error = "cannot read Data\\autospend";
				return false;
			}
			const auto root = detail::decode_xmlb(raw.data(), raw.size());
			for (const auto& c : root.children)
			{
				if (c.tag != "CharacterClass") continue;
				if (out.size() >= autospend_class_limit) break;                    // 0x43d18d: the parser keeps four classes
				AutospendClass cls;
				cls.name = detail::attribute_lower(c, "name");
				for (const auto& b : c.children)
				{
					if (detail::ascii_lower(b.tag) != "bonus") continue;
					if (cls.rules.size() >= autospend_rule_limit) break;           // 0x43d373: four rules a class
					AutospendRule r;
					const auto num = [&](const char* name) { return std::atoi(detail::attribute_lower(b, name).c_str()); };
					r.start = static_cast<std::uint8_t>(num("start"));
					r.rate = static_cast<std::uint8_t>(num("rate"));
					const char* order[4] = {"strength", "speed", "body", "mind"};
					for (int k = 0; k < 4; ++k) r.delta[k] = static_cast<std::int8_t>(num(order[k]));
					cls.rules.push_back(r);
				}
				out.push_back(std::move(cls));
			}
			return true;
		}
		catch (const detail::Error& e)
		{
			error = "Data\\autospend: " + e.message;
			return false;
		}
	}

	std::string Tables::stats_key(const std::uint32_t stats_index) const
	{
		return data && data->build && stats_index < data->build->stat.size() ? data->build->stat[stats_index] : std::string();
	}
	std::uint32_t Tables::hero_count() const { return data ? data->build->hero_count : 0; }
	std::uint32_t Tables::stats_count() const { return data ? static_cast<std::uint32_t>(data->build->stat.size()) : 0; }
	std::uint32_t Tables::item_count() const { return data ? static_cast<std::uint32_t>(data->build->item.size()) : 0; }
	std::uint32_t Tables::course_count() const { return data ? static_cast<std::uint32_t>(data->build->course.size()) : 0; }
	std::uint32_t Tables::objective_count() const { return data ? static_cast<std::uint32_t>(data->build->T.OBJV.size()) : 0; }
}
