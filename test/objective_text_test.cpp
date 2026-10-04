#include "objective_text_rules.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#include <vector>
#include <Windows.h>

int main(int argc, char** argv)
{
	using namespace objective_text_rules;
	texts table;
	int failures = 0;
	auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL: %s\n", what); } };
	const char* original = "synthetic original";
	table.reset(5);
	table.attribute(5, "updatedescription", "synthetic complete");
	check(table.select(5, 3, original) == original, "incomplete retains original pointer");
	check(std::strcmp(table.select(5, 0x40, original), "synthetic complete") == 0, "completion selects alternate");
	check(std::strcmp(table.select(5, 0xc0, original), "synthetic complete") == 0, "hidden completion and loaded state select alternate");
	check(table.select(5, 0, original) == original, "reverted completion uses original");
	table.attribute(200, "UpdateDescription", "second file");
	table.reset(7);
	check(std::strcmp(table.select(5, 0x40, original), "synthetic complete") == 0, "another allocation does not clear first file");
	check(std::strcmp(table.select(200, 0x40, original), "second file") == 0, "case-insensitive field and unsigned index");
	std::array<unsigned char, 2> records{200, 5};
	std::sort(records.begin(), records.end());
	check(std::strcmp(table.select(records[0], 0x40, original), "synthetic complete") == 0, "record reordering preserves identity");
	table.reset(5);
	check(table.select(5, 0x40, original) == original, "reused slot without extension cannot leak old text");
	table.attribute(7, "updatedescription", "");
	check(table.select(7, 0x40, original) == original, "empty extension falls back");
	table.attribute(7, "description", "ignored");
	check(table.select(7, 0x40, original) == original, "original attributes not captured");
	table.attribute(255, "updatedescription", std::string(300, 'x'));
	check(std::strlen(table.select(255, 0x40, original)) == text_capacity - 1, "native-sized bounded text and final index");
	const auto jump = branch<6>(0x1000, 0x2400);
	std::uint32_t displacement = 0;
	std::memcpy(&displacement, jump.data() + 1, 4);
	check(jump[0] == 0xe9 && 0x1005 + displacement == 0x2400 && jump[5] == 0x90, "jump encoding and displaced instruction padding");
	check(branch<5>(0x2400, 0x1000, true)[0] == 0xe8, "attribute parser call encoding");
	if (argc > 1)
	{
		std::ifstream file(argv[1], std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
		auto read = [&](std::size_t offset, void* target, std::size_t size)
		{
			if (offset > bytes.size() || size > bytes.size() - offset) return false;
			std::memcpy(target, bytes.data() + offset, size);
			return true;
		};
		IMAGE_DOS_HEADER dos{};
		IMAGE_NT_HEADERS32 nt{};
		if (!read(0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
			!read(static_cast<std::size_t>(dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
			nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt.OptionalHeader.ImageBase != 0x400000)
		{
			check(false, "valid supported PE32 image for guard verification");
		}
		else
		{
			const auto headers = static_cast<std::size_t>(dos.e_lfanew) + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
			for (const auto& guard : guards)
			{
				bool matched = false;
				for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i)
				{
					IMAGE_SECTION_HEADER section{};
					if (!read(headers + i * sizeof section, &section, sizeof section)) break;
					const auto rva = guard.va - nt.OptionalHeader.ImageBase;
					const auto count = guard.hex.size() / 2;
					if (rva < section.VirtualAddress || rva - section.VirtualAddress > section.SizeOfRawData ||
						count > section.SizeOfRawData - (rva - section.VirtualAddress)) continue;
					std::vector<unsigned char> actual(count);
					if (!read(std::size_t(section.PointerToRawData) + rva - section.VirtualAddress, actual.data(), count)) break;
					auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
					matched = true;
					for (std::size_t j = 0; j < count; ++j)
						matched &= actual[j] == (digit(guard.hex[j * 2]) * 16 + digit(guard.hex[j * 2 + 1]));
					break;
				}
				check(matched, "retail executable guard");
				if (!matched) std::printf("guard address: 0x%08x\n", guard.va);
			}
		}
	}
	std::printf("objective text rules: %d failures\n", failures);
	return failures ? 1 : 0;
}
