#define _CRT_RAND_S
#include "save_format.hpp"

#include "save_format_rules.hpp"
#include "save_core_rules.hpp"
// Guards integration: policy and file operations live outside capture/lifetime.
#include "save_guard_rules.hpp"
#include "save_content_guards.hpp"
#include "save_trace.hpp"
#include "trivia_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "lcsave/lcsave.hpp"

#include <Windows.h>
#include <wincrypt.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifdef XML2_SAVE_STARTUP_FAULT_TEST
#include "../test/save_startup_faults.hpp"
#endif

namespace save_format
{
	namespace
	{
		using namespace save_format_rules;

		constexpr const char* as_before = "saves stay in the game's own format";
		constexpr const char* writer_name = "XML2 Fix " FIX_VERSION;

		// ---- state (the card and the loader run on the game's thread; the pipe thread only queues jobs) ----

		enum class readiness
		{
			untried,
			ok,
			failed, // D11: nothing is read or written in either folder
		};

		bool enabled = false;         // [Save] Format=lc and every guard matched
		readiness ready = readiness::untried;
		std::string installation_failure; // nonempty until every requested startup hook is installed
		std::string failure;          // why ready == failed (shown as the notice, logged once)
		lcsave::Tables tables;
		save_guard_rules::refusal_latches write_refusals;
		std::string old_folder;       // "...\Save\" as the card built it
		std::string lc_dir;           // "...\SaveLC\"
		std::string pending_reason;   // appended to the next message the card formats (a refused read / write / delete)
		std::string notice;           // shown in the highest free slot while pending
		int notice_slot = -1;

		// The campaign in play: the LC file it was loaded from (its serial, sequence, KEEP sections), for the next write.
		struct campaign
		{
			bool loaded = false;
			std::vector<std::uint8_t> file;
			std::uint64_t serial = 0;
			std::uint32_t sequence = 0;
			float play_time = 0;
			int slot = -1;
		} current;

		// CORE-BEGIN: recording metadata and lifetime (independent of Guards' file protection).
		campaign pending_campaign;
		save_core_rules::lifetime core_lifetime;
		std::vector<std::uint8_t> retained_profile;
		std::vector<lcsave::ZoneCaptureMetadata> cached_captures, pending_captures;
		lcsave::ZoneCaptureMetadata active_visit;
		bool visit_known = false, visit_restore_disabled = false, native_loading = false;
		std::uint8_t campaign_origin = 0;
		lcsave::ZoneCaptureMetadata* capturing = nullptr;
		void* restore_record = nullptr;
		std::array<std::uint16_t,256> original_restore_map{};
		std::atomic<unsigned> numbered_seen{0}, captures_seen{0}, capture_failures{0}, identity_resets{0}, resets_seen{0}, native_loads_completed{0};
		using capture_t = bool(__fastcall*)(void*, void*, const char*, int, float);
		using native_load_t = void(__fastcall*)(void*, void*, void*, int);
		capture_t retail_capture = nullptr;
		// CORE-END


		// Counters for status().
		std::atomic<unsigned> reads{0}, write_count{0}, refusals{0}, converted{0}, talents_added{0};

		// The stream helpers' overflow flag (the stubs write 1 when a copy was skipped).
		volatile std::uint8_t overflow_flag = 0;
		// The zone open bridge: 1 while CMap::load's reader runs; the return address it parks.
		volatile std::uint8_t zone_open_flag = 0;
		volatile std::uint32_t zone_open_return = 0;
		// The last placement file the engine opened: its name, byte count and SHA-1, and what the tables hold.
		std::string zone_opened_name;
		std::string zone_opened_sha1;
		std::string zone_tables_sha1;
		std::uint32_t zone_opened_size = 0;
		std::atomic<unsigned> zones_hashed{0}, zone_hash_mismatches{0};

		// The game's own card methods, kept for the message formatter and the test build's "as before" paths.
		using message_t = char*(__fastcall*)(void*, void*, int, int, int); // __thiscall as a __fastcall with an unused edx
		message_t game_message = nullptr;
		using serialize_t = void(__fastcall*)(void*, void*, void*);
		using game_getter_t = void*(__cdecl*)();
		constexpr address game_getter = 0x46dce0;

		trivia_rules::cursors trivia_cursors;
		void ensure_ready();

		void reject_install(const std::string& reason)
		{
			installation_failure = reason;
			failure = reason;
			ready = readiness::failed;
			logger::write("save format: startup refused: %s; required LC save/trivia hooks were not installed", reason.c_str());
		}

		std::string install_guard_error(const guard& site, const char* kind)
		{
			char text[512]{};
			std::snprintf(text, sizeof text, "%s at 0x%08lX (%s)", kind, static_cast<unsigned long>(site.va), site.what);
			return text;
		}

		bool __fastcall trivia_get(void* game, void*, const int act, const int question)
		{
			ensure_ready();
			if (ready != readiness::ok) return true; // refuse rewards when persistence cannot be verified
			const int bit = lcsave::trivia_bit(tables, act, question);
			if (bit < 0 || bit >= 250) return false;
			const auto* words = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(game) + 0x5e8);
			return (words[bit / 32] & (1u << (bit % 32))) != 0;
		}

		void __fastcall trivia_set(void* game, void*, const int act, const int question)
		{
			ensure_ready();
			if (ready != readiness::ok) return;
			const int bit = lcsave::trivia_bit(tables, act, question);
			if (bit < 0 || bit >= 250) return;
			auto* words = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(game) + 0x5e8);
			words[bit / 32] |= 1u << (bit % 32);
		}

		int __fastcall trivia_cursor_get(void* game, void*, const int act)
		{
			ensure_ready();
			if (ready != readiness::ok) return 0;
			return trivia_cursors.get(act, lcsave::trivia_question_count(tables, act),
				[&](int question) { return trivia_get(game, nullptr, act, question); });
		}

		void __fastcall trivia_cursor_set(void*, void*, const int act, const int question)
		{
			ensure_ready();
			if (ready == readiness::ok) trivia_cursors.set(act, lcsave::trivia_question_count(tables, act), question);
		}

		// ---- helpers --------------------------------------------------------------------------------------

		template <typename T>
		T at(const void* base, const std::size_t offset)
		{
			T value{};
			std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof value);
			return value;
		}

		void* in_game(const address va)
		{
			return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va));
		}

		address address_of(const void* pointer)
		{
			return static_cast<address>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		// A copy of game memory under SEH (a faulting read gives false, nothing copied).
		bool try_copy(void* to, const void* from, const std::size_t size)
		{
			__try
			{
				std::memcpy(to, from, size);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}

		struct write
		{
			address va;
			std::vector<std::uint8_t> bytes;
		};

		// Preflight every site before writing. Any write/cache/protection failure vetoes startup,
		// including failures after bytes changed; cleanup still attempts every protection restore.
		bool patch(const std::vector<write>& writes)
		{
			std::vector<DWORD> protection(writes.size());
			const auto restore = [&](std::size_t count)
			{
				bool ok = true;
				for (std::size_t i = count; i-- > 0;)
				{
					DWORD ignored = 0;
					if (!VirtualProtect(in_game(writes[i].va), writes[i].bytes.size(), protection[i], &ignored))
					{
						logger::write("save format: ERROR: can't restore XMen2.exe protection at 0x%08lX (error %lu); startup refused", static_cast<unsigned long>(writes[i].va), GetLastError());
						ok = false;
					}
				}
				return ok;
			};
			for (std::size_t i = 0; i < writes.size(); ++i)
			{
				if (!VirtualProtect(in_game(writes[i].va), writes[i].bytes.size(), PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("save format: ERROR: can't unprotect XMen2.exe at 0x%08lX (error %lu); startup refused", static_cast<unsigned long>(writes[i].va), GetLastError());
					if (!restore(i)) logger::write("save format: ERROR: preflight protection cleanup failed; startup remains refused");
					return false;
				}
			}
			bool ok = true;
			for (const auto& w : writes)
			{
				std::memcpy(in_game(w.va), w.bytes.data(), w.bytes.size());
				if (!FlushInstructionCache(GetCurrentProcess(), in_game(w.va), w.bytes.size()))
				{
					logger::write("save format: ERROR: can't flush XMen2.exe instructions at 0x%08lX (error %lu); startup refused", static_cast<unsigned long>(w.va), GetLastError());
					ok = false;
					break;
				}
			}
			const bool restored = restore(writes.size());
			return ok && restored;
		}

		std::vector<std::uint8_t> dword_bytes(const std::uint32_t v)
		{
			std::vector<std::uint8_t> out;
			put32(out, v);
			return out;
		}

		std::uint64_t now_filetime()
		{
			FILETIME ft{};
			GetSystemTimeAsFileTime(&ft);
			return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
		}

		std::uint64_t random_serial()
		{
			unsigned a = 0, b = 0;
			rand_s(&a);
			rand_s(&b);
			std::uint64_t v = (static_cast<std::uint64_t>(a) << 32) | b;
			return v ? v : 1;
		}

		std::string hex(const std::uint8_t* data, const std::size_t size)
		{
			static const char digits[] = "0123456789abcdef";
			std::string out;
			for (std::size_t i = 0; i < size; ++i)
			{
				out += digits[data[i] >> 4];
				out += digits[data[i] & 15];
			}
			return out;
		}

		// SHA-1 through CryptoAPI (advapi32); "" when it fails.
		std::string sha1_hex(const std::uint8_t* data, const std::size_t size)
		{
			HCRYPTPROV provider = 0;
			HCRYPTHASH hash = 0;
			std::string out;
			if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
			{
				return out;
			}
			if (CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash))
			{
				bool ok = true;
				std::size_t done = 0;
				while (ok && done < size)
				{
					const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size - done, 1u << 20));
					ok = CryptHashData(hash, data + done, chunk, 0) != 0;
					done += chunk;
				}
				std::uint8_t digest[20]{};
				DWORD length = sizeof digest;
				if (ok && (size != 0 || CryptHashData(hash, data, 0, 0)) && CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0))
				{
					out = hex(digest, length);
				}
				CryptDestroyHash(hash);
			}
			CryptReleaseContext(provider, 0);
			return out;
		}

		// ---- files ----------------------------------------------------------------------------------------

		struct file_info
		{
			bool exists = false;
			std::uint64_t size = 0;
			std::uint64_t mtime = 0;
		};

		file_info stat_file(const std::string& path)
		{
			file_info info;
			WIN32_FILE_ATTRIBUTE_DATA data{};
			if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data) && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				info.exists = true;
				info.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
				info.mtime = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
			}
			return info;
		}

		bool file_exists(const std::string& path)
		{
			return stat_file(path).exists;
		}

		std::string slot_path(const int slot)
		{
			return lc_dir + slot_file(slot);
		}

		// The LC file of a slot as the slot list and the loader see it: the file, else its backup when the file is
		// damaged (never for an intact file this build can't use). `path` says which was used.
		struct loaded_file
		{
			inspection info;
			std::vector<std::uint8_t> bytes;
			std::string path;
			bool from_backup = false;
			bool exists = false;
		};

		loaded_file load_lc(const std::string& path, const std::uint8_t kind)
		{
			loaded_file out;
			std::string why;
			if (!file_exists(path))
			{
				const std::string bak = path + ".bak";
				if (!file_exists(bak))
				{
					return out;
				}
				out.exists = true;
				out.path = bak;
				out.from_backup = true;
				if (!read_file(bak, out.bytes, why))
				{
					out.info.status = file_status::damaged;
					out.info.why = why;
					return out;
				}
				out.info = inspect(out.bytes.data(), out.bytes.size(), kind);
				return out;
			}
			out.exists = true;
			out.path = path;
			if (read_file(path, out.bytes, why))
			{
				out.info = inspect(out.bytes.data(), out.bytes.size(), kind);
			}
			else
			{
				out.info.status = file_status::damaged;
				out.info.why = why;
			}
			if (out.info.status == file_status::damaged)
			{
				const std::string bak = path + ".bak";
				std::vector<std::uint8_t> backup;
				if (file_exists(bak) && read_file(bak, backup, why))
				{
					const auto info = inspect(backup.data(), backup.size(), kind);
					if (info.status != file_status::damaged)
					{
						logger::write("save format: %s is damaged (%s); its backup stands in", path.c_str(), out.info.why.c_str());
						out.info = info;
						out.bytes = std::move(backup);
						out.path = bak;
						out.from_backup = true;
					}
				}
			}
			return out;
		}

		// ---- readiness: the tables and the import, once, at the first file operation ---------------------

		std::string game_folder_narrow()
		{
			// The DLL's folder (the game folder once installed), as the library wants it.
			return logger::module_dir().string();
		}

		void fail_closed(const std::string& why)
		{
			ready = readiness::failed;
			failure = why;
			notice = "saves are off: " + why;
			logger::write("save format: ERROR: %s - saving and loading are disabled for this session (D11); no file in Save or SaveLC is opened", why.c_str());
		}

		std::vector<ledger_entry> ledger;

		bool write_ledger(std::string& error)
		{
			std::string text = "# XML2 Fix " FIX_VERSION ": old saves imported into SaveLC (name, size, modification time, SHA-1, result, slot, fix version, detail). Edit nothing here.\n";
			for (const auto& e : ledger)
			{
				text += ledger_line(e) + "\n";
			}
			return write_atomically(lc_dir + ledger_file, std::vector<std::uint8_t>(text.begin(), text.end()), error);
		}

		void add_notice(const std::string& text)
		{
			if (notice.empty())
			{
				notice = text;
			}
		}

		// SPEC 5.6 B: every old file not in the ledger is converted into the lowest free slot (its own when free).
		void import_old_saves()
		{
			std::vector<std::uint8_t> text;
			std::string why;
			if (file_exists(lc_dir + ledger_file) && read_file(lc_dir + ledger_file, text, why))
			{
				ledger = parse_ledger(std::string(text.begin(), text.end()));
			}
			std::array<bool, slot_count> taken{};
			for (int n = 0; n < slot_count; ++n)
			{
				taken[n] = file_exists(slot_path(n)) || file_exists(slot_path(n) + ".bak");
			}
			std::vector<std::string> names{old_profile_file};
			for (int n = 0; n < slot_count; ++n)
			{
				names.push_back(old_slot_file(n));
			}
			bool ledger_changed = false;
			unsigned seen = 0, skipped = 0, done = 0, refused_count = 0, waiting = 0;
			for (const auto& name : names)
			{
				const std::string path = old_folder + name;
				const auto info = stat_file(path);
				if (!info.exists)
				{
					continue;
				}
				++seen;
				if (ledger_check(ledger, name, info.size, info.mtime, "", FIX_VERSION) == ledger_verdict::skip_unread)
				{
					++skipped;
					continue;
				}
				std::vector<std::uint8_t> bytes;
				if (!read_file(path, bytes, why))
				{
					logger::write("save format: import: %s not read (%s) - left for the next start", path.c_str(), why.c_str());
					continue;
				}
				const std::string sha1 = sha1_hex(bytes.data(), bytes.size());
				const auto verdict = ledger_check(ledger, name, info.size, info.mtime, sha1, FIX_VERSION);
				if (verdict == ledger_verdict::skip_unread || verdict == ledger_verdict::skip_converted)
				{
					++skipped;
					continue;
				}
				// A line for this file replaces an older one with the same SHA-1 (a retry).
				ledger.erase(std::remove_if(ledger.begin(), ledger.end(), [&](const ledger_entry& e) { return e.sha1 == sha1; }), ledger.end());
				ledger_entry entry;
				entry.name = name;
				entry.size = info.size;
				entry.mtime = info.mtime;
				entry.sha1 = sha1;
				entry.fix_version = FIX_VERSION;
				ledger_changed = true;
				const bool profile = name == old_profile_file;
				const auto own = old_slot_of(name);
				std::string target;
				int slot = -1;
				if (profile)
				{
					target = lc_dir + profile_file;
					if (file_exists(target))
					{
						entry.result = "skipped";
						entry.detail = "profile.lcsave exists already";
						ledger.push_back(entry);
						++skipped;
						continue;
					}
				}
				else
				{
					const auto chosen = import_slot(taken, own.value_or(-1));
					if (!chosen)
					{
						entry.result = "waiting";
						entry.detail = "no free slot in SaveLC (retried at the next start)";
						ledger.push_back(entry);
						++waiting;
						add_notice("an old save waits for a free slot (" + name + ")");
						logger::write("save format: import: %s waits for a free slot", name.c_str());
						continue;
					}
					slot = *chosen;
					target = slot_path(slot);
				}
				lcsave::ConvertOptions options;
				options.original_name = name;
				options.slot = static_cast<std::uint8_t>(slot < 0 ? 0 : slot);
				options.fixed_saved_at = true;
				options.saved_at = now_filetime();
				options.writer_version = writer_version(FIX_VERSION_MAJOR, FIX_VERSION_MINOR, FIX_VERSION_PATCH, writer_kind_import);
				options.writer = writer_name;
				// the campaign serial: the library's default, the first 8 bytes of the original's SHA-1
				const auto result = profile ? lcsave::convert_old_profile_to_new(bytes, tables, options) : lcsave::convert_old_to_new(bytes, tables, options);
				const std::uint8_t kind = profile ? kind_profile : kind_campaign;
				if (!result.ok)
				{
					entry.result = "refused";
					entry.slot = -1;
					entry.detail = result.error;
					ledger.push_back(entry);
					++refused_count;
					add_notice("an old save could not be converted (" + name + "): " + result.error);
					logger::write("save format: import: %s refused: %s", name.c_str(), result.error.c_str());
					continue;
				}
				if (inspect(result.bytes.data(), result.bytes.size(), kind).status != file_status::ok)
				{
					entry.result = "refused";
					entry.detail = "the converted file doesn't pass the reader (a library fault)";
					ledger.push_back(entry);
					++refused_count;
					add_notice("an old save could not be converted (" + name + ")");
					logger::write("save format: import: ERROR: the converted %s doesn't pass the reader", name.c_str());
					continue;
				}
				if (!write_atomically(target, result.bytes, why))
				{
					entry.result = "waiting";
					entry.detail = "not written: " + why;
					ledger.push_back(entry);
					++waiting;
					add_notice("an old save could not be written to SaveLC (" + name + ")");
					logger::write("save format: import: %s converted but not written: %s", name.c_str(), why.c_str());
					continue;
				}
				entry.result = "converted";
				entry.slot = slot;
				entry.detail = result.ledger;
				ledger.push_back(entry);
				if (slot >= 0)
				{
					taken[slot] = true;
				}
				++done;
				converted.fetch_add(1);
				logger::write("save format: import: %s (%llu bytes, SHA-1 %s) -> %s%s; %s", name.c_str(), static_cast<unsigned long long>(info.size), sha1.c_str(),
				              profile ? profile_file : slot_file(slot).c_str(), result.refused ? (" with " + std::to_string(result.refused) + " things dropped").c_str() : "",
				              result.ledger.c_str());
			}
			if (ledger_changed && !write_ledger(why))
			{
				logger::write("save format: import: ERROR: the ledger %s%s wasn't written: %s", lc_dir.c_str(), ledger_file, why.c_str());
			}
			logger::write("save format: import: %u old file(s) in %s: %u converted now, %u known from the ledger, %u refused, %u waiting (originals untouched; ledger %s%s)",
			              seen, old_folder.c_str(), done, skipped, refused_count, waiting, lc_dir.c_str(), ledger_file);
		}

		void ensure_ready()
		{
			if (ready != readiness::untried)
			{
				return;
			}
			ready = readiness::ok;
			char folder[0x105]{};
			if (!try_copy(folder, in_game(card_folder), 0x104))
			{
				folder[0] = 0;
			}
			old_folder = folder;
			const auto lc = lc_folder(old_folder);
			if (!lc)
			{
				fail_closed("the game's save folder isn't known (\"" + old_folder + "\")");
				return;
			}
			lc_dir = *lc;
			if (!CreateDirectoryA(lc_dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
			{
				fail_closed("can't create " + lc_dir + " (error " + std::to_string(GetLastError()) + ")");
				return;
			}
			std::string error;
			const auto build = logger::module_dir().wstring();
			if (!save_content_guards::verify_mods(std::filesystem::path(build), error) ||
			    !lcsave::load_tables(build, tables, error))
			{
				fail_closed("the save tables of this build don't verify: " + error);
				return;
			}
			// Prepare content identity before the front end can enter an online session.
			// Captures and overwrite preflight use a nonblocking change check, never a tree scan.
			const auto digest_start = GetTickCount64();
			if (!lcsave::prepare_conversation_digest(tables, error))
			{
				fail_closed("the conversation identity of this build cannot be prepared: " + error);
				return;
			}
			logger::write("save format: conversation identity prepared in %llu ms before play",
				static_cast<unsigned long long>(GetTickCount64()-digest_start));
			logger::write("save format: tables of %ls verified: content version %lu, catalogue revision %lu; %lu heroes, %lu stats names, %lu items, %lu courses, %lu objectives",
			              build.c_str(), static_cast<unsigned long>(tables.content_version()), static_cast<unsigned long>(tables.catalogue_revision()),
			              static_cast<unsigned long>(tables.hero_count()), static_cast<unsigned long>(tables.stats_count()), static_cast<unsigned long>(tables.item_count()),
			              static_cast<unsigned long>(tables.course_count()), static_cast<unsigned long>(tables.objective_count()));
			logger::write("save format: LC saves in %s (the old %s is read once for the import and never written)", lc_dir.c_str(), old_folder.c_str());
			import_old_saves();
		}

		void refuse(const std::string& reason)
		{
			pending_reason = reason;
			refusals.fetch_add(1);
			logger::write("save format: refused: %s", reason.c_str());
		}

		// ---- the old image <-> the engine's buffer ----------------------------------------------------------

		// 0x80 bytes of description (as the card's writer keeps it: strncpy 0x7f), the u32 length, the payload.
		std::vector<std::uint8_t> old_image(const char* description, const std::uint8_t* payload, const std::size_t size)
		{
			std::vector<std::uint8_t> out(old_envelope + size);
			if (description)
			{
				strncpy_s(reinterpret_cast<char*>(out.data()), 0x80, description, description_max);
			}
			const auto length = static_cast<std::uint32_t>(size);
			std::memcpy(out.data() + 0x80, &length, 4);
			std::memcpy(out.data() + old_envelope, payload, size);
			return out;
		}

		// Inserts the talents the library reports into the stream's hero heaps (SPEC-old-save 1.6 b), logged per hero.
		bool add_missing_talents(std::vector<std::uint8_t>& image)
		{
			std::vector<lcsave::TalentAdd> adds;
			if (!lcsave::missing_talents(image, tables, adds)) return false;
			if (adds.empty()) return true;
			const auto heaps = hero_heaps(image.data(), image.size());
			if (!heaps)
			{
				logger::write("save format: the stream's hero blocks don't parse - no talent added");
				return false;
			}
			unsigned added = 0, full = 0, powers = 0;
			bool valid=true;
			for (const auto& a : adds)
			{
				// Only the shared talents (grab, SPEC 61): a hero's own powers are the engine's to grant when the hero
				// joins (heroes not yet in the party hold shared talents only), and a power inserted at rank 0 before
				// that is not known to be harmless.
				if (!a.shared)
				{
					++powers;
					continue;
				}
				if (a.hero_block >= heaps->size())
				{
					logger::write("save format: talent %u for stats index %lu names hero block %lu of %zu - skipped", a.talent_id, static_cast<unsigned long>(a.stats_index),
					              static_cast<unsigned long>(a.hero_block), heaps->size());
					valid=false;continue;
				}
				switch (insert_talent(image.data() + (*heaps)[a.hero_block].heap, a.talent_id))
				{
				case heap_insert::inserted:
					++added;
					logger::write("save format: hero block %lu (stats index %lu): talent id %u added at rank 0 (the save predates it)", static_cast<unsigned long>(a.hero_block),
					              static_cast<unsigned long>(a.stats_index), a.talent_id);
					break;
				case heap_insert::present:
					break;
				case heap_insert::full:
					++full;
					logger::write("save format: hero block %lu (stats index %lu): talent id %u NOT added - the heap holds 19 talents already", static_cast<unsigned long>(a.hero_block),
					              static_cast<unsigned long>(a.stats_index), a.talent_id);
					break;
				case heap_insert::invalid:
					valid=false;
					logger::write("save format: hero block %lu (stats index %lu): talent id %u NOT added - the heap's count or bitmap isn't the engine's",
					              static_cast<unsigned long>(a.hero_block), static_cast<unsigned long>(a.stats_index), a.talent_id);
					break;
				}
			}
			talents_added.fetch_add(added);
			if (added || full || powers)
			{
				logger::write("save format: %u missing shared talent(s) added, %u refused (heap full); %u hero power(s) the save lacks left to the engine", added, full, powers);
			}
			return valid && full==0;
		}

		// ---- the card's methods (__thiscall: this in ecx, the arguments on the stack, callee cleans) -------------

		// The card remembers the device number of its last call at +0x31c (every retail method stores it).
		void store_device(void* card, const int device)
		{
			std::memcpy(static_cast<std::uint8_t*>(card) + 0x31c, &device, 4);
		}

		std::uint32_t __fastcall card_count_campaigns(void* card, void*, const int device)
		{
			store_device(card, device);
			ensure_ready();
			// D11/import notices must remain reachable even when no campaign can be listed.
			if (ready != readiness::ok) return 1;
			const auto count = save_guard_rules::count_lc_slots(lc_dir);
			return count || notice.empty() ? count : 1;
		}

		std::uint32_t __fastcall card_delete_all(void* card, void*, const int device)
		{
			store_device(card, device);
			logger::write("save format: the game asked to delete every save (card vt+0x28) - nothing deleted in the LC mode");
			return 1;
		}

		std::uint32_t __fastcall card_profile_exists(void* card, void*, const int device)
		{
			store_device(card, device);
			ensure_ready();
			if (ready != readiness::ok)
			{
				return 0;
			}
			return file_exists(lc_dir + profile_file) || file_exists(lc_dir + profile_file + ".bak") ? 1 : 0;
		}


		// CORE-BEGIN: actual capture provenance, bounds, per-entity checks and campaign reset.
		std::string game_text(const char* pointer)
		{
			std::string text;
			if (!pointer) return text;
			for (std::size_t i=0;i<256;++i)
			{
				char c=0;
				if (!try_copy(&c,pointer+i,1)) return {};
				if (!c) return text;
				text.push_back(c);
			}
			return {}; // never label a truncated name as verified
		}

		std::string record_zone(void* record)
		{
			std::uint32_t handle=0, offset=0, used=0;
			if (!record || !try_copy(&handle,record,4)) return {};
			const auto slot=handle&0x00ffffff;
			if (!slot || slot>=2048 || !try_copy(&used,in_game(0xa26c30+0x2004),4) || used>0x3800 ||
			    !try_copy(&offset,in_game(0xa26c30+4+slot*4),4) || offset>=used) return {};
			return save_core_rules::folded(game_text(static_cast<const char*>(in_game(0xa26c30+0x2008+offset))));
		}

		bool entity_names(void* entity,std::string& definition,std::string& instance)
		{
			std::uint32_t def=0,name=0;
			if (!entity || !try_copy(&def,static_cast<const std::uint8_t*>(entity)+0x14,4) ||
			    !try_copy(&name,static_cast<const std::uint8_t*>(entity)+0x18,4)) return false;
			// CEntity stores general-table handles here. Read without calling its lazy table getter.
			auto read=[](std::uint32_t offset,void* out,std::size_t n){return try_copy(out,in_game(0xa2c440+offset),n);};
			return save_core_rules::general_name(def,read,definition) && !definition.empty() &&
			       save_core_rules::general_name(name,read,instance);
		}

		void __cdecl numbering_hook(void* entity,const std::uint32_t placement,const std::uint32_t ordinal)
		{
			numbered_seen.fetch_add(1);
			if (!visit_known) return;
			lcsave::PlacedEntityMetadata item;
			item.ordinal=ordinal; item.placement_index=placement;
			if (!save_core_rules::ordinal_fits(ordinal) || !entity_names(entity,item.definition_name,item.instance_name))
			{
				active_visit.capture_flags|=save_core_rules::capture_incomplete;
				core_lifetime.write_blocked=true;
				logger::write("save core: number observation failed: entity=%p ordinal=%lu placement=%lu raw_names=%08lx/%08lx def=%s inst=%s",
				              entity,static_cast<unsigned long>(ordinal),static_cast<unsigned long>(placement),
				              static_cast<unsigned long>(at<std::uint32_t>(entity,0x14)),static_cast<unsigned long>(at<std::uint32_t>(entity,0x18)),
				              item.definition_name.c_str(),item.instance_name.c_str());
				return;
			}
			active_visit.entities.push_back(std::move(item));
		}

		lcsave::ZoneCaptureMetadata* metadata_for(void* record)
		{
			const auto zone=record_zone(record);
			float stamp=0;
			if (zone.empty() || !try_copy(&stamp,static_cast<const std::uint8_t*>(record)+0x55d8,4)) return nullptr;
			for (auto& meta:cached_captures) if (meta.zone==zone && meta.time_stamp==stamp) return &meta;
			return nullptr;
		}

		void __cdecl capture_overflow(void*)
		{
			if (capturing) capturing->capture_flags|=save_core_rules::capture_incomplete;
			if (visit_known) active_visit.capture_flags|=save_core_rules::capture_incomplete;
			capture_failures.fetch_add(1);
			core_lifetime.write_blocked=true;
		}

		bool __fastcall capture_hook(void* record,void*,const char* name,const int flag,const float stamp)
		{
			const auto capture_start = GetTickCount64();
			if (ini::flag(L"Debug", L"SaveTiming", false)) logger::write("save timing: capture begin");
			lcsave::ZoneCaptureMetadata meta=active_visit;
			meta.zone=save_core_rules::folded(game_text(name)); meta.time_stamp=stamp;
			if (!visit_known || meta.zone!=active_visit.zone)
				meta.capture_flags|=save_core_rules::capture_unobserved;
			std::string error;
			if (ready!=readiness::ok || !lcsave::conversation_digest(tables,meta.zone,meta.conversation_digest,error))
				meta.capture_flags|=save_core_rules::capture_unobserved;
			if (ini::flag(L"Debug", L"SaveTiming", false)) logger::write("save timing: conversation digest %llu ms", static_cast<unsigned long long>(GetTickCount64()-capture_start));
			capturing=&meta;
			const bool ok=retail_capture(record,nullptr,name,flag,stamp);
			capturing=nullptr;
			if (!ok) meta.capture_flags|=save_core_rules::capture_incomplete;
			if (meta.capture_flags&save_core_rules::capture_incomplete) core_lifetime.write_blocked=true;
			// A zero hash or model-inferred mapping must never become an observed capture.
			if (meta.capture_flags&save_core_rules::capture_unobserved)
			{
				core_lifetime.write_blocked=true;
				logger::write("save core: capture of %s has no verified visit provenance; writes blocked",meta.zone.c_str());
			}
			cached_captures.erase(std::remove_if(cached_captures.begin(),cached_captures.end(),[&](const auto& old){return old.zone==meta.zone;}),cached_captures.end());
			cached_captures.push_back(std::move(meta)); captures_seen.fetch_add(1);
			logger::write("save core: captured %s at %.9g; %zu observed numbered entities; status 0x%08lx",
			              cached_captures.back().zone.c_str(),static_cast<double>(stamp),cached_captures.back().entities.size(),
			              static_cast<unsigned long>(cached_captures.back().capture_flags));
			if (ini::flag(L"Debug", L"SaveTiming", false)) logger::write("save timing: capture total %llu ms", static_cast<unsigned long long>(GetTickCount64()-capture_start));
			return ok;
		}

		void reset_zone_record(void* record,const std::string& zone)
		{
			// Native clear(1) disposes dependent world items, not just ordinal slots. Keep the named empty
			// cache record so its manager count remains valid; capture later replaces it with observed state.
			std::uint32_t handle=0; float stamp=0;
			try_copy(&handle,record,4); try_copy(&stamp,static_cast<const std::uint8_t*>(record)+0x55d8,4);
			using clear_t=void(__fastcall*)(void*,void*,int);
			reinterpret_cast<clear_t>(in_game(0x465ec0))(record,nullptr,1);
			std::memcpy(record,&handle,4);
			const std::uint32_t count=1;
			std::memcpy(static_cast<std::uint8_t*>(record)+0x4808,&count,4);
			std::memcpy(static_cast<std::uint8_t*>(record)+0x55d8,&stamp,4);
			visit_restore_disabled=true; identity_resets.fetch_add(1);
			notice="LC save: "+zone+" changed or its numbering differs; zone state reset";
			logger::write("save core: %s: mode-2 numbering/placement mismatch; remaining restores disabled and dependent items cleared",zone.c_str());
			cached_captures.erase(std::remove_if(cached_captures.begin(),cached_captures.end(),[&](const auto& old){return old.zone==zone;}),cached_captures.end());
			if (visit_known)
			{
				auto fresh=active_visit;fresh.time_stamp=stamp;cached_captures.push_back(std::move(fresh));
			}
		}

		bool __fastcall restore_hook(void* record,void*,void* entity)
		{
			if (!record || !entity || visit_restore_disabled) return true;
			std::uint16_t ordinal=0;
			if (!try_copy(&ordinal,static_cast<const std::uint8_t*>(entity)+0x6e,2) || !save_core_rules::ordinal_fits(ordinal))
			{core_lifetime.write_blocked=true;return true;}
			if (restore_record!=record)
			{
				// Freeze the reconstructed model map before changing any actual ordinal slot. Otherwise an
				// earlier remap could overwrite the source needed by a later identical-name placement.
				if (!try_copy(original_restore_map.data(),static_cast<const std::uint8_t*>(record)+0x480c,512))
				{core_lifetime.write_blocked=true;return true;}
				restore_record=record;
			}
			std::string def,name;
			const auto* meta=metadata_for(record);
			const auto* observed=active_visit.entities.empty()?nullptr:&active_visit.entities.back();
			const lcsave::PlacedEntityMetadata* expected=nullptr;
			if (meta && observed && observed->ordinal==ordinal && entity_names(entity,def,name))
				expected=save_core_rules::placement_source(meta->entities,observed->placement_index,def,name);
			if (!visit_known || !meta || !observed || observed->ordinal!=ordinal ||
			    meta->placement_sha1!=active_visit.placement_sha1)
			{
				logger::write("save core: restore reset diagnostic zone=%s visit=%d meta=%d observed=%d ordinal=%lu observed_ordinal=%lu placement=%lu cache=%zu pending=%zu hash_match=%d",
				              record_zone(record).c_str(),visit_known?1:0,meta?1:0,observed?1:0,static_cast<unsigned long>(ordinal),
				              observed?static_cast<unsigned long>(observed->ordinal):0,observed?static_cast<unsigned long>(observed->placement_index):0,
				              cached_captures.size(),pending_captures.size(),meta&&meta->placement_sha1==active_visit.placement_sha1?1:0);
				reset_zone_record(record,record_zone(record));return true;
			}
			std::uint16_t state_index=0;
			if (!expected)
			{
				// An old inferred capture lacks explicit tombstones. Its original zero slot is legacy
				// removal semantics, never permission to apply another saved twin's record.
				if ((meta->capture_flags&save_core_rules::capture_unobserved) && original_restore_map[ordinal-1]==0)
					return false;
				reset_zone_record(record,record_zone(record));return true;
			}
			if (!save_core_rules::ordinal_fits(expected->ordinal))
			{reset_zone_record(record,record_zone(record));return true;}
			state_index=original_restore_map[expected->ordinal-1];
			if (state_index>=768){core_lifetime.write_blocked=true;return true;}
			// Match actual placement BEFORE the zero-map/death decision. A skipped factory changes native
			// ordinals even when placement bytes and names are identical. Tombstones retain flags_b=0.
			std::memcpy(static_cast<std::uint8_t*>(record)+0x480c+2*(ordinal-1),&state_index,2);
			using restore_t=bool(__fastcall*)(void*,void*,void*);
			return reinterpret_cast<restore_t>(in_game(0x465c10))(record,nullptr,entity);
		}

		void __cdecl campaign_reset_hook()
		{
			if (native_loading) return; // a reset performed as part of native load belongs to the pending load
			current={}; pending_campaign={};cached_captures.clear();pending_captures.clear();
			trivia_cursors.clear();
			core_lifetime.new_game(); campaign_origin=0; visit_known=false;active_visit={};restore_record=nullptr;
			resets_seen.fetch_add(1);
			logger::write("save core: resetgame: campaign retention, identity and capture metadata cleared; profile retained");
		}

		bool run_native_load(void* game,void* buffer,const int mode)
		{
			__try
			{
				reinterpret_cast<native_load_t>(in_game(game_load))(game,nullptr,buffer,mode);return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER){return false;}
		}

		void __fastcall native_load_hook(void* game,void*,void* buffer,const int mode)
		{
			logger::write("save core: load entry diagnostic mode=%d pending=%d cached=%zu pending_metadata=%zu buffer=%p",mode,pending_campaign.loaded?1:0,cached_captures.size(),pending_captures.size(),buffer);
			const bool campaign_load=core_lifetime.begin_native(mode,pending_campaign.loaded);
			if (campaign_load && !pending_campaign.loaded)
			{
				// Mode1 is only saved stats. Only a FULL fresh transfer replaces campaign retention.
				current={};cached_captures.clear();campaign_origin=mode==2?2:0;
				active_visit={};visit_known=false;restore_record=nullptr;
				// Native online transfers contain zone records but no LC sidecar. Missing metadata is
				// not a placement mismatch: validate/infer it before the native loader sees the stream.
				std::vector<std::uint8_t> source(stream_capacity);std::string error;
				if(!buffer || !try_copy(source.data(),buffer,source.size()) ||
				   !lcsave::native_zone_metadata(source,tables,cached_captures,error))
				{
					core_lifetime.finish_native(mode,false);
					refuse("native transfer metadata could not be validated: "+(error.empty()?std::string("unreadable stream"):error));return;
				}
				logger::write("save core: native transfer mode %d: %zu zone metadata record(s) inferred from validated stream",mode,cached_captures.size());
			}
			native_loading=true; overflow_flag=0;
			const bool returned=run_native_load(game,buffer,mode);
			native_loading=false;
			// Combined Core/Guards: translated bytes do not establish native application success.
			const int loaded_slot=pending_campaign.slot;
			const auto completed=save_core_rules::native_completion(mode,pending_campaign.loaded,loaded_slot,
				returned,overflow_flag!=0,core_lifetime.write_blocked,!save_content_guards::runtime_failure().empty());
			const bool ok=completed.native_ok;
			core_lifetime.finish_native(mode,ok);
			if (ok && campaign_load) trivia_cursors.clear();
			if (ok && campaign_load && pending_campaign.loaded)
			{
				current=std::move(pending_campaign);cached_captures=std::move(pending_captures);
				core_lifetime.commit_save(current.serial,current.sequence);
				if(completed.confirm_campaign)confirm_load_success(kind_campaign,loaded_slot);
			}
			// Keep the failed file protected even if a later resetgame clears session state.
			if(completed.refuse_campaign)
				write_refusals.refuse(slot_path(loaded_slot),"native application failed; original remains protected");
			if (campaign_load){pending_campaign={};pending_captures.clear();}
			if (!ok) {current={};cached_captures.clear();refuse("native load failed or skipped stream bytes; campaign writes blocked; return to menu");}
			else native_loads_completed.fetch_add(1);
			logger::write("save core: native load mode %d returned=%d overflow=%u; retained state committed=%d; W11 remains unverified",
			              mode,returned?1:0,static_cast<unsigned>(overflow_flag),(ok&&campaign_load)?1:0);
		}

		bool available_capacity(const std::vector<std::uint8_t>& image,std::string& error)
		{
			lcsave::NativeLoadDemand demand;
			if (!lcsave::native_load_demand(image,demand,error)) return false;
			std::uint32_t attached=0,instances=0,pointer=0;
			if (!try_copy(&pointer,in_game(0x717b30),4) || !pointer || !try_copy(&attached,in_game(pointer+0x239c),4) ||
			    !try_copy(&pointer,in_game(0x72a524),4) || !pointer || !try_copy(&instances,in_game(pointer+0x3854),4))
			{error="native item/attached pool occupancy cannot be read";return false;}
			if (!save_core_rules::capacity_fits(demand.instances,instances,256))
			{error="native item-instance free capacity (256) is insufficient";return false;}
			if (!save_core_rules::capacity_fits(demand.attached_enhancements,attached,375))
			{error="native attached-enhancement free capacity (375) is insufficient";return false;}
			return true;
		}
		// CORE-END

		// A write of a kind: the engine's bytes re-expressed and written as SPEC 1.6 says.
		bool write_lc(const std::uint8_t kind, const int slot, const char* description, const std::uint8_t* data, const std::size_t size)
		{
			const auto write_start = GetTickCount64();
			// Guards: refuse automatic/default writes to an unreadable or unresolved original.
			const std::string path = kind == kind_profile ? lc_dir + profile_file : slot_path(slot);
			const auto latched = write_refusals.reason(path);
			const auto runtime = save_content_guards::runtime_failure();
			if (!latched.empty() || !runtime.empty())
			{
				refuse(path + ": " + (latched.empty() ? runtime : latched));
				return false;
			}
			const auto previous = load_lc(path,kind);
			if (previous.exists)
			{
				std::string why;
				bool usable = previous.info.status == file_status::ok &&
				              save_content_guards::verify_keys(previous.bytes,why);
				if (usable)
				{
					const auto check = kind == kind_profile ? lcsave::new_to_old_profile(previous.bytes,tables) : lcsave::new_to_old_stream(previous.bytes,tables);
					usable = check.ok;
					why = check.error;
				}
				if (!usable)
				{
					why = why.empty() ? previous.info.why : why;
					write_refusals.refuse(path,why);
					refuse(path + ": original refused (" + why + "); automatic writes locked until an explicit deletion or successful load");
					return false;
				}
			}
			if (overflow_flag)
			{
				overflow_flag = 0;
				refuse("the game's own serializer ran out of room (a copy was skipped); nothing was written");
				return false;
			}
			const auto image = old_image(description, data, size);
			lcsave::NewFileContext context;
			context.description = description ? std::string(description).substr(0, description_max) : std::string();
			context.slot = static_cast<std::uint8_t>(slot < 0 ? 0 : slot);
			context.saved_at = now_filetime();
			context.writer_version = writer_version(FIX_VERSION_MAJOR, FIX_VERSION_MINOR, FIX_VERSION_PATCH, writer_kind_fix);
			context.writer = writer_name;
			context.campaign_serial = 0;
			context.save_sequence = 0;
			if (kind == kind_campaign)
			{
				// CORE: real reset owns lifetime; play time cannot identify a new campaign.
				if (!core_lifetime.may_save())
				{
					refuse("campaign capture/load is incomplete or pending; nothing written; return to menu");return false;
				}
				const bool continues = current.loaded;
				if (continues)
				{
					context.campaign_serial = current.serial;
					context.save_sequence = current.sequence + 1;
				}
				else
				{
					context.campaign_serial = random_serial();
					context.save_sequence = 1;
				}
			}
			// CORE: live and cached records each carry their own capture provenance; never relabel old bits.
			context.retained_file=kind==kind_profile?retained_profile:current.file;
			context.zone_captures=cached_captures;
			context.origin=kind==kind_campaign?campaign_origin:0;
			context.require_capture_metadata=kind==kind_campaign;
			const auto result = kind == kind_profile ? lcsave::old_profile_to_new(image, tables, context) : lcsave::old_stream_to_new(image, tables, context);
			if (!result.ok)
			{
				// CORE-BEGIN: retain the actual refused campaign stream while its image is alive.
				logger::write("save core: refused write ledger: %s",result.ledger.c_str());
				if(kind==kind_campaign && ini::flag(L"Debug",L"SaveDumpRefused",false))
				{
					std::string dump_error;
					const auto dump_path=lc_dir+"refused-native-"+std::to_string(now_filetime())+".save";
					const bool dumped=write_atomically(dump_path,image,dump_error);
					logger::write("save core: refused native dump %s: %s",dump_path.c_str(),dumped?"written":dump_error.c_str());
				}
				// CORE-END
				refuse("the save couldn't be expressed in the LC format: " + result.error);
				return false;
			}
			const auto info = inspect(result.bytes.data(), result.bytes.size(), kind);
			if (info.status != file_status::ok)
			{
				refuse("the file about to be written doesn't pass the reader (" + info.why + "); nothing was written");
				return false;
			}
			std::string error;
			if (!save_content_guards::verify_keys(result.bytes,error) || !save_guard_rules::write_lc_atomically(path, result.bytes, error))
			{
				// I/O can be retried; existing-file identity refusals remain latched by preflight/read.
				refuse("writing " + path + " failed: " + error);
				return false;
			}
			if (kind == kind_campaign)
			{
				current.loaded = true;
				current.file = result.bytes;
				current.serial = context.campaign_serial;
				current.sequence = context.save_sequence;
				current.play_time = info.play_time;
				current.slot = slot;
				core_lifetime.commit_save(context.campaign_serial,context.save_sequence);
			}
			else retained_profile=result.bytes;
			write_count.fetch_add(1);
			// CORE-BEGIN: native dangling-item normalization must remain visible after a successful write.
			if (!result.ledger.empty()) logger::write("save core: write ledger: %s",result.ledger.c_str());
			// CORE-END
			logger::write("save format: wrote %s (%zu bytes, sequence %lu, serial %016llx%s)", path.c_str(), result.bytes.size(), static_cast<unsigned long>(context.save_sequence),
			              static_cast<unsigned long long>(context.campaign_serial), result.parked ? (", " + std::to_string(result.parked) + " parked records carried").c_str() : "");
			if (ini::flag(L"Debug", L"SaveTiming", false)) logger::write("save timing: write kind %u total %llu ms", kind, static_cast<unsigned long long>(GetTickCount64()-write_start));
			return true;
		}

		// A read of a kind: the LC file validated and translated into the engine's buffer.
		bool read_lc(const std::uint8_t kind, const int slot, std::uint32_t* size, std::uint8_t* data)
		{
			const std::size_t capacity = kind == kind_profile ? profile_size : stream_capacity;
			const std::string path = kind == kind_profile ? lc_dir + profile_file : slot_path(slot);
			const auto file = load_lc(path, kind);
			if (!file.exists)
			{
				refuse("no " + path);
				return false;
			}
			// Guards: every refused existing file is protected, including the independent profile.
			struct read_attempt
			{
				std::string path;
				bool accepted = false;
				~read_attempt() { if (!accepted) write_refusals.refuse(path,pending_reason); }
			} attempt{path};
			std::string key_error;
			const auto runtime = save_content_guards::runtime_failure();
			if (!runtime.empty())
			{
				refuse(runtime);
				return false;
			}
			if (file.info.status == file_status::damaged)
			{
				refuse(path + " is damaged (" + file.info.why + ") and has no usable backup");
				return false;
			}
			if (file.info.status == file_status::needs_newer)
			{
				refuse("this save needs a newer version of XML2 Fix (" + file.info.why + ")");
				return false;
			}
			if (file.info.status == file_status::wrong_kind)
			{
				refuse(path + " is " + file.info.why);
				return false;
			}
			if (!save_content_guards::verify_keys(file.bytes,key_error))
			{
				refuse(path + ": " + key_error);
				return false;
			}
			auto result = kind == kind_profile ? lcsave::new_to_old_profile(file.bytes, tables) : lcsave::new_to_old_stream(file.bytes, tables);
			if (!result.ok)
			{
				refuse("this save can't be loaded by this build: " + result.error);
				return false;
			}
			if (result.bytes.size() != old_envelope + capacity)
			{
				refuse("the library produced " + std::to_string(result.bytes.size()) + " bytes for a " + std::to_string(old_envelope + capacity) + "-byte image");
				return false;
			}
			if (kind == kind_campaign)
			{
				std::string error;
				std::vector<lcsave::ZoneCaptureMetadata> metadata;
				if (!available_capacity(result.bytes,error) || !lcsave::loaded_zone_metadata(file.bytes,tables,metadata,error))
				{refuse("native preflight: "+error);return false;}
				pending_captures=std::move(metadata);
				if (!add_missing_talents(result.bytes))
				{refuse("required shared talents do not fit the native hero heap; load refused");return false;}
			}
			std::memcpy(data, result.bytes.data() + old_envelope, capacity);
			*size = static_cast<std::uint32_t>(capacity);
			overflow_flag = 0;
			if (kind == kind_campaign)
			{
				// CORE: stage retention; card-buffer success is not native load completion.
				pending_campaign.loaded = true;
				pending_campaign.file = file.bytes;
				pending_campaign.serial = file.info.campaign_serial;
				pending_campaign.sequence = file.info.save_sequence;
				pending_campaign.play_time = file.info.play_time;
				pending_campaign.slot = slot;
				core_lifetime.stage_load();
			}
			if (kind==kind_profile) retained_profile=file.bytes; // CORE: independent profile retention
			attempt.accepted = true;
			// A translation is not native load completion; Core confirms success through confirm_load_success().
			reads.fetch_add(1);
			// CORE-BEGIN: successful translation can still discard zones or reset conversation bits.
			save_core_rules::forward_load_ledger(result.ledger,notice,[](const std::string& line)
			{logger::write("save core: load: %s",line.c_str());});
			// CORE-END
			logger::write("save format: read %s (%zu bytes, sequence %lu, origin %u%s%s)", file.path.c_str(), file.bytes.size(), static_cast<unsigned long>(file.info.save_sequence),
			              file.info.origin, file.from_backup ? ", from the backup" : "", result.parked ? (", " + std::to_string(result.parked) + " parked").c_str() : "");
			return true;
		}

		std::uint32_t __fastcall card_write_profile(void* card, void*, const int device, const std::uint32_t size, const std::uint8_t* data)
		{
			store_device(card, device);
			ensure_ready();
			if (ready != readiness::ok)
			{
				refuse(failure);
				return 0;
			}
			if (size != profile_size || !data)
			{
				refuse("the game offered " + std::to_string(size) + " profile bytes, not " + std::to_string(profile_size));
				return 0;
			}
			return write_lc(kind_profile, -1, "Settings", data, size) ? 1 : 0;
		}

		std::uint32_t __fastcall card_read_profile(void* card, void*, const int device, std::uint32_t* size, std::uint8_t* data)
		{
			store_device(card, device);
			ensure_ready();
			if (ready != readiness::ok)
			{
				refuse(failure);
				return 0;
			}
			if (!size || !data || *size < profile_size)
			{
				refuse("the game offered no room for the profile");
				return 0;
			}
			return read_lc(kind_profile, -1, size, data) ? 1 : 0;
		}

		std::uint32_t __fastcall card_enumerate(void* card, void*, const int device, const int first, std::uint8_t* records, const int count)
		{
			store_device(card, device);
			ensure_ready();
			overflow_flag = 0;
			if (!records || count <= 0)
			{
				return 1;
			}
			for (int i = 0; i < count; ++i)
			{
				std::uint8_t* record = records + static_cast<std::size_t>(i) * slot_record_size;
				std::memset(record, 0, slot_record_size);
				record[slot_record_empty] = 1;
			}
			if (ready != readiness::ok)
			{
				// D11: an empty list, the message in the first record (loading it shows the cause).
				const auto text = notice_text(failure);
				std::memcpy(records, text.c_str(), text.size() + 1);
				records[slot_record_empty] = 0;
				notice_slot = first;
				return 1;
			}
			int highest_free = -1;
			for (int i = 0; i < count; ++i)
			{
				const int slot = first + i;
				if (slot < 0 || slot >= slot_count)
				{
					continue;
				}
				std::uint8_t* record = records + static_cast<std::size_t>(i) * slot_record_size;
				const auto file = load_lc(slot_path(slot), kind_campaign);
				if (!file.exists)
				{
					highest_free = slot;
					continue;
				}
				const auto text = slot_text(file.info, file.from_backup);
				std::memcpy(record, text.c_str(), text.size() + 1);
				record[slot_record_empty] = 0;
				record[slot_record_corrupt] = 0; // never: a flagged slot is offered for deletion
				const auto size = static_cast<std::uint32_t>(file.bytes.size());
				std::memcpy(record + slot_record_file_size, &size, 4);
				std::snprintf(reinterpret_cast<char*>(record + slot_record_size_text), 0x20, "%lu KB", static_cast<unsigned long>((size + 1023) / 1024));
				if (file.info.status != file_status::ok)
				{
					logger::write("save format: slot %d: %s is %s (%s)", slot, file.path.c_str(),
					              file.info.status == file_status::needs_newer ? "from a newer version" : file.info.status == file_status::damaged ? "damaged" : "not a campaign",
					              file.info.why.c_str());
				}
			}
			notice_slot = -1;
			if (!notice.empty() && highest_free >= 0)
			{
				std::uint8_t* record = records + static_cast<std::size_t>(highest_free - first) * slot_record_size;
				const auto text = notice_text(notice);
				std::memcpy(record, text.c_str(), text.size() + 1);
				record[slot_record_empty] = 0;
				notice_slot = highest_free;
			}
			return 1;
		}

		std::uint32_t __fastcall card_write_slot(void* card, void*, const int device, const int slot, const char* description, const std::uint32_t size, const std::uint8_t* data, int)
		{
			store_device(card, device);
			ensure_ready();
			if (ready != readiness::ok)
			{
				refuse(failure);
				return 0;
			}
			if (slot < 0 || slot >= slot_count || size != stream_capacity || !data)
			{
				refuse("the game asked to write slot " + std::to_string(slot) + " with " + std::to_string(size) + " bytes");
				return 0;
			}
			if (slot == notice_slot)
			{
				notice.clear();
				notice_slot = -1;
			}
			return write_lc(kind_campaign, slot, description, data, size) ? 1 : 0;
		}

		std::uint32_t __fastcall card_read_slot(void* card, void*, const int device, const int slot, std::uint32_t* size, std::uint8_t* data)
		{
			store_device(card, device);
			ensure_ready();
			if (ready != readiness::ok)
			{
				refuse(failure);
				return 0;
			}
			if (slot == notice_slot)
			{
				refuse(notice.empty() ? failure : notice);
				return 0;
			}
			if (slot < 0 || slot >= slot_count || !size || !data || *size < stream_capacity)
			{
				refuse("the game asked to read slot " + std::to_string(slot) + " into " + std::to_string(size ? *size : 0) + " bytes");
				return 0;
			}
			return read_lc(kind_campaign, slot, size, data) ? 1 : 0;
		}

		std::uint32_t __fastcall card_delete_slot(void* card, void*, const int device, const int slot)
		{
			store_device(card, device);
			ensure_ready();
			if (ready != readiness::ok)
			{
				refuse(failure);
				return 0;
			}
			if (slot == notice_slot)
			{
				notice.clear();
				notice_slot = -1;
				logger::write("save format: the notice slot was dismissed");
				return 1;
			}
			if (slot < 0 || slot >= slot_count)
			{
				return 0;
			}
			const std::string path = slot_path(slot);
			if (delete_action_for(reinterpret_cast<address>(_ReturnAddress())) == delete_action::keep_for_write)
			{
				// The save sequence's delete before an overwrite: the write that follows keeps this file as the backup.
				logger::write("save format: slot %d: the game's delete before an overwrite - %s kept until the write replaces it (the old file becomes its .bak)", slot,
				              path.c_str());
				return 1;
			}
			bool ok = true;
			for (const auto& p : {path, path + ".bak"})
			{
				if (file_exists(p) && !DeleteFileA(p.c_str()))
				{
					ok = false;
					refuse("can't delete " + p + " (error " + std::to_string(GetLastError()) + ")");
				}
			}
			if (ok)
			{
				write_refusals.explicit_delete(path);
				logger::write("save format: slot %d deleted (%s and its backup)", slot, path.c_str());
				if (current.slot == slot)
				{
					current.slot = -1;
				}
			}
			return ok ? 1 : 0;
		}

		char* __fastcall card_message(void* card, void*, const int code, const int device, const int argument)
		{
			int shown = code;
			if (!pending_reason.empty())
			{
				int mode = -1;
				if (!try_copy(&mode, in_game(save_load_menu + save_load_mode), 4))
				{
					mode = -1;
				}
				shown = message_code_for(code, mode, ready == readiness::failed);
				logger::write("save format: the menu (mode %d) shows message %d%s with the reason", mode, shown,
				              shown != code ? (" (the game asked for " + std::to_string(code) + ", a save failure, on Load Game)").c_str() : "");
			}
			char* text = game_message(card, nullptr, shown, device, argument);
			if (!pending_reason.empty() && text)
			{
				const auto with = message_with_reason(text, pending_reason, card_message_size);
				std::memcpy(text, with.c_str(), with.size() + 1);
				pending_reason.clear();
			}
			return text;
		}

		// ---- the placement file's bytes (N14 c) ------------------------------------------------------------

		// The reader's buffer and name, read under SEH; "" / null when they can't be.
		bool reader_fields(const void* reader, const std::uint8_t*& buffer, const char*& name)
		{
			std::uint8_t fields[8]{};
			if (!try_copy(fields, reader, 8))
			{
				return false;
			}
			std::memcpy(&buffer, fields, 4);
			std::memcpy(&name, fields + 4, 4);
			return true;
		}

		bool sha1_of_game_memory(const std::uint8_t* buffer, const std::uint32_t size, std::string& out)
		{
			std::vector<std::uint8_t> copy(size);
			if (!try_copy(copy.data(), buffer, size))
			{
				return false;
			}
			out = sha1_hex(copy.data(), copy.size());
			return !out.empty();
		}

		void __cdecl xmlb_hook(void* reader, const std::uint32_t size)
		{
			if (!zone_open_flag)
			{
				return;
			}
			zone_open_flag = 0; // one file per zone open
			const std::uint8_t* buffer = nullptr;
			const char* name = nullptr;
			if (!reader_fields(reader, buffer, name) || !buffer || size == 0 || size > lc_max_file)
			{
				return;
			}
			char name_copy[0x200]{};
			if (name)
			{
				for (std::size_t i = 0; i + 1 < sizeof name_copy; ++i)
				{
					if (!try_copy(name_copy + i, name + i, 1) || !name_copy[i])
					{
						break;
					}
				}
			}
			zone_opened_name = name_copy;
			zone_opened_size = size;
			if (!sha1_of_game_memory(buffer, size, zone_opened_sha1))
			{
				logger::write("save format: zone %s: the placement bytes couldn't be hashed", zone_opened_name.c_str());
				return;
			}
			std::string zone = zone_opened_name;
			for (auto& c : zone)
			{
				c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			if (zone.rfind("maps/", 0) == 0)
			{
				zone = zone.substr(5);
			}
			// The reader stores the name it resolved (".xmlb" / ".engb" appended); the tables know the zone by its path.
			for (const char* ext : {".xmlb", ".engb"})
			{
				const std::size_t n = std::strlen(ext);
				if (zone.size() > n && zone.compare(zone.size() - n, n, ext) == 0)
				{
					zone.resize(zone.size() - n);
					break;
				}
			}
			// CORE: this is the actual opened buffer; the loop below fills actual placement associations.
			active_visit={};active_visit.zone=zone;active_visit.capture_flags=0;visit_restore_disabled=false;restore_record=nullptr;
			visit_known=zone_opened_sha1.size()==40;
			if (visit_known) for(std::size_t i=0;i<20;++i)
				active_visit.placement_sha1[i]=static_cast<std::uint8_t>(std::strtoul(zone_opened_sha1.substr(i*2,2).c_str(),nullptr,16));
			lcsave::Sha1Digest expected{};
			zone_tables_sha1.clear();
			if (ready == readiness::ok && lcsave::zone_file_sha1(tables, zone, expected))
			{
				zone_tables_sha1 = hex(expected.data(), expected.size());
			}
			zones_hashed.fetch_add(1);
			const bool agree = !zone_tables_sha1.empty() && zone_tables_sha1 == zone_opened_sha1;
			if (!agree)
			{
				zone_hash_mismatches.fetch_add(1);
			}
			logger::write("save format: zone %s: the engine opened %lu bytes, SHA-1 %s; the tables say %s%s", zone.c_str(), static_cast<unsigned long>(size), zone_opened_sha1.c_str(),
			              zone_tables_sha1.empty() ? "(no entry)" : zone_tables_sha1.c_str(), agree ? " - same" : " - DIFFERENT (zone records of this zone would be refused)");
		}

		// ---- pipe jobs on the game's thread ------------------------------------------------------------------

		std::mutex job_mutex;
		std::string job_kind;   // "dump" or "check"; empty = none
		std::string job_path;
		std::string job_result;
		HANDLE job_done = nullptr;

		// The serializer run under SEH: false when it faulted.
		bool run_serializer(std::uint8_t* base)
		{
			__try
			{
				void* game = reinterpret_cast<game_getter_t>(in_game(game_getter))();
				const auto* vtable = at<const std::uint8_t*>(game, 0);
				at<serialize_t>(vtable, 0x208)(game, nullptr, base);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The live game as an old image, through its own serializer into a buffer laid out as CSaveLoad lays it out.
		bool serialize_live(std::vector<std::uint8_t>& image, std::string& error)
		{
			std::vector<std::uint8_t> buffer(stream_wrapper);
			std::uint8_t* base = buffer.data();
			std::memcpy(base + stream_capacity, &base, 4);
			base[stream_description] = 0;
			base[0x2fc44] = 0;
			overflow_flag = 0;
			if (!run_serializer(base))
			{
				error = "the serializer faulted";
				return false;
			}
			if (overflow_flag)
			{
				overflow_flag = 0;
				error = "the serializer ran out of room";
				return false;
			}
			image = old_image(reinterpret_cast<const char*>(base + stream_description), base, stream_capacity);
			return true;
		}

		std::string run_job(const std::string& kind, const std::string& path)
		{
			if (ready != readiness::ok)
			{
				return "error " + (failure.empty() ? std::string("the LC mode isn't ready (no file operation yet)") : failure);
			}
			std::vector<std::uint8_t> image;
			std::string error;
			if (!serialize_live(image, error))
			{
				return "error " + error;
			}
			lcsave::NewFileContext context;
			context.description = std::string(reinterpret_cast<const char*>(image.data()), strnlen(reinterpret_cast<const char*>(image.data()), 0x7f));
			context.slot = 0xff;
			context.saved_at = now_filetime();
			context.writer_version = writer_version(FIX_VERSION_MAJOR, FIX_VERSION_MINOR, FIX_VERSION_PATCH, writer_kind_fix);
			context.writer = writer_name;
			context.campaign_serial = current.loaded ? current.serial : 0;
			context.save_sequence = current.loaded ? current.sequence : 0;
			context.origin=campaign_origin;context.zone_captures=cached_captures;context.require_capture_metadata=true;
			context.retained_file=current.file; // CORE: dump reflects preservation too
			const auto result = lcsave::old_stream_to_new(image, tables, context);
			if (!result.ok)
			{
				return "error the live game couldn't be expressed in the LC format: " + result.error;
			}
			if (kind == "dump")
			{
				if (!write_atomically(path, result.bytes, error))
				{
					return "error " + error;
				}
				return "ok " + std::to_string(result.bytes.size()) + " bytes " + path;
			}
			// check: the live game's dump against the file last loaded, line by line (META differs by design).
			if (!current.loaded)
			{
				return "error no LC file was loaded in this session";
			}
			std::string e1, e2;
			const std::string live = lcsave::dump(result.bytes, tables, e1);
			const std::string file = lcsave::dump(current.file, tables, e2);
			if (live.empty() || file.empty())
			{
				return "error dump: " + (live.empty() ? e1 : e2);
			}
			// Line multisets: an inserted record shifts every later line, so lines are compared as sets (a line
			// only the live dump has, a line only the file's has), the volatile META lines left out.
			const auto lines = [](const std::string& text)
			{
				std::vector<std::string> out;
				std::size_t at = 0;
				while (at < text.size())
				{
					auto end = text.find('\n', at);
					if (end == std::string::npos)
					{
						end = text.size();
					}
					auto line = text.substr(at, end - at);
					const auto first = line.find_first_not_of(" \t");
					const auto body = first == std::string::npos ? std::string() : line.substr(first);
					if (body.rfind("\"description\":", 0) != 0 && body.rfind("\"play_time\":", 0) != 0 && body.rfind("\"saved_at\":", 0) != 0 &&
					    body.rfind("\"writer\":", 0) != 0 && body.rfind("\"slot\":", 0) != 0 && body.rfind("\"origin\":", 0) != 0)
					{
						out.push_back(line);
					}
					at = end + 1;
				}
				std::sort(out.begin(), out.end());
				return out;
			};
			const auto la = lines(live);
			const auto lb = lines(file);
			std::vector<std::string> only_live, only_file;
			std::set_difference(la.begin(), la.end(), lb.begin(), lb.end(), std::back_inserter(only_live));
			std::set_difference(lb.begin(), lb.end(), la.begin(), la.end(), std::back_inserter(only_file));
			if (!only_live.empty() || !only_file.empty())
			{
				const std::string first = (only_live.empty() ? only_file[0] : only_live[0]).substr(0, 120);
				logger::write("save format: check: the live game differs from the loaded file: %zu dump line(s) only live, %zu only in the file (of %zu / %zu); first: %s",
				              only_live.size(), only_file.size(), la.size(), lb.size(), first.c_str());
				for (std::size_t i = 0; i < only_live.size() && i < 20; ++i)
				{
					logger::write("save format: check:   live only: %s", only_live[i].substr(0, 200).c_str());
				}
				for (std::size_t i = 0; i < only_file.size() && i < 20; ++i)
				{
					logger::write("save format: check:   file only: %s", only_file[i].substr(0, 200).c_str());
				}
				return "ok differs live-only " + std::to_string(only_live.size()) + " file-only " + std::to_string(only_file.size()) + " first " + first;
			}
			logger::write("save format: check: the live game's dump equals the loaded file's (%zu lines, META's volatile fields left out)", la.size());
			return "ok equal " + std::to_string(la.size()) + " lines";
		}
	}

	void pump()
	{
		if (!enabled)
		{
			return;
		}
		std::string kind, path;
		{
			std::lock_guard lock(job_mutex);
			if (job_kind.empty())
			{
				return;
			}
			kind = job_kind;
			path = job_path;
		}
		const auto result = run_job(kind, path);
		{
			std::lock_guard lock(job_mutex);
			job_result = result;
			job_kind.clear();
		}
		SetEvent(job_done);
	}

	std::string pipe(const std::string& arguments)
	{
		std::string verb = arguments.substr(0, arguments.find(' '));
		std::string rest = arguments.size() > verb.size() ? arguments.substr(verb.size() + 1) : "";
		for (auto& c : verb)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (verb == "status")
		{
			return "ok " + status();
		}
		if (!enabled)
		{
			return "error the LC save mode is off ([Save] Format isn't lc, or a guard didn't match)";
		}
		if (verb != "dump" && verb != "check")
		{
			return "error save takes status, dump <file.lcsave> or check";
		}
		if (verb == "dump" && rest.empty())
		{
			return "error save dump needs a file path";
		}
		if (!job_done)
		{
			job_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		}
		{
			std::lock_guard lock(job_mutex);
			if (!job_kind.empty())
			{
				return "error a save job is still running";
			}
			job_kind = verb;
			job_path = rest;
			job_result.clear();
		}
		if (WaitForSingleObject(job_done, 10000) != WAIT_OBJECT_0)
		{
			std::lock_guard lock(job_mutex);
			job_kind.clear();
			return "error the game didn't run the job within 10 s (is it reading its keyboard?)";
		}
		std::lock_guard lock(job_mutex);
		return job_result;
	}

	std::string status()
	{
		std::string state = !enabled ? "off" : ready == readiness::untried ? "lc (tables not loaded yet)" : ready == readiness::ok ? "lc" : "lc DISABLED: " + failure;
		return "save format " + state + "; reads " + std::to_string(reads.load()) + "; writes " + std::to_string(write_count.load()) + "; refusals " + std::to_string(refusals.load()) +
		       "; core captures " + std::to_string(captures_seen.load()) + "; numbered " + std::to_string(numbered_seen.load()) + "; capture failures " + std::to_string(capture_failures.load()) + "; identity resets " + std::to_string(identity_resets.load()) + "; campaign resets " + std::to_string(resets_seen.load()) + "; native completions " + std::to_string(native_loads_completed.load()) + "; W11 open; campaign writes " + (core_lifetime.may_save()?"allowed":"blocked") +
		       "; imported " + std::to_string(converted.load()) + "; talents added " + std::to_string(talents_added.load()) + "; zones hashed " + std::to_string(zones_hashed.load()) +
		       " (" + std::to_string(zone_hash_mismatches.load()) + " differ from the tables; last " + (zone_opened_name.empty() ? "none" : zone_opened_name + " " + zone_opened_sha1) + ")" + save_trace::status();
	}

	// Guards integration: Core calls this only after native load/application has succeeded.
	void confirm_load_success(const std::uint8_t kind, const int slot)
	{
		if (ready == readiness::ok && (kind == kind_profile || (kind == kind_campaign && slot >= 0 && slot < slot_count)))
			write_refusals.successful_load(kind == kind_profile ? lc_dir + profile_file : slot_path(slot));
	}

	bool startup_allowed()
	{
		return installation_failure.empty();
	}

	const std::string& startup_error()
	{
		return installation_failure;
	}

	void install(const HMODULE game) try
	{
		const auto text = ini::text(L"Save", L"Format").value_or("");
		const auto chosen = parse_mode(text);
		if (!chosen.error.empty())
		{
			reject_install("invalid [Save] Format=" + text + ": " + chosen.error);
			return;
		}
		if (!chosen.set || chosen.value == mode::off)
		{
			return; // no key: the game's own format, nothing patched
		}
		// Default denial also covers any future early return before successful installation.
		installation_failure = "required LC save/trivia hooks were not installed";
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			reject_install("XMen2.exe is not loaded at the required image base 0x400000");
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			reject_install(install_guard_error(*g, "LC save/trivia guard mismatch"));
			return;
		}

		// CORE-BEGIN: capture/reset guard validation before installing any Core patch.
		for(const auto& g:save_core_rules::guards)
		{
			std::vector<std::uint8_t> bytes(limits_rules::hex_size(g.hex));
			if(!try_copy(bytes.data(),in_game(g.va),bytes.size()) || !limits_rules::matches(bytes.data(),g.hex))
			{reject_install(install_guard_error(g, "LC capture guard mismatch"));return;}
		}
		{
			const auto& g=save_core_rules::ordinal_write_guard;
			std::vector<std::uint8_t> bytes(limits_rules::hex_size(g.hex));
			if(!try_copy(bytes.data(),in_game(g.va),bytes.size()) || !limits_rules::matches(bytes.data(),g.hex))
			{reject_install(install_guard_error(g, "LC ordinal guard mismatch"));return;}
		}
		// CORE-END

		// The stubs and bridges go in one block of the fix's own, made executable once written.
		std::vector<std::uint8_t> block;
		std::vector<address> stub_at(flag_sites.size());
		constexpr std::size_t block_reserve = 0x1000;
		auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, block_reserve, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!memory)
		{
			reject_install("cannot allocate LC stubs (Windows error " + std::to_string(GetLastError()) + ")");
			return;
		}
		const address base = address_of(memory);
		const address flag = address_of(const_cast<std::uint8_t*>(&overflow_flag));
		std::vector<write> writes;
		writes.push_back({0x46a690, jump(0x46a690, address_of(&trivia_cursor_get), 12)});
		writes.push_back({0x46a6a0, jump(0x46a6a0, address_of(&trivia_cursor_set), 15)});
		writes.push_back({0x469e10, jump(0x469e10, address_of(&trivia_get), 7)});
		writes.push_back({0x469e60, jump(0x469e60, address_of(&trivia_set), 8)});
		writes.push_back({0x55f190, jump(0x55f190, address_of(&card_count_campaigns), 6)});
		for (std::size_t i = 0; i < flag_sites.size(); ++i)
		{
			stub_at[i] = base + static_cast<address>(block.size());
			const auto stub = flag_stub(flag_sites[i], stub_at[i], flag);
			block.insert(block.end(), stub.begin(), stub.end());
			writes.push_back({flag_sites[i].va, jump(flag_sites[i].va, stub_at[i], limits_rules::hex_size(flag_sites[i].hex))});
		}
		const address xmlb_at = base + static_cast<address>(block.size());
		const auto xb = xmlb_bridge(xmlb_at, address_of(&xmlb_hook));
		block.insert(block.end(), xb.begin(), xb.end());
		const address zone_at = base + static_cast<address>(block.size());
		const auto zb = zone_open_bridge(zone_at, address_of(const_cast<std::uint8_t*>(&zone_open_flag)), address_of(const_cast<std::uint32_t*>(&zone_open_return)));
		block.insert(block.end(), zb.begin(), zb.end());
		// CORE-BEGIN: independent guarded bridges; no edits to Guards' atomic wrapper.
		const address number_at=base+static_cast<address>(block.size());
		save_core_rules::append(block,save_core_rules::numbering_bridge(number_at,address_of(&numbering_hook)));
		writes.push_back({0x48624c,jump(0x48624c,number_at,7)});
		writes.push_back({0x486260,call(0x486260,address_of(&restore_hook),5)});
		const address reset_at=base+static_cast<address>(block.size());
		save_core_rules::append(block,save_core_rules::reset_bridge(reset_at,address_of(&campaign_reset_hook)));
		writes.push_back({0x5f2e70,jump(0x5f2e70,reset_at,6)});
		const address capture_at=base+static_cast<address>(block.size());
		save_core_rules::append(block,save_core_rules::capture_trampoline(capture_at));
		retail_capture=reinterpret_cast<capture_t>(in_game(capture_at));
		writes.push_back({0x4664d0,jump(0x4664d0,address_of(&capture_hook),5)});
		for(const auto& site:save_core_rules::capture_bounds)
		{
			const address bound_at=base+static_cast<address>(block.size());
			save_core_rules::append(block,save_core_rules::bound_bridge(site,bound_at,address_of(&capture_overflow)));
			writes.push_back({site.va,jump(site.va,bound_at,site.length)});
		}
		const address ordinal_at=base+static_cast<address>(block.size());
		save_core_rules::append(block,save_core_rules::ordinal_write_bridge(ordinal_at,address_of(&capture_overflow)));
		writes.push_back({0x46667a,jump(0x46667a,ordinal_at,8)});
		writes.push_back({game_vtable+0x20c,dword_bytes(address_of(&native_load_hook))});
		// CORE-END
		if (block.size() > block_reserve)
		{
			reject_install("LC stubs exceed their allocated block");
			VirtualFree(memory, 0, MEM_RELEASE);
			return;
		}
		std::memcpy(memory, block.data(), block.size());
		DWORD old = 0;
		if (!VirtualProtect(memory, block_reserve, PAGE_EXECUTE_READ, &old))
		{
			reject_install("cannot make LC stubs executable (Windows error " + std::to_string(GetLastError()) + ")");
			VirtualFree(memory, 0, MEM_RELEASE);
			return;
		}
		if (!FlushInstructionCache(GetCurrentProcess(), memory, block.size()))
		{
			reject_install("cannot flush LC stub instructions (Windows error " + std::to_string(GetLastError()) + ")");
			VirtualFree(memory, 0, MEM_RELEASE);
			return;
		}
		writes.push_back({xmlb_convert_call_insn, call(xmlb_convert_call_insn, xmlb_at, 5)});
		writes.push_back({zone_open_call, call(zone_open_call, zone_at, 5)});

		game_message = reinterpret_cast<message_t>(in_game(0x55e9b0));
		const std::array<std::pair<address, const void*>, 9> methods{{
			{0x28, reinterpret_cast<const void*>(&card_delete_all)},
			{0x2c, reinterpret_cast<const void*>(&card_profile_exists)},
			{0x54, reinterpret_cast<const void*>(&card_write_profile)},
			{0x58, reinterpret_cast<const void*>(&card_read_profile)},
			{0x5c, reinterpret_cast<const void*>(&card_enumerate)},
			{0x64, reinterpret_cast<const void*>(&card_write_slot)},
			{0x6c, reinterpret_cast<const void*>(&card_read_slot)},
			{0x70, reinterpret_cast<const void*>(&card_delete_slot)},
			{0x74, reinterpret_cast<const void*>(&card_message)},
		}};
		for (const auto& [offset, function] : methods)
		{
			writes.push_back({card_vtable + offset, dword_bytes(address_of(function))});
		}
		if (!patch(writes))
		{
			reject_install("cannot install the required LC card/trivia patches");
			VirtualFree(memory, 0, MEM_RELEASE);
			return;
		}
		enabled = true;
		installation_failure.clear();
		logger::write("save format: guarded trivia getter/setter and independent per-act cursors installed; TRIV bind rule 2 uses compact native bits; rule 1 retains legacy positions");
		logger::write("save core: guarded actual numbering/capture/reset and load lifetime installed; capture masks 1/2; W11 open");
		logger::write("save format: LC ([Save] Format=lc): the card's nine file methods (vtable 0x%08lX) are the fix's - saves are read and written as .lcsave files in SaveLC "
		              "through the LC library; the old Save folder is converted once at the first file operation and never written; %zu stream-skip sites flagged; the zone "
		              "placement file hashed at 0x%08lX / 0x%08lX",
		              static_cast<unsigned long>(card_vtable), flag_sites.size(), static_cast<unsigned long>(zone_open_call), static_cast<unsigned long>(xmlb_convert_call_insn));
	}
	catch (const std::exception& error)
	{
		reject_install(std::string("LC hook installation failed: ") + error.what());
	}
}
