#pragma once
namespace guard_fixture
{
		using namespace save_format_rules;

		std::vector<std::uint8_t> le16(const std::uint16_t v) { return {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)}; }
		std::vector<std::uint8_t> le32(const std::uint32_t v) { std::vector<std::uint8_t> o; put32(o, v); return o; }
		void append(std::vector<std::uint8_t>& to, const std::vector<std::uint8_t>& what) { to.insert(to.end(), what.begin(), what.end()); }
		void append(std::vector<std::uint8_t>& to, const std::string& text) { to.insert(to.end(), text.begin(), text.end()); }
		std::vector<std::uint8_t> str(const std::string& text) { auto o = le16(static_cast<std::uint16_t>(text.size())); append(o, text); return o; }

		struct section
		{
			std::string tag;
			std::uint8_t major = 1, minor = 0;
			std::uint16_t flags = section_required;
			std::vector<std::uint8_t> payload;
		};

		// A META payload: rsize, saved at, description, origin, zone, act, difficulty, play time, writer, slot; mods list.
		std::vector<std::uint8_t> meta(const std::string& description, const std::uint8_t slot, const float play_time)
		{
			std::vector<std::uint8_t> head;
			append(head, le32(0x12345678)); append(head, le32(0));  // saved at
			append(head, str(description));
			head.push_back(1);                                     // origin: converted
			append(head, le32(0x0f000001));                        // zone
			append(head, le16(2));                                 // act
			append(head, le32(1));                                 // difficulty
			std::uint32_t bits = 0; std::memcpy(&bits, &play_time, 4); append(head, le32(bits));
			append(head, str("test writer"));
			head.push_back(slot);
			std::vector<std::uint8_t> out = le32(static_cast<std::uint32_t>(head.size()));
			append(out, head);
			append(out, le32(0)); // mods: none
			return out;
		}

		std::vector<std::uint8_t> empty_section()
		{
			return le32(0); // a head with no fields
		}

		// A container of `kind` with the sections given, CRCs computed; `major` the format major.
		std::vector<std::uint8_t> container(const std::uint8_t kind, const std::vector<section>& sections, const std::uint16_t major = 1, const std::uint32_t required = 0)
		{
			std::vector<std::uint8_t> file(lc_header_size, 0);
			std::memcpy(file.data(), lc_magic.data(), 8);
			file[8] = kind;
			file[9] = 0x11;
			std::memcpy(file.data() + 0x0a, le16(major).data(), 2);
			std::memcpy(file.data() + 0x10, le32(required).data(), 4);
			std::memcpy(file.data() + 0x1c, le32(static_cast<std::uint32_t>(sections.size())).data(), 4);
			std::memcpy(file.data() + 0x20, le32(lc_header_size).data(), 4);
			std::memcpy(file.data() + 0x24, le32(0x01040000).data(), 4);
			std::memcpy(file.data() + 0x28, le32(14).data(), 4);
			std::memcpy(file.data() + 0x30, le32(0xabcdef01).data(), 4);
			std::memcpy(file.data() + 0x3c, le32(3).data(), 4);
			std::vector<std::uint8_t> table, payloads;
			std::uint32_t offset = static_cast<std::uint32_t>(lc_header_size + sections.size() * lc_section_entry);
			for (const auto& s : sections)
			{
				while (payloads.size() % 4) payloads.push_back(0);
				const std::uint32_t at = offset + static_cast<std::uint32_t>(payloads.size());
				append(table, std::vector<std::uint8_t>(s.tag.begin(), s.tag.end()));
				table.push_back(s.major); table.push_back(s.minor);
				append(table, le16(s.flags));
				append(table, le32(at));
				append(table, le32(static_cast<std::uint32_t>(s.payload.size())));
				append(table, le32(0));
				append(table, le32(crc32(s.payload.data(), s.payload.size())));
				append(payloads, s.payload);
			}
			append(file, table);
			append(file, payloads);
			std::memcpy(file.data() + 0x18, le32(static_cast<std::uint32_t>(file.size())).data(), 4);
			std::memcpy(file.data() + 0x38, le32(crc32(file.data() + lc_header_size, file.size() - lc_header_size)).data(), 4);
			std::memcpy(file.data() + 0x84, le32(crc32(file.data(), 0x84)).data(), 4);
			return file;
		}

		std::vector<section> campaign_sections(const std::string& description = "12:34 - somewhere (Normal)")
		{
			std::vector<section> out;
			for (const auto tag : campaign_required)
			{
				section s;
				s.tag = std::string(tag);
				s.payload = tag == "META" ? meta(description, 3, 1234.5f) : empty_section();
				out.push_back(s);
			}
			return out;
		}
}
