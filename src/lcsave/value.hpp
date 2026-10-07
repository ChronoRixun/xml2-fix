#pragma once

// A dynamic value with the shapes the reference works with (None, int, float, str, bytes, list, dict), so that
// the record schemas of container.py, the converter's section data and the canonical dumps translate line for
// line. Dicts keep insertion order and overwrite on a repeated key (Python's dict); text keeps its bytes and
// how Python decoded them (Enc), which decides JSON escapes, sorting and lower-casing.

#include "keys.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace lcsave::detail
{
	struct Error
	{
		std::string message;
	};
	[[noreturn]] void fail(std::string message);

	struct Item;

	struct Value
	{
		enum class Type : std::uint8_t { none, integer, real, text, bytes, list, dict };
		Type type = Type::none;
		Enc enc = Enc::cp1252;
		std::int64_t i = 0;
		double f = 0;
		std::string s;                 // text or bytes
		std::vector<Value> l;
		std::vector<Item> d;

		Value() = default;
		static Value integer(std::int64_t v) { Value x; x.type = Type::integer; x.i = v; return x; }
		static Value real(double v) { Value x; x.type = Type::real; x.f = v; return x; }
		static Value text(std::string v, Enc e = Enc::cp1252) { Value x; x.type = Type::text; x.s = std::move(v); x.enc = e; return x; }
		static Value bytes(std::string v) { Value x; x.type = Type::bytes; x.s = std::move(v); return x; }
		static Value bytes(const std::uint8_t* p, std::size_t n) { return bytes(std::string(reinterpret_cast<const char*>(p), n)); }
		static Value list() { Value x; x.type = Type::list; return x; }
		static Value dict() { Value x; x.type = Type::dict; return x; }
		static Value dict(std::initializer_list<std::pair<const char*, Value>> items);

		bool is_none() const { return type == Type::none; }
		// Python truthiness
		bool truthy() const;
		std::int64_t as_int() const;
		double as_real() const;
		const std::string& as_text() const;
		const std::string& as_bytes() const;
		const std::vector<Value>& as_list() const;
		std::vector<Value>& as_list();

		// dict access by an ASCII key
		bool has(const char* key) const;
		const Value& operator[](const char* key) const;      // fails when missing (Python KeyError)
		Value& set(const char* key, Value v);                // insert or overwrite
		Value& set(Value key, Value v);
		const Value* find(const Value& key) const;
		Value& push(Value v) { l.push_back(std::move(v)); return l.back(); }
	};

	struct Item
	{
		Value key;
		Value value;
	};

	bool operator==(const Value& a, const Value& b);       // Python ==: int and float compare by value, text by character
	inline bool operator!=(const Value& a, const Value& b) { return !(a == b); }
	bool less_text(const Value& a, const Value& b);        // Python str < str (by code point)
	bool py_less(const Value& a, const Value& b);          // sorted() over the types the dumps sort

	// Python round(x, 3) and float repr
	double py_round3(double x);
	std::string py_float_repr(double x);
	// json.dumps(v, indent=1, sort_keys=True) (ensure_ascii)
	std::string to_json(const Value& v);
	// repr() of a list of text / ints as the converter's notes print them
	std::string py_repr_value(const Value& v);
}
