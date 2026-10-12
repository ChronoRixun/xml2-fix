// The public calls (lcsave.hpp): every internal failure becomes a result with the reference's message.

#include "internal.hpp"

#include <algorithm>
#include <cstring>

namespace lcsave
{
	namespace
	{
		std::string ledger_line(const detail::ConvertState& s, const std::string& outcome)
		{
			std::string zones;
			for (const auto cv : s.zone_eras) zones += (zones.empty() ? "" : ",") + std::to_string(cv);
			return outcome + "; start era " + std::to_string(s.start_era) + "; items era " + std::to_string(s.items_era) + "; zone eras [" +
				zones + "]; dropped zone_records=" + std::to_string(s.dropped.zone_records) + " world_items=" +
				std::to_string(s.dropped.world_items) + " zone_vars=" + std::to_string(s.dropped.zone_vars) + " other=" +
				std::to_string(s.dropped.other) + " talents=" + std::to_string(s.dropped.talents) + " states=" +
				std::to_string(s.dropped.states) + "; notes " + std::to_string(s.notes.size());
		}

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

	Result convert_old_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables)
	{
		return convert_old_to_new(old_file, tables, ConvertOptions{});
	}

	Result convert_old_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables, const ConvertOptions& options)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			r.refused = 1;
			return r;
		}
		detail::ConvertState state;
		r.ok = guarded(r.error, [&] { r.bytes = detail::convert_slot(old_file, *tables.data, options, state); });
		if (!r.ok)
		{
			r.refused = 1;
			r.bytes.clear();
		}
		r.parked = state.parked;
		r.ledger = ledger_line(state, r.ok ? "converted" : "refused: " + r.error);
		for (const auto& n : state.notes) r.ledger += "\n" + n;
		return r;
	}

	Result new_to_old_stream(const std::vector<std::uint8_t>& new_file, const Tables& tables)
	{
		return new_to_old_stream(new_file, tables, retail_layout, nullptr, nullptr);
	}

	Result new_to_old_stream(const std::vector<std::uint8_t>& new_file, const Tables& tables, const NativeLayout& native,
		const TypedRegistry* registry, MigrationReport* report)
	{
		Result r;
		if (report) *report = MigrationReport{};
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			return r;
		}
		std::vector<std::string> notes;
		r.ok = guarded(r.error, [&]
		{
			const auto m = detail::reverse_slot(new_file, *tables.data, nullptr, notes, native, registry, report);
			r.bytes = detail::write_slot(m, native);
		});
		for (const auto& n : notes) r.ledger += (r.ledger.empty() ? "" : "\n") + n;
		if (!r.ok) { r.refused = 1; r.bytes.clear(); }
		return r;
	}

	Result old_stream_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context)
	{
		return old_stream_to_new(old_stream, tables, context, retail_layout);
	}

	Result old_stream_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context,
		const NativeLayout& native)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			return r;
		}
		detail::ConvertState state;
		r.ok = guarded(r.error, [&] { r.bytes = detail::stream_to_new(old_stream, *tables.data, context, state, native); });
		if (!r.ok)
		{
			r.refused = 1;
			r.bytes.clear();
		}
		r.ledger = ledger_line(state, r.ok ? "saved" : "refused: " + r.error);
		for (const auto& n : state.notes) r.ledger += "\n" + n;
		return r;
	}

	std::string dump(const std::vector<std::uint8_t>& file, const Tables& tables, std::string& error)
	{
		if (!tables.loaded())
		{
			error = "tables not loaded";
			return {};
		}
		std::string out;
		guarded(error, [&]
		{
			const auto& t = *tables.data;
			if (file.size() >= 8 && std::memcmp(file.data(), new_magic, 8) == 0)
			{
				out = detail::to_json(detail::dump_new(file, t));
				return;
			}
			const auto old = detail::read_slot(file.data(), file.size(), retail_layout);   // an old file is a retail save
			detail::ConvertState state;
			int start = 13, items = 13;                 // dump.dump_any: 0.1.10's eras when the converter refuses
			ConvertOptions options;
			options.original_name = "x";
			try
			{
				detail::convert_slot(file, t, options, state);
				start = state.start_era;
				items = state.items_era;
			}
			catch (const detail::Error&)
			{
			}
			out = detail::to_json(detail::dump_old(old, t, start, items));
		});
		return out;
	}

	std::string dump(const std::vector<std::uint8_t>& file, const Tables& tables)
	{
		std::string error;
		return dump(file, tables, error);
	}

	bool missing_talents(const std::vector<std::uint8_t>& old_stream, const Tables& tables, std::vector<TalentAdd>& out, std::string& error)
	{
		return missing_talents(old_stream, tables, out, error, retail_layout);
	}

	bool missing_talents(const std::vector<std::uint8_t>& old_stream, const Tables& tables, std::vector<TalentAdd>& out, std::string& error,
		const NativeLayout& native)
	{
		out.clear();
		if (!tables.loaded())
		{
			error = "tables not loaded";
			return false;
		}
		return guarded(error, [&]
		{
			std::vector<std::uint8_t> image;
			const std::vector<std::uint8_t>* src = &old_stream;
			if (old_stream.size() == native.payload)
			{
				image.assign(old_envelope, 0);
				const u32 plen = native.payload;
				std::memcpy(image.data() + 0x80, &plen, 4);
				image.insert(image.end(), old_stream.begin(), old_stream.end());
				src = &image;
			}
			const auto m = detail::read_slot(src->data(), src->size(), native);
			const auto& b = *tables.data->build;
			for (std::size_t n = 0; n < m.heroes.size(); ++n)
			{
				const std::size_t si = n + 1;
				if (si >= b.herostat_talents.size()) break;
				const auto& a = m.heroes[n].attributes;
				std::vector<u16> held;
				for (u32 k = 0; k < a.tail.count; ++k) held.push_back(a.talents[k].id);
				std::vector<u16> added;
				for (const auto tid : b.herostat_talents[si])
				{
					if (std::find(held.begin(), held.end(), tid) != held.end()) continue;
					if (std::find(added.begin(), added.end(), tid) != added.end()) continue;
					added.push_back(tid);
					TalentAdd t;
					t.hero_block = static_cast<std::uint32_t>(n);
					t.stats_index = static_cast<std::uint32_t>(si);
					t.talent_id = tid;
					t.shared = tid < 100;
					out.push_back(t);
				}
			}
		});
	}

	bool missing_talents(const std::vector<std::uint8_t>& old_stream, const Tables& tables, std::vector<TalentAdd>& out)
	{
		std::string error;
		return missing_talents(old_stream, tables, out, error);
	}
}
