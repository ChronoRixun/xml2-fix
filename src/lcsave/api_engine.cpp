// The engine side's additions to the public interface (lcsave.hpp, last block): the zone placement hash the
// tables hold, and the profile file's three directions. On the library's internals, in api.cpp's manner.

#include "internal.hpp"

#include <cstring>

namespace lcsave
{
	namespace
	{
		template <typename F> bool guarded(std::string& error, F&& f)
		{
			try
			{
				f();
				return true;
			}
			catch (const detail::Error& e)
			{
				error = e.message;
			}
			catch (const std::exception& e)
			{
				error = std::string("internal error: ") + e.what();
			}
			return false;
		}
	}

	bool zone_file_sha1(const Tables& tables, const std::string& zone, Sha1Digest& out)
	{
		if (!tables.loaded() || !tables.data->build)
		{
			return false;
		}
		const detail::ZoneNumbering* z = nullptr;
		std::string error;
		if (!guarded(error, [&] { z = tables.data->build->zone_numbering(zone); }) || !z)
		{
			return false;
		}
		std::memcpy(out.data(), z->sha1.data(), out.size());
		return true;
	}

	Result convert_old_profile_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables, const ConvertOptions& options)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			r.refused = 1;
			return r;
		}
		detail::ConvertState state;
		r.ok = guarded(r.error, [&] { r.bytes = detail::convert_profile(old_file, *tables.data, options, state); });
		if (!r.ok)
		{
			r.refused = 1;
			r.bytes.clear();
		}
		r.ledger = r.ok ? "converted (profile)" : "refused: " + r.error;
		return r;
	}

	Result old_profile_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			return r;
		}
		detail::ConvertState state;
		r.ok = guarded(r.error, [&] { r.bytes = detail::profile_to_new(old_stream, *tables.data, context, state); });
		if (!r.ok)
		{
			r.refused = 1;
			r.bytes.clear();
		}
		return r;
	}

	Result new_to_old_profile(const std::vector<std::uint8_t>& new_file, const Tables& tables)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			return r;
		}
		r.ok = guarded(r.error, [&] { r.bytes = detail::reverse_profile(new_file, *tables.data); });
		if (!r.ok)
		{
			r.refused = 1;
		}
		return r;
	}
}
