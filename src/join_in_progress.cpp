#include "e4_observe.hpp"
#include "join_in_progress.hpp"
#include "join_in_progress_core.hpp"
#include "join_in_progress_rules.hpp"
#include "game_version.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "gns_transport.hpp"
#include "entity_capacity.hpp"
#include "entity_transfer.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "save_format.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace join_in_progress
{
	namespace
	{
		using namespace join_in_progress_rules;
		using join_in_progress_core::action;
		using join_in_progress_core::stage;

		std::atomic<int> state{-1};
		bool trace = false; // [Dev] JoinInProgressTrace=1
		bool area_load = false; // latched at the synchronous zone-loader boundary, consumed by the pump afterwards
		bool composed_receiver = false;
		bool deferred_start_running = false;
		bool existing_cohort_member = false;
		std::uint32_t deferred_start_task = 0;
		join_in_progress_core::machine machine; // the game's thread only (bridges, receive pump, load tick)
		join_in_progress_core::cohort epoch;

		const std::uint32_t original_teardown = gamespy_thunk, add_resume_at = add_resume, frame_resume_at = frame_walk_resume,
			frame_exit_at = frame_walk_exit, stream_resume_at = stream_walk_resume, stream_exit_at = stream_walk_exit,
			wait_at = wait, natneg_resume_at = natneg_reply_resume, natneg_skip_at = natneg_reply_skip,
			controllers_at = controllers_getter, record_controller_at = record_controller, admit_resume_at = admit_controller_resume,
			players_at = players_getter, checksum_send_at = checksum_send_resume,
			checksum_compare_at = checksum_compare_resume, campaign_receive_at = campaign_receive_resume,
			campaign_begin_at = campaign_begin_resume, game_getter_at = game_getter, broadcast_at = broadcast_resume,
			seats_at = seats_getter, record_seats_resume_at = record_seats_resume, record_seats_exit_at = record_seats_exit,
			set_flags_at = set_flags, ready_clear_resume_at = ready_clear_resume, ready_clear_skip_at = ready_clear_skip, clear_flags_at = clear_flags;

		template<class T> T& at(const void* base, std::uint32_t offset) { return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(base) + offset); }
		template<class F> F fn(std::uint32_t va) { return reinterpret_cast<F>(static_cast<std::uintptr_t>(va)); }
		using getter_t = void*(__cdecl*)();
		using index_t = int(__thiscall*)(const void*);
		using add_t = void(__thiscall*)(void*, void*);
		using init_t = void(__thiscall*)(void*);
		using send_t = char(__thiscall*)(void*, void*, void*, int);
		using connection_t = void*(__thiscall*)(void*);
		using mode_t = std::uint32_t(__thiscall*)(void*);
		using set_mode_t = void(__thiscall*)(void*, std::uint32_t);
		using campaign_start_t = char(__thiscall*)(void*);
		using actual_start_t = void(__cdecl*)(int);
		using native_load_t = void(__thiscall*)(void*, void*, int);
		using network_setup_t = void(__thiscall*)(void*, std::uint32_t);
		using flag_t = void(__thiscall*)(void*, std::uint32_t);
		using zone_type_t = std::uint8_t(__thiscall*)(void*);
		using task_add_t = std::uint32_t(__thiscall*)(void*, std::uint32_t, void*, std::uint32_t);
		using task_remove_t = void(__thiscall*)(void*, std::uint32_t);
		using image_bit_t = void(__thiscall*)(void*, char);
		using alloc_t = void*(__cdecl*)(std::uint32_t, std::uint32_t);
		using free_t = void(__cdecl*)(void*);
		using natneg_t = void*(__thiscall*)(void*, void*, std::uint32_t);
		using loaded_t = char(__thiscall*)(void*);
		using barrier_t = void(__thiscall*)(void*, float);
		using zone_flag_t = bool(__fastcall*)(void*, void*);
		using assign_t = std::int32_t(__thiscall*)(void*, std::int32_t, char);
		using set_controller_t = void(__thiscall*)(void*, std::int32_t);
		using player_added_t = char(__thiscall*)(void*, void*, std::int32_t);
		using message_t = void(__thiscall*)(void*, const char*, std::int32_t, float);
		using entity_of_t = void*(__thiscall*)(const std::uint32_t*);
		using zone_name_t = const char*(__fastcall*)(void*, void*);
		using seat_binding_t = void(__thiscall*)(void*, std::int32_t, std::int32_t);
		using name_text_t = const char*(__thiscall*)(const std::uint32_t*);
		using seat_active_t = char(__thiscall*)(void*, std::int32_t);

		const join_in_progress_rules::guard* mismatch()
		{
			__try
			{
				const auto* image = reinterpret_cast<const std::uint8_t*>(0x400000);
				const auto target = entity_capacity::installed_target(campaign_receive);
				composed_receiver = receiver_composed(image + campaign_receive - 0x400000, target);
				for (const auto& g : guards)
				{
					if (g.va == campaign_receive && composed_receiver) continue;
					if (!limits_rules::matches(image + g.va - 0x400000, g.hex)) return &g;
				}
				return nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return &guards[0]; }
		}

		void* session() { return fn<getter_t>(session_getter)(); }
		void* manager() { return fn<getter_t>(manager_getter)(); }
		int index_of(const void* record) { return fn<index_t>(record_index)(record); }
		// The participant list without the native walk's cursor side effect (0x611160 writes +0x274).
		void* first(void* s) { return s && at<std::uint32_t>(s, 0x278) ? at<void*>(s, 0x26c) : nullptr; }
		void* local_record(void* s)
		{
			for (void* n = first(s); n; n = at<void*>(n, node_next))
				if (at<std::uint32_t>(at<void*>(n, node_record), record_flags) & flag_local) return at<void*>(n, node_record);
			return nullptr;
		}
		bool listed(void* s, const void* record)
		{
			for (void* n = first(s); n; n = at<void*>(n, node_next)) if (at<void*>(n, node_record) == record) return true;
			return false;
		}
		bool host(void* s) { const void* local = local_record(s); return local && index_of(local) == 0; }
		bool zone_load_pending()
		{
			void* zones = fn<getter_t>(zones_getter)();
			if (!zones || at<std::uint32_t>(zones, 0) != zones_vtable) return false;
			return fn<zone_flag_t>(at<std::uint32_t>(at<void*>(zones, 0), zones_loading_slot))(zones, nullptr);
		}

		// One reliable message (the native 0x14-byte message: body, length +8, reliable +0xe, opcode +0x10) to one peer.
		bool send(const void* record, std::uint8_t opcode, std::uint8_t* body, std::uint16_t length)
		{
			alignas(4) std::uint8_t message[0x20]{};
			e4_observe::native_call("jip:message init", [&] { fn<init_t>(message_init)(message); });
			at<std::uint8_t*>(message, 0) = body;
			at<std::uint16_t>(message, 8) = length;
			at<std::uint8_t>(message, 0xe) = 1;
			at<std::uint8_t>(message, 0x10) = opcode;
			void* peer = at<void*>(record, record_peer);
			if (!peer) return false;
			void* s = session();
            void* connection = e4_observe::native_call("jip:peer connection", [&] { return fn<connection_t>(peer_connection)(peer); });
            return e4_observe::native_call("jip:send", [&] { return fn<send_t>(send_to)(s, connection, message, 0); }) != 0;
		}
		void clear_frontend_callback(void* m)
		{
			// These pointers belonged to the pre-game menu, which has closed on existing players.
			at<std::uint32_t>(m, 0x2298) = 0;
			at<std::uint32_t>(m, 0x229c) = 0;
		}

		bool start_image(void*, const join_in_progress_core::seat& seat)
		{
			void* m = manager();
			if (at<std::uint32_t>(m, manager_sender))
			{
				logger::write("join in progress: a campaign sender is already running; cohort restart refused");
				return false;
			}
			const auto& profile = save_format::installed_profile();
			auto* buffer = static_cast<std::uint8_t*>(fn<alloc_t>(game_alloc)(profile.wrapper, 0x2d));
			if (!buffer) return false;
			std::memset(buffer, 0, profile.wrapper); // the sender includes the payload's unused padding
			if (!save_format::serialize_wrapper(buffer))
			{
				fn<free_t>(game_free)(buffer);
				logger::write("join in progress: the live campaign serializer failed");
				return false;
			}
			join_in_progress_core::rewind_campaign(buffer, profile.payload);
			void* game = fn<getter_t>(game_getter)();
			const auto get_mode = fn<mode_t>(at<std::uint32_t>(at<void*>(game, 0), mode_slot));
            const auto mode = e4_observe::native_call("jip:mode", [&] { return get_mode(game); });
			const auto set_mode = fn<set_mode_t>(at<std::uint32_t>(at<void*>(game, 0), 0x26c));
            e4_observe::native_call("jip:set mode", [&] { set_mode(game, join_in_progress_core::campaign_transfer_mode(mode)); });
			void* previous = at<void*>(m, manager_image);
			at<void*>(m, manager_image) = buffer;
			if (previous) fn<free_t>(game_free)(previous);
			clear_frontend_callback(m);
			at<std::uint8_t>(m, manager_difficulty) = 0; // this native byte selects saved-stats-only, not difficulty
			at<std::uint8_t>(m, 0) = 1; // native start cleanup must own/remove this transfer's sender task
			// Ownership passes to the native campaign manager, exactly as for Load Saved Campaign. Its sender,
			// J completion and 0x26 broadcast restart every participant from this one canonical image.
			if (!e4_observe::native_call("jip:campaign start", [&] { return fn<campaign_start_t>(native_campaign_start)(m); }))
			{
				at<void*>(m, manager_image) = nullptr;
				fn<free_t>(game_free)(buffer);
				return false;
			}
			logger::write("join in progress: player %d - live campaign image (%u bytes), native full-campaign transfer to the cohort", seat.index, unsigned(profile.payload));
			return true;
		}

		void finish_image(void*, const join_in_progress_core::seat& seat)
		{
			logger::write("join in progress: player %d has the campaign image; native completion starts the cohort after every image arrives", seat.index);
		}

		void give_up(void*, const join_in_progress_core::seat& seat)
		{
			logger::write("join in progress: player %d did not complete the cohort restart at %s", seat.index,
				join_in_progress_core::name(seat.at));
		}

		bool __cdecl checksum_hold() { return state.load() == 1 && epoch.active; }

		void __cdecl campaign_notice(const void* event)
		{
			if (at<std::uint8_t>(event, 0) != opcode_prepare) return;
			const void* body = at<const void*>(event, 4);
			if (!body || at<std::uint32_t>(body, 0) != 2) return;
			epoch.begin();
			existing_cohort_member = machine.started;
			void* m = manager();
			at<std::uint8_t>(m, 0) = 1;
			if (machine.started) clear_frontend_callback(m);
			logger::write("join in progress: full campaign image received; cohort checksum hold active");
		}

		void __cdecl deferred_campaign_start(void*);
		bool __cdecl campaign_start_notice()
		{
			if (epoch.active)
			{
				if (!deferred_start_running)
				{
					if (epoch.request())
					{
						deferred_start_task = fn<task_add_t>(task_add)(fn<getter_t>(scheduler_getter)(),
							static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&deferred_campaign_start)), nullptr, 0x7fffffff);
						logger::write("join in progress: cohort restart deferred out of packet delivery");
					}
					return false;
				}
				if (!epoch.start()) return false;
				void* s = session();
				if (host(s))
				{
					void* m = manager();
					if (void* local = local_record(s)) fn<image_bit_t>(set_image_done)(local, 0);
					if (const auto task = at<std::uint32_t>(m, manager_sender))
					{
						at<std::uint32_t>(m, manager_sender) = 0;
						fn<task_remove_t>(task_remove)(fn<getter_t>(scheduler_getter)(), task);
					}
					// Existing-world teardown can pump nested scheduler ticks before native cleanup reaches the
					// sender. Clear the local completion bit now so another J cannot broadcast another start.
				}
				logger::write("join in progress: native cohort campaign start entered");
			}
			return true;
		}
		void __cdecl deferred_campaign_start(void*)
		{
			if (epoch.active && epoch.requested && zone_load_pending()) return;
			const auto task = deferred_start_task;
			deferred_start_task = 0;
			if (task) fn<task_remove_t>(task_remove)(fn<getter_t>(scheduler_getter)(), task);
			if (!epoch.active || !epoch.requested || epoch.started) return;
			deferred_start_running = true;
			// Native time reset preserves the previous input frame's one-shot step. The next clock tick
			// promotes it over the fixed 30 Hz step, before the fresh input streams can replace it. Clear
			// both steps before native campaign setup installs 30 Hz, for the whole restarting cohort.
			fn<init_t>(clock_reset_step)(fn<getter_t>(clock_getter)());
			if (existing_cohort_member)
			{
				logger::write("join in progress: existing participant re-enters the native campaign start epoch");
				fn<network_setup_t>(network_setup)(session(), 0x10);
				void* pause = fn<getter_t>(pause_getter)();
				// Soft reset paused the input manager and marked records loading. Balance it before initialization
				// clears the pause object's state, otherwise native unpause sees "already unpaused" and does nothing.
				fn<flag_t>(pause_set)(pause, 0);
				for (void* n = first(session()); n; n = at<void*>(n, node_next))
					fn<flag_t>(clear_flags)(at<void*>(n, node_record), 0x10);
				// A reused session still has phase 4 and the old pause context initialized. Re-enter the native
				// campaign-loading epoch so all old peers register/start the same load barrier as the newcomer.
				at<std::uint32_t>(session(), 0x424) &= ~0x14u; // fresh campaign phases, without the soft-reset marker
				at<std::uint8_t>(pause, 0x64) = 0;
			}
			fn<actual_start_t>(campaign_begin)(0);
			deferred_start_running = false;
		}
		void __fastcall campaign_load_bridge(void* game, void*, void* buffer, int mode)
		{
			const int full = join_in_progress_core::campaign_restore_mode(mode, epoch.active && epoch.started);
			if (full != mode) logger::write("join in progress: cohort host restore mode %d -> %d", mode, full);
			// A warm newcomer may complete its native load between pump ticks. Observe the actual load call
			// rather than requiring a later poll to catch its transient loading flags.
			if (epoch.active && epoch.started) epoch.observe(true, true);
			fn<native_load_t>(at<std::uint32_t>(at<void*>(game, 0), 0x20c))(game, buffer, full);
		}

		void __cdecl broadcast_notice(const void* message)
		{
			if (!epoch.active || at<std::uint8_t>(message, 0x10) != opcode_start || !epoch.announce()) return;
			void* s = session();
			if (!host(s)) return;
			if (void* local = local_record(s)) fn<image_bit_t>(set_image_done)(local, 0);
			void* m = manager();
			if (const auto task = at<std::uint32_t>(m, manager_sender))
			{
				at<std::uint32_t>(m, manager_sender) = 0;
				fn<task_remove_t>(task_remove)(fn<getter_t>(scheduler_getter)(), task);
			}
			logger::write("join in progress: cohort start announced; native sender retired before world reset");
		}

		void observe_epoch()
		{
			if (!epoch.active) return;
			void* pause = fn<getter_t>(pause_getter)();
			void* menus = *reinterpret_cast<void**>(0x8aff18);
			void* menu = menus ? at<void*>(menus, 0x86090) : nullptr;
			const bool start_menu = menu && std::strcmp(reinterpret_cast<const char*>(menu) + 0xc, "start") == 0;
			const bool loading = zone_load_pending() || start_menu || (pause && at<std::uint8_t>(pause, 0x6c));
			if (existing_cohort_member && epoch.resume_controls(!loading))
			{
				// Existing-world teardown disables controllers after network setup enabled them. The first input
				// packet cannot be produced until the restored world has completed and native controls resume.
				fn<init_t>(controller_resume)(fn<getter_t>(controllers_getter)());
				logger::write("join in progress: existing participant's native controls resumed after world restore");
			}
			if (epoch.observe(loading, machine.holding()))
			{
				logger::write("join in progress: cohort synchronized; checksum protection resumed");
				// The native slow-player guard (0x559190: per participant index, count/phase pairs at 0x79fb80, a drop at
				// 900 slow input frames in phase 2) never resets them before it drops someone. A host that played alone,
				// or a cohort that waited out the restore, carried counts into the restarted session and the host dropped
				// itself right after the bring-in (live E15, 0x559341). The restart is a new measurement.
				std::memset(reinterpret_cast<void*>(slow_player_counts), 0, slow_player_bytes);
				logger::write("join in progress: the slow-player counters start again with the restored session");
			}
		}

		// One pass over the seats. loading: called from the host's load tick (its zone is loaded); pause: the pause object.
		void advance(void* s, bool loading, void* pause)
		{
			const auto now = GetTickCount();
			const bool pending_load = loading || zone_load_pending();
			for (auto& seat : machine.seats)
			{
				if (seat.at == stage::free) continue;
				if (!listed(s, seat.record))
				{
					logger::write("join in progress: player %d left while %s; the seat is free again", seat.index, join_in_progress_core::name(seat.at));
					seat = {};
					continue;
				}
				if (seat.at == stage::joined) { seat = {}; continue; }
				join_in_progress_core::observation o;
				o.zone_load_pending = pending_load;
				o.add_waiting = at<std::uint8_t>(manager(), manager_wait) != 0;
				o.image_done = epoch.started || (at<std::uint32_t>(seat.record, record_flags) & flag_image) != 0;
				o.loaded = pause && join_in_progress_core::participant_loaded(
					at<std::uint32_t>(pause, pause_loaded + seat.index * 4), epoch.started && epoch.saw_loading,
					*reinterpret_cast<std::uint16_t*>(0xa3bbf4));
				o.host_loaded = loading;
				const auto before = seat;
				switch (machine.step(seat, o, now))
				{
				case action::run_add:
					logger::write("join in progress: zone load - adding pending player %d (native add: 0x1f to everyone, record marked)", seat.index);
					{
						// Record creation normally does this before admission even returns. A late record kept
						// its AI hero untouched; replay those native side effects only at the shared area boundary.
						void* controllers = fn<getter_t>(controllers_getter)();
						const auto assign = fn<assign_t>(at<std::uint32_t>(at<void*>(controllers, 0), controller_assign_slot));
                        const int index = seat.index;
                        void* record = const_cast<void*>(seat.record);
                        const auto slot = e4_observe::native_call("jip:assign", [&] { return assign(controllers, index, 0); });
                        e4_observe::native_call("jip:record controller", [&] { fn<set_controller_t>(record_controller)(record, slot); });
                        void* seats = fn<getter_t>(seats_getter)();
                        const auto activate = fn<set_controller_t>(at<std::uint32_t>(at<void*>(seats, 0), seats_activate_slot));
                        e4_observe::native_call("jip:activate", [&] { activate(seats, index); });
                        const auto update = fn<init_t>(at<std::uint32_t>(at<void*>(seats, 0), seats_update_slot));
                        e4_observe::native_call("jip:update", [&] { update(seats); });
                        void* players = fn<getter_t>(players_getter)();
                        const auto added = fn<player_added_t>(at<std::uint32_t>(at<void*>(players, 0), player_added_slot));
                        e4_observe::native_call("jip:player added", [&] { added(players, record, 0); });
						logger::write("join in progress: player %d gets controller slot %d and is announced to the game", seat.index, int(slot));
					}
					{
                        void* m = manager();
                        void* record = const_cast<void*>(seat.record);
                        e4_observe::native_call("jip:add entry", [&] { fn<add_t>(add_entry)(m, record); });
                    }
					break;
				case action::send_image:
					if (!start_image(s, seat)) { give_up(s, before); seat = {}; }
					break;
				case action::send_start: finish_image(s, seat); break;
				case action::finish:
					logger::write("join in progress: player %d reached the shared start (%u ms after image completion)", seat.index, unsigned(now - before.since));
					break;
				case action::give_up: give_up(s, before); break;
				default: break;
				}
			}
		}

		bool guarded_advance(bool loading, void* pause)
		{
			__try
			{
				void* s = session();
				if (!s || !first(s)) { if (machine.started || machine.any()) machine.reset(); return false; }
				static DWORD last = 0;
				if (trace && machine.any() && GetTickCount() - last >= 2000)
				{
					last = GetTickCount();
					void* local = local_record(s);
					void* zones = fn<getter_t>(zones_getter)();
					logger::write("jip driver: loading=%d host=%d local=%p index=%d zones=%p vt=%08x expected=%08x pending=%d holding=%d",
						loading, host(s), local, local ? index_of(local) : -1, zones, zones ? at<std::uint32_t>(zones, 0) : 0,
						zones_vtable, zone_load_pending(), machine.holding());
					for (const auto& seat : machine.seats)
						if (seat.at != stage::free && listed(s, seat.record))
							logger::write("jip seat: index=%d stage=%s flags=%08x add_wait=%d loaded=%u", seat.index,
								join_in_progress_core::name(seat.at), at<std::uint32_t>(seat.record, record_flags),
								at<std::uint8_t>(manager(), manager_wait), pause ? at<std::uint32_t>(pause, pause_loaded + seat.index * 4) : 0);
				}
				if (!machine.any() || !host(s)) return false;
				advance(s, loading, pause);
				return machine.holding();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				logger::write_once("jip:fault", "join in progress: a fault while bringing a player in - the bring-in stops");
				machine.seats = {};
				return false;
			}
		}

		// ---- solo story segments: seat owners ------------------------------------------------------------------------
		join_in_progress_core::segment_tracker segment{};
		std::uint32_t owners_members = 0;
		bool read_binding(void* game, join_in_progress_core::seat_binding& b, std::array<bool, 4>& active, std::uint32_t& members)
		{
			__try
			{
				void* s = session();
				if (!s || !first(s)) return false;
				members = at<std::uint32_t>(s, session_members);
				for (int k = 0; k < 4; ++k)
				{
					const char* n = fn<name_text_t>(name_text)(reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(game) + game_party + 4 * k));
					std::size_t i = 0;
					for (; n && n[i] && i + 1 < b.name[k].size(); ++i) b.name[k][i] = n[i];
					b.name[k][i] = 0;
					b.controller[k] = at<std::int32_t>(game, game_slot_controller + 4 * k);
					b.human[k] = at<std::uint8_t>(game, game_slot_human + k) != 0;
				}
				void* seats = fn<getter_t>(seats_getter)();
				for (int c = 0; c < 4; ++c) active[c] = fn<seat_active_t>(at<std::uint32_t>(at<void*>(seats, 0), seats_active_slot))(seats, c) != 0;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool write_binding(void* game, const join_in_progress_core::seat_binding& b)
		{
			__try
			{
				for (int k = 0; k < 4; ++k)
				{
					*reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(game) + game_slot_controller + 4 * k) = b.controller[k];
					*(static_cast<std::uint8_t*>(game) + game_slot_human + k) = b.human[k] ? 1 : 0;
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		// The heroes the players hold now, from the party's live heroes (entity +0x3d4 controller, +0x3d8 bit 0 AI), taken
		// when seatParty is about to change the party; used if a solo segment starts at the next binding.
		join_in_progress_core::owner_memory party_change_owners{};
		bool party_change_noted = false;
		bool read_live_owners(join_in_progress_core::owner_memory& out)
		{
			// Party slot k's hero name (pool handle at game+0x14) -> its hero record (registry 0x44b8f0 vt+0x40, the
			// native binding's lookup); controller c's hero (table 0x70b814) is that hero when its entity's record
			// (+0x35c) is that record, it is player-controlled (+0x3d8 bit 0 clear) and its controller (+0x3d4) is c.
			using registry_find_t = void*(__thiscall*)(void*, const char*);
			__try
			{
				auto* game = reinterpret_cast<std::uint8_t*>(game_static);
				if (*reinterpret_cast<const std::uint32_t*>(game) != game_vtable) return false;
				void* registry = fn<getter_t>(hero_registry_getter)();
				if (!registry) return false;
				const auto find = fn<registry_find_t>(at<std::uint32_t>(at<void*>(registry, 0), hero_registry_find_slot));
				join_in_progress_core::owner_memory m{};
				for (int k = 0; k < 4; ++k)
				{
					const auto* handle = reinterpret_cast<const std::uint32_t*>(game + game_party + 4 * k);
					if (!*handle) continue;
					const char* n = fn<name_text_t>(name_text)(handle);
					if (!n || !n[0]) continue;
					void* record = find(registry, n);
					if (!record) continue;
					for (int c = 0; c < 4; ++c)
					{
						const std::uint32_t h = *reinterpret_cast<const std::uint32_t*>(hero_table + 4 * c);
						if (!h || m[c][0]) continue;
						auto* hero = static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle)(&h));
						if (!hero || at<void*>(hero, hero_record) != record || (hero[hero_ai_flags] & 1) || at<std::int32_t>(hero, actor_controller) != c) continue;
						std::size_t i = 0;
						for (; n[i] && i + 1 < m[c].size(); ++i) m[c][i] = n[i];
						m[c][i] = 0;
					}
				}
				out = m;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		void log_binding(const join_in_progress_core::seat_binding& binding)
		{
			for (int k = 0; k < 4; ++k)
				if (binding.name[k][0])
					logger::write("solo segment: party slot %d %s -> controller %d%s", k, binding.name[k].data(), int(binding.controller[k]), binding.human[k] ? "" : " (AI)");
		}
		// After the native seat binding (game vt+0x188) at the zone load (0x486d3b, from_load) or the zone start's
		// refresh (0x422618): the owners only inside a solo segment and in the zone where it ends.
		void seat_binding_after(void* game, bool from_load)
		{
			using namespace join_in_progress_core;
			seat_binding binding;
			std::array<bool, 4> active{};
			std::uint32_t members = 0;
			if (!read_binding(game, binding, active, members)) { segment = {}; owners_members = 0; return; }
			if (members != owners_members)
			{
				// Someone joined or left: what this PC remembers may differ from a newcomer's empty memory.
				segment = {};
				owners_members = members;
			}
			const segment_step step = observe_segment(segment, wide(binding, active), from_load);
			const bool noted = party_change_noted;
			if (from_load) party_change_noted = false;
			if (step == segment_step::started)
			{
				const bool live = noted;
				if (live) segment.owners = party_change_owners;
				char held[160]{};
				int n = 0;
				for (int c = 0; c < 4 && n >= 0 && n < int(sizeof(held)); ++c)
					if (segment.owners[c][0]) n += std::snprintf(held + n, sizeof(held) - n, "%s controller %d %s", n ? "," : "", c, segment.owners[c].data());
				logger::write("solo segment: starts - a player holds no hero; heroes held at its start (%s):%s", live ? "the heroes held when the party changed" : "the last wide zone's start", n ? held : " none remembered");
			}
			int moved = 0;
			if (step != segment_step::none)
			{
				moved = restore_owners(binding, segment.owners);
				if (moved)
				{
					if (!write_binding(game, binding)) return;
					log_binding(binding);
					logger::write(step == segment_step::ended ? "solo segment: ends - %d hero(es) handed back to the players who had them when it started"
						: "solo segment: %d hero(es) given to the players who had them when it started", moved);
				}
			}
			settle_segment(segment, binding, wide(binding, active), from_load, moved);
			if (step == segment_step::started || step == segment_step::inside)
				logger::write_once("jip:narrow", "solo segment: not every player holds a hero in this zone - watching players keep their heroes in mind for when the party widens (logged once)");
		}
		// In place of `call dword ptr [ebp+0x188]` at 0x486d3b (zone load) and `call dword ptr [ebx+0x188]` at 0x422618
		// (zone start refresh): the native seat binding, then the owners.
		void __fastcall seat_binding_bridge(void* game, void*, std::int32_t a, std::int32_t b)
		{
			fn<seat_binding_t>(at<std::uint32_t>(at<void*>(game, 0), game_seat_binding_slot))(game, a, b);
			seat_binding_after(game, true);
		}
		void __fastcall seat_rebinding_bridge(void* game, void*, std::int32_t a, std::int32_t b)
		{
			fn<seat_binding_t>(at<std::uint32_t>(at<void*>(game, 0), game_seat_binding_slot))(game, a, b);
			seat_binding_after(game, false);
		}

		// In place of the slow-player guard's drop (0x55933c, thiscall session, 5 stack arguments): the player stays.
		void __fastcall slow_drop_bridge(void* session, void*, void* record, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)
		{
			int index = -1;
			bool local = false;
			__try
			{
				if (record)
				{
					index = index_of(record);
					local = (at<std::uint32_t>(record, record_flags) & flag_local) != 0;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			(void)session;
			logger::write("online: slow-player guard: player %d%s reported frames over 50 ms for 1,700 frames (native PLAYER_LAG1/LAG2 shown) - kept in the session (the stock guard dropped it here); the counts start again",
				index, local ? " (this PC, the host)" : "");
		}

		int __cdecl roster_hero_bridge(char* out, int capacity, const char* pattern, const char* hero)
		{
			using format_t = int(__cdecl*)(char*, int, const char*, ...);
			return join_in_progress_core::format_roster_hero(fn<format_t>(roster_format), out, capacity, pattern, hero);
		}

		// ---- bridges -------------------------------------------------------------------------------------------------

		void __cdecl start_mark()
		{
			if (!machine.started) logger::write("join in progress: the session started; admission stays open while a seat is free");
			machine.started = true;
		}

		bool __cdecl defer_add(const void* record)
		{
			__try
			{
				const bool active = at<std::uint8_t>(record, record_active) != 0;
				const int index = index_of(record);
				return machine.defer_add(record, index, active, GetTickCount());
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		bool __cdecl pending(const void* record) { return machine.pending(record); }
		bool __cdecl late_admission() { return machine.started; }
		bool __cdecl defer_record_seats(const void* record, void* s)
		{
			if (!machine.started || !host(s)) return false;
			const int index = index_of(record);
			if (!machine.defer_add(record, index, false, GetTickCount())) return false;
			logger::write("join in progress: player %d hero-seat activation deferred in participant creation", index);
			return true;
		}
		// The admission itself (before the newcomer's 0x1e): pending from here, so no frame sees the record unmarked.
		void __cdecl note_late(const void* record, int index)
		{
			if (machine.defer_add(record, index, false, GetTickCount()))
				logger::write("join in progress: player %d admitted while the session plays - pending (outside the lockstep) until the next zone load", index);
		}

		char __fastcall all_loaded_bridge(void* pause, void*)
		{
			const char native = fn<loaded_t>(all_loaded)(pause);
			// The shared native restart already includes every participant. Its first barrier is readiness to
			// load (nonzero grid entries), before the later zone-loaded phase (2); do not hold that first phase.
			if (epoch.active && epoch.started) return native;
			if (!machine.any()) return native;
			return guarded_advance(true, pause) ? 0 : native;
		}

		void __fastcall area_barrier_bridge(void* s, void*, float timeout)
		{
			bool waiting = false;
			for (const auto& seat : machine.seats) waiting |= seat.at == stage::pending;
			if (waiting && host(s))
			{
				epoch.begin();
				existing_cohort_member = true;
				clear_frontend_callback(manager());
				area_load = true;
				logger::write("join in progress: synchronous area-load boundary reached; adding before the zone loads");
				guarded_advance(false, nullptr);
			}
			fn<barrier_t>(area_barrier)(s, timeout);
		}

#ifndef NDEBUG
		// [Dev] MeshDeferMs = <ms> (development builds only): a burst's second and later mesh completions run that much
		// later from the receive pump, as a slow natneg would - other participants' 0x25 replies then arrive while the
		// first one is still "being added" (the 4-player start race on demand). [Dev] MeshReplyLegacy = 1: the stock
		// 0x25 rebind without the guard below (negative control).
		DWORD mesh_defer_ms = 0;
		bool mesh_reply_legacy = false;
		DWORD mesh_burst_at = 0;
		struct deferred_mesh { void* record; std::uint32_t port; std::uint8_t address[12]; DWORD due; };
		std::vector<deferred_mesh> deferred_meshes;
#endif
		void mesh_connect_now(void* record, std::uint32_t port, void* address);
		void __cdecl mesh_connect(void* record, std::uint32_t port, void* address)
		{
#ifndef NDEBUG
			if (mesh_defer_ms)
			{
				const DWORD now = GetTickCount();
				const bool later = mesh_burst_at && now - mesh_burst_at < 3000;
				mesh_burst_at = now;
				if (later)
				{
					deferred_mesh d{record, port, {}, now + mesh_defer_ms};
					std::memcpy(d.address, address, sizeof(d.address));
					deferred_meshes.push_back(d);
					logger::write("join in progress: [Dev] MeshDeferMs - mesh completion for player %d deferred %lu ms", index_of(record), mesh_defer_ms);
					return;
				}
			}
#endif
			mesh_connect_now(record, port, address);
		}
#ifndef NDEBUG
		void run_deferred_meshes()
		{
			for (std::size_t i = 0; i < deferred_meshes.size();)
			{
				if (int(GetTickCount() - deferred_meshes[i].due) < 0) { ++i; continue; }
				deferred_mesh d = deferred_meshes[i];
				deferred_meshes.erase(deferred_meshes.begin() + i);
				if (!listed(session(), d.record)) continue;
				logger::write("join in progress: [Dev] MeshDeferMs - deferred mesh completion for player %d runs now", index_of(d.record));
				mesh_connect_now(d.record, d.port, d.address);
			}
		}
#endif
		void mesh_connect_now(void* record, std::uint32_t port, void* address)
		{
			__try
			{
				void* m = manager();
				if (machine.started && !host(session()))
				{
					epoch.begin();
					existing_cohort_member = true;
					clear_frontend_callback(m);
					logger::write("join in progress: new mesh participant; cohort checksum hold active");
				}
				const int index = index_of(record);
				at<std::int32_t>(m, manager_adding) = index;
				void* done = e4_observe::native_call("jip:mesh completion", [&] { return fn<natneg_t>(natneg_done)(m, address, port); });
				logger::write("join in progress: mesh peer %d connected through the session transport (natneg completion %s)", index, done ? "ran" : "found no record");
				// 0x607210 sends the host's 0x23 topology update. Its stock caller then sends 0x25 to the completed
				// peer (0x6051bb..0x6051dc); without that reply the newcomer cannot complete its half of the mesh.
				if (done) logger::write("join in progress: mesh peer %d - native 0x25 completion reply %s", index,
					send(done, opcode_mesh_done, nullptr, 0) ? "sent" : "NOT sent");
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				logger::write_once("jip:mesh-fault", "join in progress: the mesh completion faulted - the add waits for natneg");
			}
		}

		bool same_address(const void* peer, std::uint32_t ip, std::uint32_t port)
		{
			return peer && at<std::uint32_t>(peer, peer_address) == ip && at<std::uint16_t>(peer, peer_port) == std::uint16_t(port);
		}
#ifndef NDEBUG
		// MeshReplyLegacy: the stock completion, logged (which record it took and where that record's peer now is).
		void* legacy_done(void* m, void* address, std::uint32_t port, const char* what)
		{
			const std::int32_t adding = at<std::int32_t>(m, manager_adding);
			void* r = fn<natneg_t>(natneg_done)(m, address, port);
			__try
			{
				const std::uint32_t ip = at<std::uint32_t>(address, address_ip);
				const auto* b = reinterpret_cast<const std::uint8_t*>(&ip);
				void* peer = r ? at<void*>(r, record_peer) : nullptr;
				logger::write("join in progress: [Dev] legacy %s from %u.%u.%u.%u:%u - stock completion for player %d -> record %p peer %p", what, b[0], b[1], b[2], b[3], unsigned(port), int(adding), r, peer);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			return r;
		}
#else
		void* legacy_done(void* m, void* address, std::uint32_t port, const char*) { return fn<natneg_t>(natneg_done)(m, address, port); }
#endif
		// Replaces the 0x25 mesh reply's completion call (0x604eb0, thiscall manager, ret 8). See decide_mesh_reply.
		void* __fastcall mesh_reply_bridge(void* m, void*, void* address, std::uint32_t port)
		{
			port &= 0xffff; // 0x6106c0 returns the port in ax only
#ifndef NDEBUG
			if (mesh_reply_legacy) return legacy_done(m, address, port, "0x25 reply");
#endif
			__try
			{
				const std::uint32_t ip = at<std::uint32_t>(address, address_ip);
				void* s = session();
				const std::int32_t adding = at<std::int32_t>(m, manager_adding);
				using by_index_t = void*(__thiscall*)(void*, std::int32_t);
				void* target = s && adding >= 0 ? fn<by_index_t>(record_by_index)(s, adding) : nullptr;
				void* owner = nullptr;
				for (void* n = first(s); n && !owner; n = at<void*>(n, node_next))
				{
					void* r = at<void*>(n, node_record);
					if (r && !(at<std::uint32_t>(r, record_flags) & flag_local) && same_address(at<void*>(r, record_peer), ip, port)) owner = r;
				}
				void* bound = target ? at<void*>(target, record_peer) : nullptr;
				const auto d = join_in_progress_core::decide_mesh_reply(owner != nullptr, target != nullptr, bound && !same_address(bound, ip, port));
				const auto* b = reinterpret_cast<const std::uint8_t*>(&ip);
				if (d == join_in_progress_core::mesh_reply::already_bound)
				{
					if (owner != target)
						logger::write("join in progress: mesh reply from %u.%u.%u.%u:%u (player %d's peer) while player %d is being added - left as bound (no rebind)",
							b[0], b[1], b[2], b[3], unsigned(port), index_of(owner), int(adding));
					return owner;
				}
				if (d == join_in_progress_core::mesh_reply::refused)
				{
					logger::write("join in progress: mesh reply from %u.%u.%u.%u:%u refused for player %d, whose record keeps its own peer (the stock rebind would bind it to another participant and free its peer)",
						b[0], b[1], b[2], b[3], unsigned(port), int(adding));
					return nullptr;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				logger::write_once("jip:mesh-reply-fault", "join in progress: the mesh reply guard faulted - the stock completion runs");
			}
			return fn<natneg_t>(natneg_done)(m, address, port);
		}
		// Replaces the natneg completions' 0x607210 calls (0x6051a6 result 2, 0x60524b result 3; thiscall manager,
		// ret 8; both callers dereference the result). A record that already has its peer keeps it: natneg reports the
		// participant's real address, and rebinding there would bypass the room tunnel / GNS link (and free the peer the
		// session transport uses). The caller's 0x25 then goes to the record's own peer.
		void* __fastcall natneg_done_bridge(void* m, void*, void* address, std::uint32_t port)
		{
			port &= 0xffff;
#ifndef NDEBUG
			if (mesh_reply_legacy) return legacy_done(m, address, port, "natneg");
#endif
			__try
			{
				const std::uint32_t ip = at<std::uint32_t>(address, address_ip);
				void* s = session();
				const std::int32_t adding = at<std::int32_t>(m, manager_adding);
				using by_index_t = void*(__thiscall*)(void*, std::int32_t);
				void* target = s && adding >= 0 ? fn<by_index_t>(record_by_index)(s, adding) : nullptr;
				void* bound = target ? at<void*>(target, record_peer) : nullptr;
				if (bound && !same_address(bound, ip, port))
				{
					const auto* b = reinterpret_cast<const std::uint8_t*>(&ip);
					logger::write("join in progress: natneg completion for player %d at %u.%u.%u.%u:%u ignored - the record keeps its session-transport peer",
						int(adding), b[0], b[1], b[2], b[3], unsigned(port));
					return target;
				}
				const auto* b = reinterpret_cast<const std::uint8_t*>(&ip);
				logger::write("join in progress: natneg completion for player %d at %u.%u.%u.%u:%u (stock completion: the record has no peer or this one)", int(adding), b[0], b[1], b[2], b[3], unsigned(port));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				logger::write_once("jip:natneg-done-fault", "join in progress: the natneg completion guard faulted - the stock completion runs");
			}
			return fn<natneg_t>(natneg_done)(m, address, port);
		}
		// 0x6116eb in the 0x5b barrier-ack handler: 0x611110 finds no record for the sender (a peer no participant record
		// is bound to) and the stock code sets flag 0x400 on null. The message is dropped instead.
		unsigned null_acks = 0;
		std::uint32_t null_guard_off = 0; // [Dev] MeshReplyLegacy=1: the 0x5b site stays stock (the Q1a access violation)
		// The native peers no participant record holds (manager list +0x124b8; address +0x14, port +8).
		void unbound_peers(char* out, std::size_t size)
		{
			out[0] = 0;
			__try
			{
				void* m = manager();
				void* s = session();
				for (void* n = m && at<std::uint32_t>(m, 0x124c4) ? at<void*>(m, 0x124b8) : nullptr; n; n = at<void*>(n, node_next))
				{
					void* peer = at<void*>(n, node_record);
					bool held = false;
					for (void* r = first(s); r && !held; r = at<void*>(r, node_next)) held = at<void*>(at<void*>(r, node_record), record_peer) == peer;
					if (held || !peer) continue;
					const auto* b = reinterpret_cast<const std::uint8_t*>(&at<std::uint32_t>(peer, peer_address));
					char one[40];
					std::snprintf(one, sizeof(one), " %u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], unsigned(at<std::uint16_t>(peer, peer_port)));
					strncat_s(out, size, one, _TRUNCATE);
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { strncat_s(out, size, " (fault)", _TRUNCATE); }
			if (!out[0]) strncat_s(out, size, " none", _TRUNCATE);
		}
		void __cdecl note_null_ack()
		{
			if (++null_acks > 5 && null_acks % 100 != 0) return;
			char peers[256];
			unbound_peers(peers, sizeof(peers));
			logger::write("online: a participant message (0x5b barrier ack / 0x4b ready / 0x4d not ready) came from a peer no participant record is bound to - ignored (%u so far; stock: access violation on the null record); unbound peers:%s", null_acks, peers);
		}
		__declspec(naked) void loaded_flag_bridge()
		{
			__asm {
				test ecx, ecx
				jnz go
				cmp dword ptr [null_guard_off], 0
				je none
			go:
				jmp dword ptr [set_flags_at]
			none:
				pushad
				call note_null_ack
				popad
				ret 4
			}
		}
		// 0x60c4f2 in the 0x4b handler (ebp = 0x611110's record for the sender): skip the handler on null.
		__declspec(naked) void ready_clear_bridge()
		{
			__asm {
				test ebp, ebp
				jz none
				push 0x10
				mov ecx, ebp
				call dword ptr [clear_flags_at]
				jmp dword ptr [ready_clear_resume_at]
			none:
				pushad
				call note_null_ack
				popad
				jmp dword ptr [ready_clear_skip_at]
			}
		}
		// Every remote participant record has its own peer; two on one peer means the mesh bound a participant twice.
		void check_mesh()
		{
			static const void* reported[2]{};
			__try
			{
				const void* seen[8]{}; const void* recs[8]{}; int count = 0;
				for (void* n = first(session()); n && count < 8; n = at<void*>(n, node_next))
				{
					void* r = at<void*>(n, node_record);
					if (!r || (at<std::uint32_t>(r, record_flags) & flag_local)) continue;
					const void* peer = at<void*>(r, record_peer);
					if (!peer) continue;
					for (int k = 0; k < count; ++k)
						if (seen[k] == peer && (reported[0] != recs[k] || reported[1] != r))
						{
							reported[0] = recs[k]; reported[1] = r;
							logger::write("online: MESH ERROR - participant records %p (player %d) and %p (player %d) are bound to one peer %p",
								recs[k], index_of(recs[k]), static_cast<const void*>(r), index_of(r), peer);
						}
					seen[count] = peer; recs[count++] = r;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}

		__declspec(naked) void gamespy_teardown_bridge()
		{
			__asm {
				mov eax, dword ptr ds:[0xa37d80]
				test eax, eax
				jz teardown
				cmp dword ptr [eax + 0xaf0], 0
				jne keep
			teardown:
				jmp dword ptr [original_teardown]
			keep:
				ret
			}
		}
		__declspec(naked) void checksum_send_bridge()
		{
			__asm {
				pushad
				call checksum_hold
				test al, al
				jz run
				popad
				xor eax, eax
				ret
			run:
				popad
				sub esp, 0x6c
				push ebx
				mov ebx, ecx
				jmp dword ptr [checksum_send_at]
			}
		}
		__declspec(naked) void checksum_compare_bridge()
		{
			__asm {
				pushad
				call checksum_hold
				test al, al
				jz run
				popad
				xor eax, eax
				ret
			run:
				popad
				sub esp, 0x20
				push ebx
				push ebp
				jmp dword ptr [checksum_compare_at]
			}
		}
		__declspec(naked) void campaign_receive_bridge()
		{
			__asm {
				pushad
				push dword ptr [esp + 0x24]
				call campaign_notice
				add esp, 4
				popad
				sub esp, 0x14
				push ebx
				push ebp
				jmp dword ptr [campaign_receive_at]
			}
		}
		__declspec(naked) void campaign_begin_bridge()
		{
			__asm {
				pushad
				call campaign_start_notice
				test al, al
				jz duplicate
				popad
				push esi
				call dword ptr [game_getter_at]
				jmp dword ptr [campaign_begin_at]
			duplicate:
				popad
				xor eax, eax
				ret
			}
		}
		__declspec(naked) void broadcast_entry_bridge()
		{
			__asm {
				pushad
				push dword ptr [esp + 0x24]
				call broadcast_notice
				add esp, 4
				popad
				push ebp
				mov ebp, ecx
				mov eax, dword ptr [ebp + 0x41c]
				jmp dword ptr [broadcast_at]
			}
		}

		__declspec(naked) void add_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 8]
				call defer_add
				add esp, 4
				pop ecx
				test al, al
				jnz deferred
				sub esp, 0x78
				mov eax, dword ptr ds:[0x6f38f8]
				jmp dword ptr [add_resume_at]
			deferred:
				ret 4
			}
		}
		__declspec(naked) void frame_walk_bridge()
		{
			__asm {
			top:
				mov esi, dword ptr [edi + 0x10]
				push esi
				call pending
				add esp, 4
				test al, al
				jnz skip
				mov ecx, esi
				jmp dword ptr [frame_resume_at]
			skip:
				mov edi, dword ptr [edi + 8]
				test edi, edi
				jnz top
				jmp dword ptr [frame_exit_at]
			}
		}
		__declspec(naked) void stream_walk_bridge()
		{
			__asm {
			top:
				mov ecx, dword ptr [esi + 0x10]
				mov edi, dword ptr [esi + 8]
				push ecx
				call pending
				add esp, 4
				test al, al
				jnz skip
				mov ecx, dword ptr [esi + 0x10]
				jmp dword ptr [stream_resume_at]
			skip:
				mov esi, edi
				test edi, edi
				jnz top
				jmp dword ptr [stream_exit_at]
			}
		}
		// Replaces the 0x1f handler's "Adding player" wait call (thiscall, three arguments): the wait, then the natneg
		// completion for the new participant's peer (the caller's address object at its [esp+0x14], the port at
		// message+0x48, the record in esi).
		__declspec(naked) void mesh_wait_bridge()
		{
			__asm {
				push dword ptr [esp + 0xc]
				push dword ptr [esp + 0xc]
				push dword ptr [esp + 0xc]
				call dword ptr [wait_at]
				pushad
				lea eax, [esp + 0x44]
				push eax
				movzx eax, word ptr [edi + 0x48]
				push eax
				push esi
				call mesh_connect
				add esp, 0xc
				popad
				ret 0xc
			}
		}
		// The participant constructor itself marks/reconciles a hero seat before the later admission notice.
		// With a free AI hero that immediately steals it on the host alone. Defer that constructor block too.
		__declspec(naked) void record_seats_bridge()
		{
			__asm {
				pushfd
				pushad
				push edi
				push esi
				call defer_record_seats
				add esp, 8
				mov dword ptr [esp + 28], eax
				popad
				popfd
				test al, al
				jnz deferred
				call dword ptr [seats_at]
				mov edi, eax
				mov ebx, dword ptr [edi]
				jmp dword ptr [record_seats_resume_at]
			deferred:
				jmp dword ptr [record_seats_exit_at]
			}
		}

		// Replaces the admission's controller slot assignment and its "remote player added" notice (0x6099f2..0x609a1c,
		// esi = the record, [esp+0xc] = its index): after Start Game the record keeps slot -1 and the game hears of the
		// player only at the bring-in.
		__declspec(naked) void admit_controller_bridge()
		{
			__asm {
				call late_admission
				test al, al
				jnz late
				call dword ptr [controllers_at]
				mov ecx, dword ptr [esp + 0xc]
				mov edx, dword ptr [eax]
				push 0
				push ecx
				mov ecx, eax
				call dword ptr [edx + 0x38]
				push eax
				mov ecx, esi
				call dword ptr [record_controller_at]
				call dword ptr [players_at]
				mov edx, dword ptr [eax]
				push 0
				push esi
				mov ecx, eax
				call dword ptr [edx + 0x14]
				jmp dword ptr [admit_resume_at]
			late:
				push dword ptr [esp + 0xc]
				push esi
				call note_late
				add esp, 8
				push -1
				mov ecx, esi
				call dword ptr [record_controller_at]
				jmp dword ptr [admit_resume_at]
			}
		}
		// The local test (thiscall 0x614290, ecx = record) of the broadcast and of the frame gate: local or pending =
		// skip; ecx/edx kept.
		__declspec(naked) void broadcast_skip_bridge()
		{
			__asm {
				test byte ptr [ecx + 0x1c], 1
				jnz yes
				push ecx
				push edx
				push ecx
				call pending
				add esp, 4
				pop edx
				pop ecx
				movzx eax, al
				ret
			yes:
				mov eax, 1
				ret
			}
		}
		// The stall watchdog's peer of a record (thiscall 0x4692d0 = record+8): none for a pending record.
		__declspec(naked) void watchdog_peer_bridge()
		{
			__asm {
				push ecx
				push edx
				push ecx
				call pending
				add esp, 4
				pop edx
				pop ecx
				test al, al
				jnz none
				mov eax, dword ptr [ecx + 8]
				ret
			none:
				xor eax, eax
				ret
			}
		}
		__declspec(naked) void natneg_reply_bridge()
		{
			__asm {
				mov esi, eax
				test eax, eax
				jz skip
				lea ecx, [esp + 0xc]
				jmp dword ptr [natneg_resume_at]
			skip:
				jmp dword ptr [natneg_skip_at]
			}
		}

		template<std::size_t N> std::vector<unsigned char> bytes(const std::array<unsigned char, N>& a) { return {a.begin(), a.end()}; }
		std::vector<unsigned char> rel(unsigned char op, std::uint32_t site, const void* target, std::size_t size)
		{
			std::vector<unsigned char> out(size, 0x90);
			out[0] = op;
			const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (site + 5);
			std::memcpy(out.data() + 1, &relative, 4);
			return out;
		}
	}

	void note_party_change()
	{
		if (state.load() != 1) return;
		join_in_progress_core::owner_memory m{};
		if (!read_live_owners(m)) { party_change_noted = false; return; }
		// The first change after a wide binding counts (a later seatParty before the load sees the narrowed party).
		if (!party_change_noted) { party_change_owners = m; party_change_noted = true; }
	}
	void install(HMODULE game)
	{
		if (!ini::flag(L"Online", L"JoinInProgress", false)) return;
		state = 0;
		trace = ini::flag(L"Dev", L"JoinInProgressTrace", false);
#ifndef NDEBUG
		mesh_defer_ms = DWORD(ini::number(L"Dev", L"MeshDeferMs", 0));
		mesh_reply_legacy = ini::number(L"Dev", L"MeshReplyLegacy", 0) == 1;
		if (mesh_defer_ms) logger::write("join in progress: [Dev] MeshDeferMs=%lu - a burst's later mesh completions are deferred (development build)", mesh_defer_ms);
		if (mesh_reply_legacy) logger::write("join in progress: [Dev] MeshReplyLegacy=1 - the stock 0x25 and natneg rebinds run unguarded and a 0x5b/0x4d from an unbound peer reaches the stock null access (negative control)");
		null_guard_off = mesh_reply_legacy ? 1u : 0u;
#endif
		if (!gns_transport::requested())
		{
			logger::write("online: [Online] JoinInProgress=1 refused - it needs [Online] Transport=gns (rule 25 GnsTransport); rule 26 JoinInProgress is not offered and Start Game stays stock");
			return;
		}
		if (!gns_transport::installed())
		{
			logger::write("online: [Online] JoinInProgress=1 refused - the GNS transport (rule 25) did not install; rule 26 JoinInProgress is not offered");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("online: [Online] JoinInProgress=1 refused - not the retail executable");
			return;
		}
		if (const auto* g = mismatch())
		{
			logger::write("online: [Online] JoinInProgress=1 refused - retail guard at 0x%06X (%s) differs; Start Game stays stock", unsigned(g->va), g->what);
			return;
		}
		xml1_hook_transaction::memory memory;
		bool safe = true;
		std::vector<xml1_hook_transaction::patch> writes{
			{roster_hero_call, bytes(roster_hero_original), rel(0xe8, roster_hero_call, reinterpret_cast<const void*>(&roster_hero_bridge), 5)},
			{teardown_call, bytes(teardown_original), rel(0xe8, teardown_call, reinterpret_cast<const void*>(&start_mark), 5)},
			{unregister_2c, bytes(branch_original), bytes(branch_patched)},
			{unregister_18, bytes(branch_original), bytes(branch_patched)},
			{gamespy_call, bytes(gamespy_original), rel(0xe8, gamespy_call, reinterpret_cast<const void*>(&gamespy_teardown_bridge), 5)},
			{lobby_tick, bytes(branch_original), bytes(branch_patched)},
			{keep_1f, bytes(branch_original), bytes(branch_patched)},
			{add_entry, bytes(add_original), rel(0xe9, add_entry, reinterpret_cast<const void*>(&add_bridge), add_original.size())},
			{record_seats, bytes(record_seats_original), rel(0xe9, record_seats, reinterpret_cast<const void*>(&record_seats_bridge), record_seats_original.size())},
			{frame_walk, bytes(frame_walk_original), rel(0xe9, frame_walk, reinterpret_cast<const void*>(&frame_walk_bridge), frame_walk_original.size())},
			{stream_walk, bytes(stream_walk_original), rel(0xe9, stream_walk, reinterpret_cast<const void*>(&stream_walk_bridge), stream_walk_original.size())},
			{loaded_call, bytes(loaded_original), rel(0xe8, loaded_call, reinterpret_cast<const void*>(&all_loaded_bridge), 5)},
			{area_barrier_call, bytes(area_barrier_original), rel(0xe8, area_barrier_call, reinterpret_cast<const void*>(&area_barrier_bridge), 5)},
			{campaign_unregister_call, bytes(campaign_unregister_original), bytes(campaign_unregister_patched)},
			{checksum_send, bytes(checksum_send_original), rel(0xe9, checksum_send, reinterpret_cast<const void*>(&checksum_send_bridge), checksum_send_original.size())},
			{checksum_compare, bytes(checksum_compare_original), rel(0xe9, checksum_compare, reinterpret_cast<const void*>(&checksum_compare_bridge), checksum_compare_original.size())},
			{broadcast_entry, bytes(broadcast_entry_original), rel(0xe9, broadcast_entry, reinterpret_cast<const void*>(&broadcast_entry_bridge), broadcast_entry_original.size())},
			{campaign_load_call, bytes(campaign_load_original), rel(0xe8, campaign_load_call, reinterpret_cast<const void*>(&campaign_load_bridge), campaign_load_original.size())},
			{campaign_begin, bytes(campaign_begin_original), rel(0xe9, campaign_begin, reinterpret_cast<const void*>(&campaign_begin_bridge), campaign_begin_original.size())},
			{mesh_wait_call, bytes(mesh_wait_original), rel(0xe8, mesh_wait_call, reinterpret_cast<const void*>(&mesh_wait_bridge), 5)},
			{natneg_reply, bytes(natneg_reply_original), rel(0xe9, natneg_reply, reinterpret_cast<const void*>(&natneg_reply_bridge), natneg_reply_original.size())},
			{mesh_reply_call, bytes(mesh_reply_original), rel(0xe8, mesh_reply_call, reinterpret_cast<const void*>(&mesh_reply_bridge), 5)},
			{natneg_done_call_2, bytes(natneg_done_2_original), rel(0xe8, natneg_done_call_2, reinterpret_cast<const void*>(&natneg_done_bridge), 5)},
			{natneg_done_call_3, bytes(natneg_done_3_original), rel(0xe8, natneg_done_call_3, reinterpret_cast<const void*>(&natneg_done_bridge), 5)},
			{loaded_flag_call, bytes(loaded_flag_original), rel(0xe8, loaded_flag_call, reinterpret_cast<const void*>(&loaded_flag_bridge), 5)},
			{unready_set_call, bytes(unready_set_original), rel(0xe8, unready_set_call, reinterpret_cast<const void*>(&loaded_flag_bridge), 5)},
			{ready_clear_site, bytes(ready_clear_original), rel(0xe9, ready_clear_site, reinterpret_cast<const void*>(&ready_clear_bridge), ready_clear_original.size())},
			{admit_controller, bytes(admit_controller_original), rel(0xe9, admit_controller, reinterpret_cast<const void*>(&admit_controller_bridge), admit_controller_original.size())},
			{broadcast_local_tests[0], bytes(broadcast_local_originals[0]), rel(0xe8, broadcast_local_tests[0], reinterpret_cast<const void*>(&broadcast_skip_bridge), 5)},
			{broadcast_local_tests[1], bytes(broadcast_local_originals[1]), rel(0xe8, broadcast_local_tests[1], reinterpret_cast<const void*>(&broadcast_skip_bridge), 5)},
			{broadcast_local_tests[2], bytes(broadcast_local_originals[2]), rel(0xe8, broadcast_local_tests[2], reinterpret_cast<const void*>(&broadcast_skip_bridge), 5)},
			{broadcast_local_tests[3], bytes(broadcast_local_originals[3]), rel(0xe8, broadcast_local_tests[3], reinterpret_cast<const void*>(&broadcast_skip_bridge), 5)},
			{broadcast_local_tests[4], bytes(broadcast_local_originals[4]), rel(0xe8, broadcast_local_tests[4], reinterpret_cast<const void*>(&broadcast_skip_bridge), 5)},
			{watchdog_peer, bytes(watchdog_peer_original), rel(0xe8, watchdog_peer, reinterpret_cast<const void*>(&watchdog_peer_bridge), 5)},
			{seat_binding_call, bytes(seat_binding_original), rel(0xe8, seat_binding_call, reinterpret_cast<const void*>(&seat_binding_bridge), seat_binding_original.size())},
			{start_alone, bytes(start_alone_original), bytes(start_alone_patched)},
			{keep_hosting, bytes(keep_hosting_original), bytes(keep_hosting_patched)},
			{seat_rebinding_call, bytes(seat_rebinding_original), rel(0xe8, seat_rebinding_call, reinterpret_cast<const void*>(&seat_rebinding_bridge), seat_rebinding_original.size())},
			{slow_drop_call, bytes(slow_drop_original), rel(0xe8, slow_drop_call, reinterpret_cast<const void*>(&slow_drop_bridge), slow_drop_original.size())}};
		if (!composed_receiver) writes.push_back({campaign_receive, bytes(campaign_receive_original), rel(0xe9, campaign_receive, reinterpret_cast<const void*>(&campaign_receive_bridge), campaign_receive_original.size())});
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			if (composed_receiver) entity_transfer::observe_prepare(&campaign_notice);
			state = 1;
			gameplay_agreement::register_rule(gameplay_capabilities::join_in_progress_id, agreement_revision, {1u, 4u, 0u, 0u}, true);
			logger::write("online: JoinInProgress installed (rule 26 revision 6, experimental) - late players wait for a shared campaign restore at the next zone; the host may start alone and keeps hosting when the last player leaves; players without a hero in a solo story segment stay and watch, and get their own heroes back when the party widens; a slow player is not dropped; failed restores or abrupt disconnections may require rehosting; every PC must use it");
		}
		else if (safe)
			logger::write("online: [Online] JoinInProgress=1 transaction refused - Start Game stays stock; rule 26 not offered");
		else
		{
			game_version::refuse_startup();
			logger::write("online: [Online] JoinInProgress=1 rollback/protection verification failed; startup refused");
		}
	}
	namespace
	{
		// Development trace ([Dev] JoinInProgressTrace=1): the scheduler, pause and session words every 5 s after Start.
		void hex(char* out, const void* base, std::uint32_t from, std::uint32_t count)
		{
			__try
			{
				for (std::uint32_t i = 0; i < count; i += 4)
					std::snprintf(out + i / 4 * 9, 10, "%08x ", at<std::uint32_t>(base, from + i));
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { std::strcpy(out, "fault"); }
		}
		void trace_words()
		{
			static DWORD last = 0;
			if (!trace || !machine.started || GetTickCount() - last < 5000) return;
			last = GetTickCount();
			char a[300]{}, b[300]{}, c[300]{}, d[300]{};
			hex(a, fn<getter_t>(scheduler_getter)(), 0, 0x40);
			hex(b, fn<getter_t>(pause_getter)(), 0x60, 0x60);
			hex(c, session(), 0x420, 0x30);
			hex(d, manager(), 0x2218, 0x28);
			logger::write("jip trace: sched %s| pause+60 %s| session+420 %s| mgr+2218 %s", a, b, c, d);
			if (epoch.active)
			{
				void* p = fn<getter_t>(pause_getter)();
				logger::write("jip epoch: start=%d saw_load=%d zones=%08x frame=%u game_type=%u mode=%u phase=%08x pause_init=%u pause=%u loaded=%u/%u/%u/%u",
					epoch.started, epoch.saw_loading, *reinterpret_cast<std::uint32_t*>(0x72a798),
					unsigned(*reinterpret_cast<std::uint16_t*>(0xa3bbf4)), *reinterpret_cast<std::uint32_t*>(0x729964),
					*reinterpret_cast<std::uint32_t*>(0x729f6c), at<std::uint32_t>(session(),0x424),
					at<std::uint8_t>(p,0x64),at<std::uint8_t>(p,0x6c),at<std::uint32_t>(p,0x94),
					at<std::uint32_t>(p,0x98),at<std::uint32_t>(p,0x9c),at<std::uint32_t>(p,0xa0));
			}
		}
	}
	namespace
	{
		// Solo story segments (the first game seats one hero alone: Wolverine's opening, Magma's mansion tours...). A
		// participant whose controller has no hero in the zone stays in the session and watches; this PC says so.
		struct
		{
			bool watching = false;
			DWORD shown = 0;
		} spectator;
		const char spectator_text[] = "Solo story segment - you rejoin when it ends";

		void spectate()
		{
			// Memory reads only (no game calls): this runs on one PC and must not touch the simulation.
			__try
			{
				void* s = session();
				void* local = s && first(s) ? local_record(s) : nullptr;
				const auto* zones = reinterpret_cast<const std::uint8_t*>(zones_static);
				const auto* game = reinterpret_cast<const std::uint8_t*>(game_static);
				if (!local || *reinterpret_cast<const std::uint32_t*>(zones) != zones_vtable || *reinterpret_cast<const std::uint32_t*>(game) != game_vtable)
				{
					spectator = {};
					return;
				}
				if (zones[zones_loading_at] & 3) return; // a zone load is pending (the CMap's +0x220 bits, as vt+0x24 reads them)
				char zone[zone_name_size + 1]{};
				std::memcpy(zone, zones + zones_name_at, zone_name_size);
				if (!zone[0] || std::strncmp(zone, "menu/", 5) == 0) return;
				// The hero controllers are the participant indices (host 0, joiners 1..3) on every PC; the record's +0x30
				// is this PC's own pad numbering (the local player is 0 everywhere) - live probe, net030-solo E8c.
				const std::int32_t controller = at<std::int32_t>(local, record_index_at);
				if (controller < 0 || controller > 3) return;
				bool has_hero = false;
				for (int k = 0; k < 4; ++k)
					has_hero |= *reinterpret_cast<const std::uint32_t*>(game + game_party + 4 * k) != 0 && game[game_slot_human + k] != 0 &&
						*reinterpret_cast<const std::int32_t*>(game + game_slot_controller + 4 * k) == controller;
				const DWORD now = GetTickCount();
				if (has_hero)
				{
					if (spectator.watching)
						logger::write("solo segment: this PC's player (controller %d) has a hero again in %s", int(controller), zone);
					spectator = {};
					return;
				}
				if (!spectator.watching)
					logger::write("solo segment: no hero for this PC's player (controller %d) in %s - watching, still in the session, until the party grows", int(controller), zone);
				if (!spectator.watching || now - spectator.shown >= spectator_repeat_ms)
				{
					fn<message_t>(message_show)(fn<getter_t>(scheduler_getter)(), spectator_text, 5, spectator_seconds);
					spectator.shown = now;
				}
				spectator.watching = true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				logger::write_once("jip:spectate", "solo segment: a fault reading this PC's hero - no watching message");
			}
		}
	}
	void tick()
	{
#ifndef NDEBUG
		if (state.load() == 1 && !deferred_meshes.empty()) run_deferred_meshes();
#endif
		if (state.load() == 1) check_mesh();
		if (state.load() == 1) spectate();
		if (state.load() == 1) trace_words();
		if (state.load() != 1) return;
		if (deferred_start_running) return;
		// Also clears started on a return to the front end, before another session can be admitted.
		guarded_advance(false, nullptr);
		if (!first(session())) { epoch = {}; area_load = false; return; }
		observe_epoch();
		if (!machine.any()) { area_load = false; return; }
		// A mid-game zone load runs synchronously between pump ticks and never opens the initial Start Game menu.
		// The boundary bridge has already added the newcomer; the next pump with the zone flags clear can serialize it.
		const bool loaded = area_load && !zone_load_pending();
		guarded_advance(loaded, loaded ? fn<getter_t>(pause_getter)() : nullptr);
		if (!machine.holding()) area_load = false;
	}
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
	bool bringing_in() { return installed() && (machine.holding() || epoch.active); }
	bool can_service() { return bringing_in() && !deferred_start_running && !zone_load_pending(); }
	bool pending_player(const void* record) { return installed() && machine.pending(record); }
}
