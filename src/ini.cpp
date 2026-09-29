#include "ini.hpp"

#include "ini_rules.hpp"
#include "log.hpp"

namespace ini
{
	namespace
	{
		const std::filesystem::path& file()
		{
			static const auto path = logger::module_dir() / L"xml2-fix.ini";
			return path;
		}
	}

	std::optional<std::string> text(const wchar_t* section, const wchar_t* key)
	{
		return ini_rules::read(file(), section, key);
	}

	int number(const wchar_t* section, const wchar_t* key, const int fallback)
	{
		return ini_rules::read_number(file(), section, key, fallback);
	}

	bool flag(const wchar_t* section, const wchar_t* key, const bool fallback)
	{
		return ini_rules::read_flag(file(), section, key, fallback);
	}
}
