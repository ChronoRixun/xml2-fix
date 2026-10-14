#include "xmlb.hpp"

#include "value.hpp"

#include <cstring>
#include <algorithm>
#include <functional>
#include <map>

namespace lcsave::detail
{
	const std::string* XNode::get(const char* key) const
	{
		const std::string* found = nullptr;
		for (const auto& [k, v] : attrs)
		{
			if (k == key) found = &v;
		}
		return found;
	}

	namespace
	{
		constexpr std::uint32_t xmlb_magic = 0x11B1;
		constexpr std::uint32_t none = 0xFFFFFFFF;

		struct Dec
		{
			const std::uint8_t* d;
			std::size_t n;
			std::size_t budget;

			std::uint32_t u32(std::size_t off) const
			{
				if (off + 4 > n) fail("XMLB: offset outside the file");
				std::uint32_t v = 0;
				std::memcpy(&v, d + off, 4);
				return v;
			}
			std::string str(std::uint32_t off) const
			{
				if (off >= n) fail("XMLB: string outside the file");
				const void* end = std::memchr(d + off, 0, n - off);
				if (!end) fail("XMLB: string not terminated");
				return std::string(reinterpret_cast<const char*>(d + off), static_cast<const std::uint8_t*>(end) - (d + off));
			}
			std::uint32_t node(std::uint32_t off, XNode& el, int depth)
			{
				if (depth > 256 || budget-- == 0) fail("XMLB: node structure too deep or cyclic");
				el.tag = str(u32(off));
				const auto sib = u32(off + 4);
				auto child = u32(off + 8);
				const auto count = u32(off + 12);
				if (count > (n - off) / 8) fail("XMLB: attribute count beyond the file");
				for (std::uint32_t i = 0; i < count; ++i)
				{
					const auto k = u32(off + 16 + 8 * std::size_t(i));
					const auto v = u32(off + 20 + 8 * std::size_t(i));
					el.attrs.emplace_back(str(k), str(v));
				}
				while (child != none)
				{
					el.children.emplace_back();
					child = node(child, el.children.back(), depth + 1);
				}
				return sib;
			}
		};

		template <typename F> void iter(const XNode& e, const char* tag, F&& f)
		{
			if (e.tag == tag) f(e);
			for (const auto& c : e.children) iter(c, tag, f);
		}

		std::string lower(const std::string* s)
		{
			return s ? py_lower(*s, Enc::latin1) : std::string();
		}
	}

	XNode decode_xmlb(const std::uint8_t* data, std::size_t size)
	{
		Dec dec{data, size, size / 16 + 1};
		if (dec.u32(0) != xmlb_magic) fail("not an XMLB file");
		std::vector<XNode> roots;
		std::uint32_t off = 8;
		while (off != none)
		{
			roots.emplace_back();
			off = dec.node(off, roots.back(), 0);
		}
		if (roots.size() == 1) return std::move(roots[0]);
		XNode wrapper;
		wrapper.tag = "xmlb_multiple_roots";
		wrapper.children = std::move(roots);
		return wrapper;
	}

	void numbered_entities(const XNode& root, std::vector<Numbered>& out, std::vector<std::string>& problems)
	{
		static const char* const smart_true[] = {"physent", "actor", "affectableharment", "doorent", "guidedprojectileent", "inventoryent",
			"moverent", "powertriggerent", "projectileent", "tornadoent", "projectiletornadoent", "rememberent", "sentryent", "tileent", "lightent"};
		static const char* const smart_false[] = {"ent", "actionent", "gameent", "cameramagnetent", "enabletargetent", "scripttriggerent",
			"treasureent", "waterent", "zonelinkent", "playerstartent", "waypointent", "worldent"};
		// defs = {e.get('name'): e for e in root.iter('entity')}: the last definition of a name wins; None is a key too
		std::map<std::pair<bool, std::string>, const XNode*> defs;
		iter(root, "entity", [&](const XNode& e)
		{
			const auto* n = e.get("name");
			defs[{n != nullptr, n ? *n : std::string()}] = &e;
		});
		auto tb = [](const XNode& d, const char* k) { const auto* v = d.get(k); return v && py_lower(*v, Enc::latin1) == "true"; };
		auto repr_opt = [](const std::string* s) { return s ? py_repr(*s, Enc::latin1) : std::string("None"); };
		std::uint32_t k = 0;
		iter(root, "entinst", [&](const XNode& ei)
		{
			const auto* t = ei.get("type");
			const auto it = defs.find({t != nullptr, t ? *t : std::string()});
			std::vector<const std::string*> insts;
			iter(ei, "inst", [&](const XNode& i) { insts.push_back(i.get("name")); });
			if (it == defs.end())
			{
				problems.push_back("no entity definition for type " + repr_opt(t));
				k += static_cast<std::uint32_t>(insts.size());
				return;
			}
			const auto& d = *it->second;
			const auto cls = lower(d.get("classname"));
			const auto sm = lower(d.get("smartent"));
			bool stays_smart = true;
			bool known = false;
			if (cls == "monsterspawnerent")
			{
				stays_smart = tb(d, "monster_smartent") && tb(d, "instantspawn") && !tb(d, "checkteam");
				known = true;
			}
			for (const char* c : smart_true)
			{
				if (!known && cls == c)
				{
					stays_smart = sm != "false";
					known = true;
				}
			}
			for (const char* c : smart_false)
			{
				if (!known && cls == c)
				{
					stays_smart = sm == "true";
					known = true;
				}
			}
			if (!known)
			{
				problems.push_back("unknown entity class " + py_repr(cls, Enc::latin1) + " (type " + repr_opt(t) + ")");
				stays_smart = true;
			}
			for (const auto* n : insts)
			{
				if (!stays_smart)
				{
					Numbered e;
					e.has_type = t != nullptr;
					e.type = t ? *t : std::string();
					e.has_name = n != nullptr;
					e.name = n ? *n : std::string();
					e.placement = k;
					out.push_back(std::move(e));
				}
				++k;
			}
		});
	}

	Hash resident_prefix_digest(const XNode& root, const std::vector<Numbered>& numbered, const std::size_t count)
	{
		if (!count || count > numbered.size()) fail("zone-prefix.lcm: invalid prefix count");
		std::map<std::pair<bool, std::string>, const XNode*> definitions;
		iter(root, "entity", [&](const XNode& e)
		{
			const auto* name = e.get("name");
			if (!definitions.emplace(std::make_pair(name != nullptr, name ? *name : std::string()), &e).second)
				fail("zone-prefix.lcm: duplicate entity definition");
		});
		std::vector<std::pair<const XNode*, const XNode*>> placements;
		iter(root, "entinst", [&](const XNode& group)
		{
			iter(group, "inst", [&](const XNode& inst) { placements.emplace_back(&group, &inst); });
		});
		auto text = [](const std::string& value) { return to_json(Value::text(value, Enc::latin1)); };
		auto attributes = [&](const XNode& e)
		{
			// ElementTree keeps the last repeated attribute; order is by Unicode
			// code point, equivalent to unsigned byte order for XMLB's latin-1.
			std::map<std::string, std::string> attrs;
			for (const auto& [k, v] : e.attrs) attrs[k] = v;
			std::vector<std::pair<std::string, std::string>> sorted(attrs.begin(), attrs.end());
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
			{ return less_text(Value::text(a.first, Enc::latin1), Value::text(b.first, Enc::latin1)); });
			std::string out = "[";
			for (const auto& [k, v] : sorted)
			{
				if (out.size() > 1) out += ',';
				out += '[' + text(k) + ',' + text(v) + ']';
			}
			return out + ']';
		};
		std::function<std::string(const XNode&)> canonical = [&](const XNode& e)
		{
			std::string out = '[' + text(e.tag) + ',' + attributes(e) + ",[";
			bool first = true;
			for (const auto& child : e.children)
			{
				if (!first) out += ',';
				first = false;
				out += canonical(child);
			}
			return out + "]]";
		};
		std::string input = "[";
		for (std::size_t k = 0; k < count; ++k)
		{
			const auto& n = numbered[k];
			const auto found = definitions.find({n.has_type, n.type});
			if (found == definitions.end() || n.placement >= placements.size()) fail("zone-prefix.lcm: unmodelled numbering");
			const auto& [group, inst] = placements[n.placement];
			if (k) input += ',';
			input += '[' + canonical(*found->second) + ',' + attributes(*group) + ',' + canonical(*inst) + ']';
		}
		input += ']';
		return sha256(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
	}
}
