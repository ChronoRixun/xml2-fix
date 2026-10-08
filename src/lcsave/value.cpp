#include "value.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace lcsave::detail
{
	void fail(std::string message)
	{
		throw Error{std::move(message)};
	}

	Value Value::dict(std::initializer_list<std::pair<const char*, Value>> items)
	{
		Value x = dict();
		x.d.reserve(items.size());
		for (const auto& it : items)
		{
			x.set(it.first, it.second);
		}
		return x;
	}

	bool Value::truthy() const
	{
		switch (type)
		{
		case Type::none: return false;
		case Type::integer: return i != 0;
		case Type::real: return f != 0;
		case Type::text:
		case Type::bytes: return !s.empty();
		case Type::list: return !l.empty();
		case Type::dict: return !d.empty();
		}
		return false;
	}

	std::int64_t Value::as_int() const
	{
		if (type == Type::integer)
		{
			return i;
		}
		fail("internal: value is not an integer");
	}

	double Value::as_real() const
	{
		if (type == Type::real)
		{
			return f;
		}
		if (type == Type::integer)
		{
			return static_cast<double>(i);
		}
		fail("internal: value is not a number");
	}

	const std::string& Value::as_text() const
	{
		if (type == Type::text)
		{
			return s;
		}
		fail("internal: value is not text");
	}

	const std::string& Value::as_bytes() const
	{
		if (type == Type::bytes || type == Type::text)
		{
			return s;
		}
		fail("internal: value is not bytes");
	}

	const std::vector<Value>& Value::as_list() const
	{
		if (type == Type::list)
		{
			return l;
		}
		fail("internal: value is not a list");
	}

	std::vector<Value>& Value::as_list()
	{
		if (type == Type::list)
		{
			return l;
		}
		fail("internal: value is not a list");
	}

	namespace
	{
		bool key_is(const Value& k, const char* key)
		{
			return k.type == Value::Type::text && k.s == key;
		}
	}

	bool Value::has(const char* key) const
	{
		for (const auto& it : d)
		{
			if (key_is(it.key, key))
			{
				return true;
			}
		}
		return false;
	}

	const Value& Value::operator[](const char* key) const
	{
		for (const auto& it : d)
		{
			if (key_is(it.key, key))
			{
				return it.value;
			}
		}
		fail(std::string("missing field '") + key + "'");
	}

	Value& Value::set(const char* key, Value v)
	{
		for (auto& it : d)
		{
			if (key_is(it.key, key))
			{
				it.value = std::move(v);
				return it.value;
			}
		}
		d.push_back(Item{text(key), std::move(v)});
		return d.back().value;
	}

	Value& Value::set(Value key, Value v)
	{
		for (auto& it : d)
		{
			if (it.key == key)
			{
				it.value = std::move(v);
				return it.value;
			}
		}
		d.push_back(Item{std::move(key), std::move(v)});
		return d.back().value;
	}

	const Value* Value::find(const Value& key) const
	{
		for (const auto& it : d)
		{
			if (it.key == key)
			{
				return &it.value;
			}
		}
		return nullptr;
	}

	namespace
	{
		bool same_text(const std::string& a, Enc ea, const std::string& b, Enc eb)
		{
			if (a.size() != b.size())
			{
				return false;
			}
			if (ea == eb)
			{
				return a == b;
			}
			for (std::size_t k = 0; k < a.size(); ++k)
			{
				if (code_point(static_cast<std::uint8_t>(a[k]), ea) != code_point(static_cast<std::uint8_t>(b[k]), eb))
				{
					return false;
				}
			}
			return true;
		}
	}

	bool operator==(const Value& a, const Value& b)
	{
		using T = Value::Type;
		if ((a.type == T::integer || a.type == T::real) && (b.type == T::integer || b.type == T::real))
		{
			if (a.type == T::integer && b.type == T::integer)
			{
				return a.i == b.i;
			}
			return a.as_real() == b.as_real();
		}
		if (a.type != b.type)
		{
			return false;
		}
		switch (a.type)
		{
		case T::none: return true;
		case T::text: return same_text(a.s, a.enc, b.s, b.enc);
		case T::bytes: return a.s == b.s;
		case T::list:
			if (a.l.size() != b.l.size()) return false;
			for (std::size_t k = 0; k < a.l.size(); ++k)
			{
				if (a.l[k] != b.l[k]) return false;
			}
			return true;
		case T::dict:
			if (a.d.size() != b.d.size()) return false;
			for (const auto& it : a.d)
			{
				const auto* other = b.find(it.key);
				if (!other || *other != it.value) return false;
			}
			return true;
		default: return false;
		}
	}

	bool less_text(const Value& a, const Value& b)
	{
		const std::size_t n = std::min(a.s.size(), b.s.size());
		for (std::size_t k = 0; k < n; ++k)
		{
			const auto x = code_point(static_cast<std::uint8_t>(a.s[k]), a.enc);
			const auto y = code_point(static_cast<std::uint8_t>(b.s[k]), b.enc);
			if (x != y)
			{
				return x < y;
			}
		}
		return a.s.size() < b.s.size();
	}

	bool py_less(const Value& a, const Value& b)
	{
		using T = Value::Type;
		if ((a.type == T::integer || a.type == T::real) && (b.type == T::integer || b.type == T::real))
		{
			if (a.type == T::integer && b.type == T::integer)
			{
				return a.i < b.i;
			}
			return a.as_real() < b.as_real();
		}
		if (a.type == T::text && b.type == T::text)
		{
			return less_text(a, b);
		}
		fail("'<' not supported between these values (Python TypeError)");
	}

	double py_round3(double x)
	{
		if (!std::isfinite(x) || x == 0)
		{
			return x;
		}
		char buf[512];
		const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, 3);
		double out = 0;
		std::from_chars(buf, r.ptr, out);
		if (out == 0)
		{
			out = std::signbit(x) ? -0.0 : 0.0;
		}
		return out;
	}

	std::string py_float_repr(double x)
	{
		if (std::isnan(x)) return "NaN";
		if (std::isinf(x)) return x > 0 ? "Infinity" : "-Infinity";
		if (x == 0) return std::signbit(x) ? "-0.0" : "0.0";
		char buf[64];
		const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
		std::string sci(buf, r.ptr);
		std::string sign;
		if (sci[0] == '-')
		{
			sign = "-";
			sci.erase(0, 1);
		}
		const auto e = sci.find('e');
		std::string digits;
		for (std::size_t k = 0; k < e; ++k)
		{
			if (sci[k] != '.') digits.push_back(sci[k]);
		}
		const int exponent = std::atoi(sci.c_str() + e + 1);
		const int decpt = exponent + 1;
		const int n = static_cast<int>(digits.size());
		std::string out = sign;
		if (decpt <= -4 || decpt > 16)
		{
			out += digits.substr(0, 1);
			if (n > 1)
			{
				out += "." + digits.substr(1);
			}
			char eb[16];
			std::snprintf(eb, sizeof eb, "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
			out += eb;
		}
		else if (decpt <= 0)
		{
			out += "0." + std::string(static_cast<std::size_t>(-decpt), '0') + digits;
		}
		else if (decpt < n)
		{
			out += digits.substr(0, static_cast<std::size_t>(decpt)) + "." + digits.substr(static_cast<std::size_t>(decpt));
		}
		else
		{
			out += digits + std::string(static_cast<std::size_t>(decpt - n), '0') + ".0";
		}
		return out;
	}

	namespace
	{
		void json_text(std::string& out, const std::string& s, Enc enc)
		{
			static const char digits[] = "0123456789abcdef";
			out.push_back('"');
			for (const unsigned char b : s)
			{
				const auto cp = code_point(b, enc);
				if (cp == '"') out += "\\\"";
				else if (cp == '\\') out += "\\\\";
				else if (cp == '\n') out += "\\n";
				else if (cp == '\r') out += "\\r";
				else if (cp == '\t') out += "\\t";
				else if (cp == '\b') out += "\\b";
				else if (cp == '\f') out += "\\f";
				else if (cp >= 0x20 && cp <= 0x7e) out.push_back(static_cast<char>(cp));
				else
				{
					out += "\\u";
					out.push_back(digits[(cp >> 12) & 15]);
					out.push_back(digits[(cp >> 8) & 15]);
					out.push_back(digits[(cp >> 4) & 15]);
					out.push_back(digits[cp & 15]);
				}
			}
			out.push_back('"');
		}

		void json_key(std::string& out, const Value& k)
		{
			switch (k.type)
			{
			case Value::Type::text: json_text(out, k.s, k.enc); break;
			case Value::Type::none: out += "\"null\""; break;
			case Value::Type::integer: out += '"' + std::to_string(k.i) + '"'; break;
			case Value::Type::real: out += '"' + py_float_repr(k.f) + '"'; break;
			default: fail("internal: JSON key of an unsupported type");
			}
		}

		void json_value(std::string& out, const Value& v, int level)
		{
			switch (v.type)
			{
			case Value::Type::none: out += "null"; return;
			case Value::Type::integer: out += std::to_string(v.i); return;
			case Value::Type::real: out += py_float_repr(v.f); return;
			case Value::Type::text: json_text(out, v.s, v.enc); return;
			case Value::Type::bytes: fail("internal: bytes are not JSON serializable");
			case Value::Type::list:
				if (v.l.empty())
				{
					out += "[]";
					return;
				}
				out += "[";
				for (std::size_t k = 0; k < v.l.size(); ++k)
				{
					out += k ? ",\n" : "\n";
					out.append(static_cast<std::size_t>(level + 1), ' ');
					json_value(out, v.l[k], level + 1);
				}
				out += "\n";
				out.append(static_cast<std::size_t>(level), ' ');
				out += "]";
				return;
			case Value::Type::dict:
			{
				if (v.d.empty())
				{
					out += "{}";
					return;
				}
				std::vector<const Item*> items;
				for (const auto& it : v.d) items.push_back(&it);
				std::stable_sort(items.begin(), items.end(), [](const Item* a, const Item* b) { return py_less(a->key, b->key); });
				out += "{";
				for (std::size_t k = 0; k < items.size(); ++k)
				{
					out += k ? ",\n" : "\n";
					out.append(static_cast<std::size_t>(level + 1), ' ');
					json_key(out, items[k]->key);
					out += ": ";
					json_value(out, items[k]->value, level + 1);
				}
				out += "\n";
				out.append(static_cast<std::size_t>(level), ' ');
				out += "}";
				return;
			}
			}
		}
	}

	std::string to_json(const Value& v)
	{
		std::string out;
		json_value(out, v, 0);
		return out;
	}

	std::string py_repr_value(const Value& v)
	{
		switch (v.type)
		{
		case Value::Type::none: return "None";
		case Value::Type::integer: return std::to_string(v.i);
		case Value::Type::real: return py_float_repr(v.f).compare("NaN") == 0 ? "nan" : py_float_repr(v.f);
		case Value::Type::text: return py_repr(v.s, v.enc);
		case Value::Type::list:
		{
			std::string out = "[";
			for (std::size_t k = 0; k < v.l.size(); ++k)
			{
				if (k) out += ", ";
				out += py_repr_value(v.l[k]);
			}
			return out + "]";
		}
		default: return "?";
		}
	}
}
