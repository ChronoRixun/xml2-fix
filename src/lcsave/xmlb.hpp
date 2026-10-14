#pragma once

// Raven binary XML (XMLB / ENGB, magic 0x11B1) as the builder's tools/xmlb.py decodes it, bounds-checked, and the
// placed-entity numbering rule of binds.numbered_entities (SPEC-old-save 1.13, CONVERTER-PORT section 3): the
// converter numbers a zone from the placement file the build has, exactly as the reference does.

#include "keys.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lcsave::detail
{
	struct XNode
	{
		std::string tag;                                       // latin-1 bytes
		std::vector<std::pair<std::string, std::string>> attrs;
		std::vector<XNode> children;
		const std::string* get(const char* key) const;          // ElementTree get: the last value set, or nullptr
	};

	// a file with several top-level elements is held in a node tagged "xmlb_multiple_roots" (xmlb.MULTI_ROOT)
	XNode decode_xmlb(const std::uint8_t* data, std::size_t size);

	struct Numbered
	{
		bool has_type = false;
		bool has_name = false;
		std::string type;
		std::string name;
		std::uint32_t placement = 0;
	};
	// binds.numbered_entities: the numbered placed entities in ordinal order, and the problems that make the zone's
	// numbering unmodellable
	void numbered_entities(const XNode& root, std::vector<Numbered>& out, std::vector<std::string>& problems);
	// SHA-256 of the Python attestation's compact ensure_ascii JSON resident prefix.
	Hash resident_prefix_digest(const XNode& root, const std::vector<Numbered>& numbered, std::size_t count);
}
