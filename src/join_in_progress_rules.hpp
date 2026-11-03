#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstring>

// [Online] JoinInProgress=1 (requested-only, gameplay rule 26 JoinInProgress, docs/join-in-progress.md; needs
// [Online] Transport=gns). Retail XMen2.exe sites, one transaction on every PC that sets the key:
//
// Start Game (0x6094f0 -> 0x609400) keeps admission, listing and the lobby tick:
//   0x60940e call 0x6074b0   unregisters 0x14, 0x19, 0x1a (the admission gate 0x609970), 0x1e  -> call the fix's
//                            start mark (the session has started; nothing unregistered)
//   0x609424 je              skips the opcode 0x2c unregister (0x607b90) when not registered    -> jmp
//   0x60943f je              the same for opcode 0x18 (0x6090d0)                                  -> jmp
//   0x609471 call 0x6159c0   GameSpy session teardown (QR2 listing, natneg) -> a bridge that skips it on the host only
//                            ([0xa37d80] = GameSpy manager, +0xaf0 = its QR2 instance, created by Post Game)
//   0x60947e je              removes the lobby tick task +0x22ac (0x60a260), which ends an add's wait -> jmp
//   0x6075e0 je              in 0x607530 (the joiner set's unregister): keep opcode 0x1f (0x6079b0, "a participant
//                            joins") so a started joiner still learns of a newcomer                -> jmp
// The newcomer stays outside the lockstep until the next zone load:
//   0x6099f2..0x609a1c       the admission (0x609970) assigns the newcomer a controller slot (0x551ed0 vt+0x38, stored
//                            at record+0x30 by 0x5382b0) and announces the remote player to the game (0x615580 vt+0x14)
//                            -> after Start Game both wait for the bring-in (live: the host's simulation stopped at a
//                            late admission)
//   0x608350 entry           the host's add of a participant (0x1e -> "Adding player" wait 0x615a60, 0x1f to everyone,
//                            mark the record active 0x60860a) -> deferred while the session is in a zone: the record
//                            stays pending (unmarked) until the next zone load
//   0x6112d4/0x611374/0x611409  the broadcast's (0x611270) per-record "local" test (0x614290) in its three modes: a
//                            pending record counts as local - nothing is queued for it (live: the per-frame input
//                            broadcasts, active-only off, filled the newcomer's reliable queue and the host's
//                            simulation stopped at a late admission)
//   0x606b14                 THE frame gate (0x606ae5..0x606b87, each frame): every non-local record must have its input
//                            for the frame (0x5587d0/0x558800) before the input manager runs (0x5587c0(1) at 0x606b98);
//                            its local test -> local or pending (live, Frida: after a late admission 0x606b98 was never
//                            reached again and the simulation stopped)
//   0x6069bd                 the frame tick's resync state (0x606960, mode 2, entered when the gate fails) walks every
//                            record's buffered input the same way: local test -> local or pending (live, Frida: the
//                            tick stayed in mode 2 from the admission on)
//   0x6137f5                 the stall watchdog (0x613790, per tick) takes each record's peer (0x4692d0): a pending
//                            record answers "no peer", so its missing frames neither announce a stall (0x5c) nor
//                            suspend the frame tick (live, Frida: ~4 s after a late admission 0x606d50 stopped calling
//                            the frame gate)
//   0x559200 / 0x55a2f0      the input manager's two walks over every participant record (frame timing 0x559190,
//                            per-player streams 0x55a2c0) skip a pending record
//   0x484def call 0x611910   synchronous area-load boundary: add before loading and latch progress for the next pump
//   0x5e68d9 call 0x60c350   initial-menu all-loaded query; the native cohort restart owns its two-phase barrier
//   0x609757 call 0x607630   keep G/H/I/J registered after the first campaign start
//   0x614a40 / 0x614810     hold checksums while the cohort restores one image; resume after the native load barrier
//   0x5f3c20 entry          queue the cohort start outside ordered packet delivery and reject duplicate starts
//   0x611270 entry          retire the completed sender before its 0x26 can cause a nested scheduler tick
//   0x607013 virtual call   cohort host restores mode 2, as the joiners do; ordinary native mode 0 is unchanged
//   0x608630 entry          observe full-campaign G; compose with the exact EntityCapacity-owned receiver if present
// The mesh add without GameSpy (stock: natneg between the two joiners, torn down at Start on joiners):
//   0x607a48 call 0x615a60   in the 0x1f handler, after the peer for the new participant is created at the address the
//                            host announced (0x612b80): the wait, then the natneg completion 0x607210 for that peer
//   0x6051ab                 the natneg completion's caller dereferences 0x607210's result (null when the add was
//                            already completed: the 0x4692d0 crash, net030-join run 4) -> null skips its 0x25 reply
namespace join_in_progress_rules
{
	using limits_rules::guard;
	constexpr std::uint32_t admission_entry = 0x609970;
	inline constexpr std::array<unsigned char, 5> admission_original{0x83,0xec,0x1c,0x53,0x57};
	inline constexpr std::array<unsigned char, 6> checksum_actor_original{0xd9,0x40,0x20,0x8b,0x48,0x28},
		checksum_hero_original{0x8b,0x44,0x24,0x78,0x03,0xfe};
	inline constexpr std::array<unsigned char, 5> checksum_camera_original{0xd9,0x00,0xd9,0x40,0x04};
	inline constexpr std::array<unsigned char, 7> checksum_produced_original{0xe8,0xa7,0xb5,0xf4,0xff,0x03,0xf8};
	constexpr std::uint32_t gamespy_thunk = 0x6159c0;
	constexpr std::uint32_t gamespy_think = 0x604da0;
	inline constexpr std::array<unsigned char,6> gamespy_think_original{0x8b,0x44,0x24,0x04,0x8b,0x08};
	constexpr std::uint32_t teardown_call = 0x60940e, unregister_2c = 0x609424, unregister_18 = 0x60943f, gamespy_call = 0x609471,
		lobby_tick = 0x60947e, keep_1f = 0x6075e0;
	constexpr std::uint32_t add_entry = 0x608350, add_resume = 0x608358;
	constexpr std::uint32_t admit_controller = 0x6099f2, admit_controller_resume = 0x609a1c, controllers_getter = 0x551ed0, players_getter = 0x615580,
		record_controller = 0x5382b0, controller_assign_slot = 0x38, player_added_slot = 0x14;
	constexpr std::array<std::uint32_t, 5> broadcast_local_tests{0x6112d4, 0x611374, 0x611409, 0x606b14, 0x6069bd};
	constexpr std::uint32_t watchdog_peer = 0x6137f5;
	constexpr std::uint32_t frame_walk = 0x559200, frame_walk_resume = 0x559205, frame_walk_exit = 0x5594f8;
	constexpr std::uint32_t stream_walk = 0x55a2f0, stream_walk_resume = 0x55a2f6, stream_walk_exit = 0x55a32d;
	constexpr std::uint32_t loaded_call = 0x5e68d9, all_loaded = 0x60c350;
    constexpr std::uint32_t any_unready = 0x60c200;
    // Team's two action checks and the host pause scheduler's final resume gate.
    constexpr std::array<std::uint32_t, 3> ready_calls{0x5e4b4e, 0x5e4c21, 0x60aaec};
    inline constexpr std::array<std::array<unsigned char, 5>, 3> ready_originals{{
        {0xe8,0xad,0x76,0x02,0x00}, {0xe8,0xda,0x75,0x02,0x00}, {0xe8,0x0f,0x17,0x00,0x00}}};
	constexpr std::uint32_t area_barrier_call = 0x484def, area_barrier = 0x611910;
	constexpr std::uint32_t campaign_unregister_call = 0x609757;
	constexpr std::uint32_t checksum_send = 0x614a40, checksum_send_resume = 0x614a46,
		checksum_compare = 0x614810, checksum_compare_resume = 0x614815,
		campaign_receive = 0x608630, campaign_receive_resume = 0x608635,
		campaign_begin = 0x5f3c20, campaign_begin_resume = 0x5f3c26, native_campaign_start = 0x6094f0;
	constexpr std::uint32_t broadcast_entry = 0x611270, broadcast_resume = 0x611279;
	constexpr std::uint32_t campaign_load_call = 0x607013;
	constexpr std::uint32_t start_loaded = 0x606fb0, network_setup = 0x613df0;
	constexpr std::uint32_t pause_set = 0x60c230, clear_flags = 0x614250;
	constexpr std::uint32_t controller_resume = 0x550d10;
	constexpr std::uint32_t clock_getter = 0x55b610, clock_reset_step = 0x55b230;
	constexpr std::uint32_t record_seats = 0x612b25, record_seats_resume = 0x612b2e, record_seats_exit = 0x612b47,
		seats_getter = 0x48de40, seats_activate_slot = 0x14, seats_update_slot = 0x68;
	constexpr std::uint32_t mesh_wait_call = 0x607a48, wait = 0x615a60, natneg_done = 0x607210;
	constexpr std::uint32_t natneg_reply = 0x6051ab, natneg_reply_resume = 0x6051b1, natneg_reply_skip = 0x6051e1;
	// The 0x25 mesh-reply handler's completion call (0x604e93 branch): stock binds the record of the participant being
	// added (manager+0x221c) to the reply's SENDER. The barrier-ack handler (opcode 0x5b, 0x6116d0) sets the sender's
	// record flag 0x400 through 0x614270 without testing 0x611110's null (the 4th-player crash at 0x614270).
	constexpr std::uint32_t mesh_reply_call = 0x604eb0, loaded_flag_call = 0x6116eb, set_flags = 0x614270, record_by_index = 0x6110c0;
	// The 0x4b ready / 0x4d not-ready handler (0x60c4d0) has the same unguarded 0x611110 lookups: 0x4b clears flag 0x10
	// on the result (and may read its index), 0x4d sets it.
	// GameSpy natneg completions (results 2 and 3) bind the same "being added" record to the natneg-reported REAL
	// address; with JoinInProgress every record already has its session-transport (room tunnel / GNS) peer.
	constexpr std::uint32_t natneg_done_call_2 = 0x6051a6, natneg_done_call_3 = 0x60524b;
	constexpr std::uint32_t ready_clear_site = 0x60c4f2, ready_clear_resume = 0x60c4fb, ready_clear_skip = 0x60c544, unready_set_call = 0x60c598;
	constexpr std::uint32_t peer_port = 0x8, peer_address = 0x14, address_ip = 0x8;

	// Game functions and fields the bring-in uses (retail; verified by the guards below).
	constexpr std::uint32_t session_getter = 0x612be0, first_node = 0x611160, record_index = 0x614210, manager_getter = 0x6097b0,
		message_init = 0x615810, send_to = 0x610f30, flush_peer = 0x610fe0, peer_connection = 0x55d7e0, game_getter = 0x46dce0, pause_getter = 0x60c7e0,
		zone_type = 0x60b9d0, scheduler_getter = 0x60b210, task_add = 0x60af70, task_remove = 0x60afe0, image_sender = 0x607fb0,
		set_image_done = 0x614420, game_alloc = 0x5606e0, game_free = 0x560700, zones_getter = 0x484990, zones_vtable = 0x68878c;
	constexpr std::uint32_t mode_slot = 0x268, zones_loading_slot = 0x24;
	constexpr std::uint32_t node_next = 0x8, node_record = 0x10, record_peer = 0x8, record_active = 0x14, record_flags = 0x1c;
	constexpr std::uint32_t manager_adding = 0x221c, manager_wait = 0x223c, manager_cursors = 0x2240, manager_image = 0x2250,
		manager_difficulty = 0x2294, manager_sender = 0x22b8, pause_loaded = 0x94;
	constexpr std::uint32_t flag_local = 0x1, flag_image = 0x2000;
	constexpr std::uint8_t opcode_start = 0x26, opcode_prepare = 0x47;
	constexpr std::uint8_t opcode_mesh_done = 0x25;
	// Solo story segments: a participant whose controller has no hero in the zone watches (the party camera follows
	// the solo hero on every PC) and gets the native timed message. hero_table: the controller -> hero handle table.
	constexpr std::uint32_t message_show = 0x60a600, entity_of_handle = 0x4654b0, hero_table = 0x70b814,
		record_controller_at = 0x30, record_index_at = 0x18, actor_controller = 0x3d4, zones_current_slot = 0x5c,
		zones_static = 0x72a578, game_static = 0x729960, game_vtable = 0x686e1c, zones_loading_at = 0x220, zones_name_at = 0x1e0, zone_name_size = 0x3f;
	constexpr std::uint32_t spectator_repeat_ms = 30000;
	// Follow-up 7: the timed-event manager (static 0x75d340, vtable 0x68de14, getter 0x4b2fc0; body = object + 4).
	// Every actor update is a timed event: the actor's members at +0x320/+0x328/+0x330 ({vptr, time}; vtable 0x682b9c
	// fires the actor update 0x434540 when the queued time equals the stored one), scheduled by 0x41d380 -> 0x41a730,
	// which builds a 24-byte record {owner handle, event*, payload} (0x41a090) and adds it through vt+8 (0x4b2b20:
	// record alloc 0x4b2c10 from the free ring, store 0x4b2b80, heap push 0x4b2c90). The game update pumps vt+0xc
	// (0x4b2d60 -> 0x4b2d70): pops the (time, index) binary heap at body+0x7c40 (count body+0x9f6c) while time < game
	// time and fires the record (0x4199f0). The heap compares TIMES ONLY (sift-up 0x4b2cd0, sift-down 0x4b2e10), so
	// the order of same-time events - every actor update of a frame - is the heap's shape, i.e. each process's own
	// push/pop history. Round 4: the same three brains re-armed in a different order on the two peers (frame 16/17).
	constexpr std::uint32_t event_manager_object = 0x75d340, event_manager_vtable = 0x68de14, event_manager_getter = 0x4b2fc0,
		event_record_alloc = 0x4b2c10, event_record_store = 0x4b2b80, event_heap_push = 0x4b2c90, event_pool_init = 0x4b2a10, event_heap_pop = 0x4b2de0;
	constexpr std::uint32_t event_body_at = 4, event_record_bytes = 24, event_records = 0x465, event_used_bitmap_at = 0x6978, event_ring_at = 0x6a08,
		event_ring_write_at = 0x7ba0, event_ring_read_at = 0x7ba4, event_ring_count_at = 0x7ba8, event_alloc_bitmap_at = 0x7bac, event_record_count_at = 0x7c3c,
		event_heap_at = 0x7c40, event_heap_count_at = 0x9f6c, event_body_bytes = 0x9f70;
	// vt+0xc is the pump 0x4b2d60; src/xml1_turrets.cpp installs its scheduler dispatch hook THERE before this module
	// (exports.cpp order), so the slot is accepted when it holds retail or that module's installed hook (follow-up 8;
	// round 5: the 16-byte vtable guard refused the whole install on every boot with Game.Xml1Turrets on).
	constexpr std::uint32_t event_manager_pump = 0x4b2d60, event_manager_pump_slot = event_manager_vtable + 0xc;
	// Follow-up 14 (review S2): an event object outside the executable image (a heap-allocated kind: round 10's
	// `kind 33a4568` host / `3454568` guest on the same owners) is keyed by its vtable pointer, which is static.
	constexpr std::uint32_t image_code_lo = 0x401000, image_code_hi = 0xa40000;
	// Host alone: the host menu's Start Game (0x5bacd0) refuses a room list of one; with rule 26 the host starts alone
	// and players drop in later (admission stays open after Start Game).
	constexpr std::uint32_t start_alone = 0x5bae34;
	inline constexpr std::array<unsigned char, 4> start_alone_original{0x83,0x78,0x0c,0x02}, start_alone_patched{0x83,0x78,0x0c,0x01};
	// Host keeps hosting: 0x612c40 ended the whole session when a participant was lost from a session of two (host +
	// the leaver); with rule 26 the host keeps the session (alone, listed) so a replacement can drop in. A lost host
	// still ends it for the joiners (0x6142a0 decides), and so does a joiner's own loss.
	constexpr std::uint32_t keep_hosting = 0x612d98;
	inline constexpr std::array<unsigned char, 1> keep_hosting_original{0x02}, keep_hosting_patched{0x01};
	constexpr std::uint32_t slow_player_counts = 0x79fb80, slow_player_bytes = 0x20;
	constexpr std::uint32_t seat_rebinding_call = 0x422618;
	inline constexpr std::array<unsigned char, 6> seat_rebinding_original{0xff,0x93,0x88,0x01,0x00,0x00};
	constexpr std::uint32_t seat_binding_call = 0x486d3b, game_seat_binding_slot = 0x188, game_party = 0x14, game_slot_controller = 0x514,
		game_slot_human = 0x524, name_text = 0x425bc0, seats_active_slot = 0x10, session_members = 0x278;
	inline constexpr std::array<unsigned char, 6> seat_binding_original{0xff,0x95,0x88,0x01,0x00,0x00};
	// A hero's AI flag (+0x3d8 bit 0, the manual switch 0x423eb0 tests it); its controller is actor_controller (+0x3d4).
	constexpr std::uint32_t hero_ai_flags = 0x3d8, hero_record = 0x35c; // +0x35c: the hero's registry record (live probe)
	// p060fix/AJ2 (live-060zb ZB-4Pb): +0x3d8 bit 0x20 is "a timed event of the static kind 0x6d5e30 is pending" - set once
	// by actor vt+0x1ec = 0x42a460 (`test 0x20; jne; or 0x20; 0x41a730(kind, time)`), cleared by the kind's fire 0x42ef30
	// (`and 0xdf`; a player hero (bit 0 clear) re-arms it every 0x3dcccccd s, an AI hero (bit 0 set) clears +0x774
	// instead). The zone-start binding gives a player hero the pending event and an AI hero none (0x70 / 0x51); a hero
	// released to AI at runtime by the participant drop keeps its pending event (0x71: 0x41da7e `or 1` only), so a seat
	// given up on the host before its reload and removed on a guest after its restore leaves that guest one more
	// queued event, popped at t=3.0 (frame 30): the heap shape and the think order parted at frame 31 (rng 160 vs 150).
	constexpr std::uint32_t hero_ai_bit = 0x01, hero_event_pending_bit = 0x20, hero_event_kind = 0x6d5e30,
		hero_event_arm = 0x42a460, hero_event_fire = 0x42ef30, hero_ai_switch_on = 0x41da60;
	constexpr std::uint32_t hero_registry_getter = 0x44b8f0, hero_registry_find_slot = 0x40;
	// The native slow-player guard (host only, 0x559190): a participant whose reported frame time is over 50 ms counts a
	// slow frame (300 good frames in a row reset the count); 200 -> PLAYER_LAG1, 600 more -> PLAYER_LAG2, 900 more ->
	// dropped here (session 0x6133a0(record, 0x10ce, 0x10cf, 0x10cf, 0), then every count reset). Over a long session a
	// PC hovering near 20 fps, or a lockstep that stalls everyone alike (the host's own index is counted first), reaches
	// it: the owner's three-PC test lost a joiner this way and a one-PC rig drops the host itself (B04). With rule 26 the
	// player stays; the warnings and the reset stay native.
	constexpr std::uint32_t slow_drop_call = 0x55933c;
	inline constexpr std::array<unsigned char, 5> slow_drop_original{0xe8,0x5f,0xa0,0x0b,0x00};
	constexpr float spectator_seconds = 6.0f;

	inline constexpr std::array<unsigned char, 5> teardown_original{0xe8,0x9d,0xe0,0xff,0xff};
	inline constexpr std::array<unsigned char, 5> gamespy_original{0xe8,0x4a,0xc5,0x00,0x00};
	inline constexpr std::array<unsigned char, 1> branch_original{0x74}, branch_patched{0xeb};
	inline constexpr std::array<unsigned char, 8> add_original{0x83,0xec,0x78,0xa1,0xf8,0x38,0x6f,0x00};
	inline constexpr std::array<unsigned char, 9> record_seats_original{0xe8,0x16,0xb3,0xe7,0xff,0x8b,0xf8,0x8b,0x1f};
	inline constexpr std::array<unsigned char, 5> frame_walk_original{0x8b,0x77,0x10,0x8b,0xce};
	inline constexpr std::array<unsigned char, 6> stream_walk_original{0x8b,0x4e,0x10,0x8b,0x7e,0x08};
	inline constexpr std::array<unsigned char, 5> loaded_original{0xe8,0x72,0x5a,0x02,0x00};
	inline constexpr std::array<unsigned char, 5> area_barrier_original{0xe8,0x1c,0xcb,0x18,0x00};
	inline constexpr std::array<unsigned char, 5> campaign_unregister_original{0xe8,0xd4,0xde,0xff,0xff},
		campaign_unregister_patched{0x90,0x90,0x90,0x90,0x90};
	inline constexpr std::array<unsigned char, 6> checksum_send_original{0x83,0xec,0x6c,0x53,0x8b,0xd9},
		campaign_begin_original{0x56,0xe8,0xba,0xa0,0xe7,0xff};
	inline constexpr std::array<unsigned char, 5> checksum_compare_original{0x83,0xec,0x20,0x53,0x55},
		campaign_receive_original{0x83,0xec,0x14,0x53,0x55};
	inline constexpr std::array<unsigned char, 9> broadcast_entry_original{0x55,0x8b,0xe9,0x8b,0x85,0x1c,0x04,0x00,0x00};
	inline constexpr std::array<unsigned char, 6> campaign_load_original{0xff,0x92,0x0c,0x02,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> watchdog_peer_original{0xe8,0xd6,0x5a,0xe5,0xff};
	inline constexpr std::array<std::array<unsigned char, 5>, 5> broadcast_local_originals{{{0xe8,0xb7,0x2f,0x00,0x00},{0xe8,0x17,0x2f,0x00,0x00},{0xe8,0x82,0x2e,0x00,0x00},{0xe8,0x77,0xd7,0x00,0x00},{0xe8,0xce,0xd8,0x00,0x00}}};
	inline constexpr std::array<unsigned char, 5> mesh_wait_original{0xe8,0x13,0xe0,0x00,0x00};
	inline constexpr std::array<unsigned char, 6> natneg_reply_original{0x8d,0x4c,0x24,0x0c,0x8b,0xf0};
	inline constexpr std::array<unsigned char, 5> mesh_reply_original{0xe8,0x5b,0x23,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> loaded_flag_original{0xe8,0x80,0x2b,0x00,0x00};
	inline constexpr std::array<unsigned char, 9> ready_clear_original{0x6a,0x10,0x8b,0xcd,0xe8,0x55,0x7d,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> unready_set_original{0xe8,0xd3,0x7c,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> natneg_done_2_original{0xe8,0x65,0x20,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> natneg_done_3_original{0xe8,0xc0,0x1f,0x00,0x00};
	inline constexpr std::array<unsigned char, 42> admit_controller_original{0xe8,0xd9,0x84,0xf4,0xff,0x8b,0x4c,0x24,0x0c,0x8b,0x10,0x6a,0x00,0x51,
		0x8b,0xc8,0xff,0x52,0x38,0x50,0x8b,0xce,0xe8,0xa3,0xe8,0xf2,0xff,0xe8,0x6e,0xbb,0x00,0x00,0x8b,0x10,0x6a,0x00,0x56,0x8b,0xc8,0xff,0x52,0x14};

	// 0x5d93f0 renders the online ready roster. vt+0x258 supplies the hero label;
	// 0x6141e0 supplies the separate participant name. The suffix call is cdecl.
	constexpr std::uint32_t roster_hero_call = 0x5d95bd, roster_format = 0x5604f0;
	inline constexpr std::array<unsigned char, 5> roster_hero_original{0xe8,0x2e,0x6f,0xf8,0xff};
	// Canonical cohort restart (p060fix/AE). The retail frame checksum (0x614a40) is hero_sum + camera + synchronized RNG
	// draw count. live-060y: at the first period after a late admission every hero transform matched (E1) but the camera
	// term differed by -70 in 4/4 runs (host eye-target yaw 22.6 deg, joiner 0.0) and the draw count by +70..+85. The
	// camera is a process-lifetime singleton (0x7171d0, constructed once in the getter 0x4457e0 at 0x445819, never
	// rebuilt by resetgame or the soft reset): its zone-start init (vt+0x74 0x4458a0) keeps the CURRENT yaw +0x74 unless
	// snapped (0x445958..0x44596f) and the full reset (vt+0x68 0x440c70) copies the yaw history +0x2f0 -> +0x2f4, so a
	// host that played carries its view into the restart while a cold joiner starts from the constructor's zeros.
	// The fix keeps a pristine image of the singleton right after its construction and returns every peer's camera to
	// it before the native campaign start of a cohort restart (deferred_campaign_start), so the restored zone's camera
	// init runs from identical inputs on every PC. The synchronized RNG is then reseeded by every peer at the first
	// shared simulation frame (the input-manager run after the frame gate passes, 0x606b98) with the native constant,
	// so the warm/cold zone-load draws before frame 0 cannot carry into the checksum.
	constexpr std::uint32_t camera_getter = 0x4457e0, camera_object = 0x7171d0, camera_guard_byte = 0x7176ac, camera_bytes = 0x4dc,
		camera_construct_call = 0x445819, camera_constructor = 0x4449e0, camera_vtable_value = 0x684cc4,
		camera_yaw_at = 0x74, camera_mode_at = 0x84, camera_yaw_history_at = 0x2f0, camera_flags_at = 0x4d8;
	inline constexpr std::array<unsigned char, 5> camera_construct_original{0xe8,0xc2,0xf1,0xff,0xff};
	constexpr std::uint32_t frame_gate_run_call = 0x606b98, input_manager_run = 0x5587c0, native_frame = 0xa3bbf4,
		seed_setter = 0x560150, native_seed = 0x845fed, rng_seed = 0x6e0874, rng_count = 0x7ad8b4;
	inline constexpr std::array<unsigned char, 5> frame_gate_run_original{0xe8,0x23,0x1c,0xf5,0xff};
	// A lobby START of a saved campaign sends the same 0x47 "prepare" (mode 2) as a late-join restart, and only the
	// guests enter the cohort epoch there (the host does not receive its own 0x47). The 8-byte body (mode u32,
	// saved-stats byte, session-kind byte, two unused bytes that are stack residue in retail) is built at
	// 0x6095a5..0x6095b5 inside Start Game 0x6094f0. The fix writes the two unused bytes on every host: the
	// late-restart marker while the host restarts a running session for a late joiner, zero at a lobby START.
	// Receivers gate the canonical restart on that marker; one side alone never restores or reseeds.
	constexpr std::uint32_t prepare_opcode_site = 0x6095a5, prepare_opcode_resume = 0x6095aa;
	inline constexpr std::array<unsigned char, 5> prepare_opcode_original{0xc6,0x44,0x24,0x30,0x47};
	// The host's native participant drop (thiscall session; record, notice code, removal reason, code, 0): the retail
	// slow-player guard's call (0x55933c) with the same codes. A seat given up after its native add is released with it.
	constexpr std::uint32_t participant_drop = 0x6133a0, drop_notice_code = 0x10ce, drop_removal_code = 0x10cf;
	// latejoin-residue P1/P2/P3 (O:/Projects/agent-work/latejoin-residue/RESULT.md): synchronized draws BEFORE frame 0
	// make zone choices (loading-screen chooser 0x487000, spawner 0x48adc0, the touched nudge 0x498ef0, drops 0x46e490,
	// AI), so a late restart reseeds at the start of each deterministic segment: CMap::LoadMap entry (0x484108, the
	// `call 0x4457e0` after the loading bits) and the zone-loaded finish entry (0x4867fe, its first call 0x596890),
	// both argument-less cdecl getters replaced by CALL-shape bridges that keep the site's return address. The AI
	// system's 32-entry brain free ring (0x78ba14 +0xc384[32], write +0xc408, read +0xc40c, free +0xc410) is only
	// initialised by the constructor (0x522f70, thiscall on the pool base); at a late restart's zone load, with every
	// brain free, it goes back to identity order. The probe ([Dev] LateJoinProbe=1) jumps from the synchronized
	// draw entry 0x560090 (`inc dword [0x7ad8b4]`, 6 bytes; resume 0x560096) into a bounded caller ring.
	constexpr std::uint32_t zone_load_entry_call = 0x484108, zone_load_entry_target = 0x4457e0;
	inline constexpr std::array<unsigned char, 5> zone_load_entry_original{0xe8,0xd3,0x16,0xfc,0xff};
	constexpr std::uint32_t zone_start_entry_call = 0x4867fe, zone_start_entry_target = 0x596890;
	// Follow-up 4 (probe round): the mode-2 restore reaches the zone through CMap::LoadZone (vt+0x4c 0x484010, called by the
	// CMap load 0x484b70), a sibling of LoadMap with the same tail: its camera getter call 0x484070 is the second zone-load site.
	constexpr std::uint32_t zone_load_zone_entry_call = 0x484070;
	inline constexpr std::array<unsigned char, 5> zone_load_zone_entry_original{0xe8,0x6b,0x17,0xfc,0xff};
	inline constexpr std::array<unsigned char, 5> zone_start_entry_original{0xe8,0x8d,0x00,0x11,0x00};
	constexpr std::uint32_t brain_ring_init = 0x522f70, brain_pool_stock = 0x78ba14, brain_ring_write = 0xc408, brain_ring_read = 0xc40c,
		brain_ring_free = 0xc410, brain_ring_bitmap = 0xc414, brain_slots = 32;
	// p060fix/AI: the brain alloc guard (0x522a10, 16 bytes) also covers the standalone ActorPools=124 install's W11
	// controller-reset relocation at 0x522a18 (a 5-byte JMP into its cave plus 2 pad bytes, written before this module
	// installs); guard_matches composes exactly the bytes actor_pools reports it committed there (stage 1, E4 off).
	constexpr std::uint32_t brain_ring_pop = 0x522a10, brain_ring_pop_relocated = 0x522a18;
	constexpr std::uint32_t draw_entry = 0x560090, draw_resume = 0x560096, draw_wrapper_float_return = 0x5601bd,
		draw_wrapper_int_return_a = 0x5601fb, draw_wrapper_int_return_b = 0x56023f;
	inline constexpr std::array<unsigned char, 6> draw_entry_original{0xff,0x05,0xb4,0xd8,0x7a,0x00};
	constexpr std::uint32_t world_static = 0x778b70, world_ring_write = 0x808, world_ring_read = 0x80c, world_ring_free = 0x810, world_live = 0x834;
	// Follow-up 3 (live-060y-ae rerun): the zone-loaded finish anchors the camera init (vt+0x74 0x4458a0) on the FIRST
	// resolved hero of the controller table 0x70b814 (0x4868f4..0x486924), and controller numbers are local to each PC:
	// the host's controller 0 is its own hero, the joiner's controller 0 is the joiner's. At a lobby START every hero
	// is stacked at the start point so the choice is moot; at a late restart the heroes stand at their image positions,
	// so the magnet search (0x446030) and the eye run from different anchors (host yaw 22.6 deg, joiner 0.0, with the
	// host's +0x74 already 0 before the restore). During a late restart the table base becomes a fix-owned copy of the
	// party list (game vt+0x11c, lockstep order) on every peer; otherwise the retail table.
	constexpr std::uint32_t camera_anchor_table_load = 0x4868f4, game_party_list_slot = 0x11c, party_list_count_at = 0x14;
	inline constexpr std::array<unsigned char, 5> camera_anchor_table_original{0xbb,0x14,0xb8,0x70,0x00};
	// Follow-up 5 (probe round 2: camera and rings canonical, the joiner still placed 4 more AI actors than the host at
	// E1, brains 16 vs 12 at "zone started", and made the actor touch-nudge draws 0x42974d at E2). A visited zone is
	// restored from its record by the zone loader (0x484ce0): the live actors through the dynamic list (0x465c50, the
	// 7-draw groups of the zone-load phase, equal on both peers) and the dormant monsters its spawners stored as
	// CSmartEntMgr records (0x48adc0 vt+0x7c; back into the manager before the placement loop). Those records are only
	// respawned by the manager's update (vt+4 0x4b7100: the records inside a box around the CAMERA, 750 units ahead,
	// 40 per call from a round-robin cursor at +4), whose one caller is the game update 0x46eef0 (game vt+0x16c) at
	// 0x46f2df - every tick, including the ticks between the zone-loaded finish and the first shared frame, where each
	// peer waits a different real time with a camera that drifts differently (the joiner spawned 4 more before frame 0
	// and 2 more at frame 0 before the re-anchor; the host never spawned them later: outside the re-anchored box).
	// During a late restart the call site hands the game update a stand-in whose update is a no-op from the zone load
	// to the frame-0 epoch, where the hold ends and the cursor goes to 0 on every peer, so the first respawn runs in
	// lockstep from the re-anchored camera. Only the one site calls vt+4 (xrefs of the getter: spawn 0x45fc51 and the
	// spawner store 0x48af03 allocate, 0x466xxx and 0x4843a0/0x484642 reset or re-intern records).
	constexpr std::uint32_t smart_update_call = 0x46f2df, smart_manager_getter = 0x4b76f0, smart_manager_vtable = 0x68e440,
		smart_cursor_at = 0x4, smart_count_slot = 0x14;
	inline constexpr std::array<unsigned char, 5> smart_update_original{0xe8,0x0c,0x84,0x04,0x00};
	// p060fix/AJ (live-060zb ZB-3P/ZB-3Pp): the site's `all` argument is `distance2(camera eye, game+0x1f0) > 10000`
	// (0x46f2b7..0x46f2d8, threshold [0x6872d0]) and the site stores the eye into game+0x1f0 after the update; the
	// zone-loaded finish sets game+0x1f0 to the far marker (1e30, 0, 0) through game vt+0x28 = 0x4685b0 (0x4869cd) so the
	// first update after a zone load examines EVERY record (plan: first 0, budget = count). The one world tick the hold
	// spans lands before the finish on some peers (the marker survives: all = 1 at frame 0, 115 records in one call) and
	// after it on others (the eye overwrites the marker: all = 0, 40 records per frame over frames 2..11) - a per-PC
	// timing, not a role: the frame-0 epoch therefore puts the marker back on every peer before the first real update.
	constexpr std::uint32_t game_respawn_anchor_at = 0x1f0, game_respawn_reset_slot = 0x28, game_respawn_reset = 0x4685b0,
		respawn_far_marker = 0x7149f2ca, respawn_sweep_threshold = 0x6872d0, respawn_marker_call = 0x4869c4;
	// The probe names every entity the zone spawns: the spawn-by-names entry 0x4612e0 (cdecl: definition, instance,
	// position, orientation, flag) is entered by JUMP with its first six bytes displaced (push ecx/ebx/ebp/esi; xor esi,esi).
	constexpr std::uint32_t spawn_entry = 0x4612e0, spawn_resume = 0x4612e6;
	inline constexpr std::array<unsigned char, 6> spawn_entry_original{0x51,0x53,0x55,0x56,0x33,0xf6};
	// Follow-up 14 (AE15): the AE12 world hold at 0x46f0a9 is gone - rounds 8-10 logged `0 world update(s) skipped`
	// on every peer in every admission (the world section does not run during the barrier wait).
	// p060fix/AQ (the join hold): the same gate, now from the host's image capture (an existing participant: the
	// late-restart 0x47) until this PC's own cohort restart begins, capped by [Online] JoinHoldSeconds. The game update
	// 0x46eef0 (game vt+0x16c, called from the main-loop iteration 0x401d70 at 0x40235a) runs its whole world section
	// 0x46f0ca..0x46f60d (the timed-event pump 0x4b2fc0 vt+0xc, countdowns, physics/collision, the camera update, the
	// smart-entity update 0x46f2df, the AI) behind the 7-byte `test byte ptr [esi+0x728], 2` at 0x46f0a9 and its
	// `je 0x46f60d` (0x46f094 also jumps to the test; the second gate 0x46f0b6..0x46f0c4 follows). Before the test only
	// the clock reads, the slow-motion scale (+0x3ec/+0x3fc..+0x40c) and game time +0x3e8 (0x46f079) run; after
	// 0x46f60d only the epilogue. The scheduler pump 0x401f74 (the network tasks, this module's tick, the native
	// message timers), the input manager update 0x40219e and rendering stay outside it, so the window stays alive.
	// A CALL-shape bridge over the test returns ZF=1 (skip) while held and retail's own test otherwise.
	constexpr std::uint32_t world_gate_site = 0x46f0a9, world_gate_flag_at = 0x728, world_gate_flag_bit = 2;
	inline constexpr std::array<unsigned char, 7> world_gate_original{0xf6,0x86,0x28,0x07,0x00,0x00,0x02};
	// AQ3: the main loop UNPAUSES the simulation clock BEFORE the update samples it at 0x46f079. Retain the native
	// load pause while held. Replace this complete 12-byte call block; the next game getter is live.
	constexpr std::uint32_t hold_clock_site = 0x402345, hold_stall_message_call = 0x4029f0, native_stall_message = 0x60a5d0;
	inline constexpr std::array<unsigned char, 12> hold_clock_original{0xe8,0xc6,0x92,0x15,0x00,0x8b,0x10,0x8b,0xc8,0xff,0x52,0x20};
	inline constexpr std::array<unsigned char, 5> hold_stall_message_original{0xe8,0xdb,0x7b,0x20,0x00};
	// The hold screen: the native timed network message (0x60a600, scheduler 0x60b210: three slots of 16 bytes at
	// +0x1c8 {text, priority, expiry, seconds}, current slot +0x1f8; drawn by the main-loop render path 0x4029f0 and
	// the menu layer 0x59f7d7 through the getter 0x60a5d0; expired by 0x60a690 from the network message tick 0x60aa7e).
	// The hold shows its text there and, when it ends, expires it through 0x60a690 if it is still the current one.
	constexpr std::uint32_t message_clear = 0x60a690, message_slots_at = 0x1c8, message_current_at = 0x1f8, message_slot_bytes = 16,
		message_slot_count = 3;
	constexpr std::int32_t join_hold_priority = 6; // above the solo-segment notice (5)
	// (1) The camera at frame 0. Every round's frame-0 camdump (11 admissions, ae11/ae13/ae14) had 49-74 dwords
	// differing between the peers after the re-anchor - eye/target/history copies by 0.66 units or a few ULPs -
	// because vt+0x74 (0x4458a0) takes the party target from the seat manager's vt+0x58 (0x48e120, which reads the
	// camera back: vt+0x88, vt+0xd8) and the yaw through 0x43f150 from the CURRENT eye/target (+0xe0/+0x104: fpatan at
	// 0x43f1f8) and the history +0x2f0..: per-peer state the loading-screen updates had drifted. The spans below are
	// the union of every dword that differed between the peers at frames 0/1/2 in those 11 pairs (camunion.py over
	// the camdumps; 236 of the 311 dwords never differed) plus the current yaw +0x74 (the init keeps it unless snapped)
	// and the head of the matrix at +0x18c (vt+0xd0 0x43fe60 writes +0x18c..). At the frame-0 epoch every peer puts
	// these bytes back from the pristine image before vt+0x74 runs from the party anchor, so the init runs from
	// identical inputs on every peer; the other 205 dwords were equal on both peers in every pair.
	struct camera_span { std::uint32_t at, bytes; };
	// AE16 (O:/Projects/agent-work/padswitch-re/RESULT.md): the controlled hero's move direction is stick x camera BASIS
	// (0x42dbf0 -> camera vt+0xcc 0x43fe00 copies +0x2cc/+0x2d8/+0x2e4; 0x447730 rewrites all nine dwords each frame) and
	// the retail camera term is ONE _ftol of six summed components, blind to ULPs - so +0x2cc..+0x2f8 is one span (the
	// +0x2d8 dword sat between the two earlier spans).
	inline constexpr std::array<camera_span, 11> camera_derived_spans{{
		{0x010, 0x18}, {0x074, 0x04}, {0x088, 0x0c}, {0x0e0, 0x48}, {0x18c, 0x3c}, {0x1c8, 0xc4},
		{0x2cc, 0x2c}, {0x414, 0x0c}, {0x458, 0x0c}, {0x484, 0x0c}, {0x4cc, 0x0c}}};
	constexpr std::uint32_t camera_basis_at = 0x2cc, camera_basis_bytes = 0x24, input_object_pointer = 0xa6abfc, input_stick_floats_at = 0xc610;
	// (2) E2's touch-nudge (O:/Projects/agent-work/e2-settle-re/RESULT.md): CEntity vt+0xac is "OnAttached(child)";
	// CActor's override 0x429710 draws three bools (0x42974d/63/73) and sets the CHILD's move target (+0x120/+0x138
	// through 0x473640) when a character lands on an actor; the only entity-to-entity caller is 0x419f20 ("set ground
	// entity": when the child's +0xbc changes to a non-world entity it resolves the parent, calls parent->vt+0xac(child)
	// at 0x419f8d and stores +0xbc), reached from FindGround 0x470bb0 inside the physics steps (0x496f20 vt+0x128,
	// 0x472cf0). A fresh actor has +0xbc = 0, so its FIRST non-world ground touches; the cold joiner's finish (110 ms)
	// ran such a step for Storm before `zone started`, the warm host's (13 ms) did not. During an armed late restart a
	// JMP-shape bridge at the override's entry (its 8-byte prologue displaced, position-independent) answers `ret 4`
	// for a party hero (the +0xbc store in 0x419f20 still happens; no draw, no move target); at the frame-0 epoch every
	// peer puts each party hero's +0xbc back to 0 and runs FindGround (thiscall, no arguments) in party order, so the
	// same ray, ground and - if an actor is below - the same nudge draws happen on every peer at the same count.
	constexpr std::uint32_t attach_override = 0x429710, attach_override_resume = 0x429718, find_ground = 0x470bb0, set_ground_call = 0x419f88;
	inline constexpr std::array<unsigned char, 8> attach_override_original{0x83,0xec,0x18,0x56,0x8b,0x74,0x24,0x20};
	constexpr std::uint32_t actor_ground_at = 0xbc, actor_ground_height_at = 0x200;
	// AE16: the actor physics step's time stamps and velocity pair (my listing of 0x496f20 / 0x476220): +0x2f0 is compared
	// with the game time at 0x4971c9..0x4971d7 (gravity held until then), +0x28c is `now - last fall` at 0x497287 and is
	// rewritten `:= now` at 0x497305, +0x150 is the per-actor last update time (dt = t - +0x150 at 0x476226, rewritten at
	// 0x476295), +0x120/+0x138 the velocity pair 0x473640 writes, +0x12c/+0x144 the angular pair (0x475f50). A warm host's
	// heroes can carry stamps from the pre-restart clock (the clock restarts at 2.0) that a cold joiner's cannot; at the
	// frame-0 epoch every peer makes them canonical. +0x278 (gravity scale), +0x254 (mass) and +0x259 (flags) are not
	// velocity and stay; CHero +0x808 (auto-move flag) is logged, not written.
	constexpr std::uint32_t actor_velocity_at = 0x120, actor_angular_at = 0x12c, actor_velocity_prev_at = 0x138, actor_angular_prev_at = 0x144,
		actor_last_update_at = 0x150, actor_last_fall_at = 0x28c, actor_gravity_hold_at = 0x2f0, hero_auto_move_flag_at = 0x808, game_time_at = 0x3e8;
	// (3) The AE14 pre-placement moved pass ran on an EMPTY party list (0x4867fe is before the heroes are placed by
	// game vt+0x190 at 0x486884 and bound by 0x422520 at 0x4868a5: round 10 `0 hero(s)` on both peers). It now runs
	// from a CALL-shape bridge over the argument-less getter call right after that binding (0x4868ad `call 0x495350`),
	// before the finish's settle loop and the first physics step.
	constexpr std::uint32_t party_bound_call = 0x4868ad, party_bound_target = 0x495350;
	inline constexpr std::array<unsigned char, 5> party_bound_original{0xe8,0x9e,0xea,0x00,0x00};
	// Follow-up 12 (round 8): the joiner alone holds a queued "moved" event (static kind 0x6d7c5c) for its OWN hero at
	// frame 0, and the host alone registers them for the AI heroes at frames 1-3. The moved check is the entity's
	// vt+0xf4 = 0x471740(time): when the position +0x20 is farther than [0x68012c] from the last-known +0x1e8, or the
	// orientation +0x2c from +0x1f4 ([0x682a2c]), or 0x470b30 says so (+0x23c nonzero, or +0x240 != -1 and due), and
	// flag bit 2 of +0x258 is clear, it sets the bit and schedules the kind at game time + arg (0x41a730; the kind's
	// payload getter 0x419c40 returns size 0, so the record's 16 payload bytes are stale residue - never compared).
	// The fire 0x4729b0 clears the bit, calls vt+0x148 (last-known refresh through 0x471400, a position notify for a
	// non-AI hero), vt+0x120 and vt+0xf4(0) again. At the frame-0 epoch every peer makes the party heroes' state
	// canonical: last-known = current, the pending bit clear, and the rebuild drops their queued moved events.
	constexpr std::uint32_t moved_check = 0x471740, moved_fire = 0x4729b0, moved_event_kind = 0x6d7c5c;
	constexpr std::uint32_t actor_position_at = 0x20, actor_orientation_at = 0x2c, actor_last_position_at = 0x1e8, actor_last_orientation_at = 0x1f4,
		actor_moved_flags_at = 0x258, actor_moved_pending_bit = 0x2, actor_moved_extra_a = 0x23c, actor_moved_extra_b = 0x240, actor_vector_bytes = 12;
	// p060fix/AK (live-060zd ZD-3P-E2p): the actor update 0x434540 re-arms its own timed event at its tail
	// 0x434fc3..0x43500c - every frame (0.001 s) while the entity's +6 bit 0 is set or a move speed (+0x180 / +0x184,
	// `fld; fcomp 0.0; test ah,0x41; je`) is positive, else every 0.1 s (0x3dcccccd through vt+0xf8). The move goal
	// (+0x1d0 goal, +0x1dc start, both FLT_MIN when none) and the two speeds are set together (0x422840) and cleared
	// together by 0x421b90 (`clear_goal`: FLT_MIN into +0x1d0 and +0x1dc, 0 into +0x180 / +0x184); the AI move driver
	// 0x424b70 reads them (0x424c8e / 0x424ca1) after the "has a goal" test 0x423ba0 and the follow handle +0x3ec.
	// The frame-0 epoch clears every non-party AI actor's goal and speeds alike (the CActor vtable 0x682ccc, controller
	// +0x3d4 = -1), so the first update after frame 0 re-arms every NPC at the same cadence on every peer; the party
	// heroes keep their own passes. The world's entities are walked by dense index through CWorld vt+0x10
	// (entity_by_dense 0x4c68d0; the installed world's getter 0x4c7f20).
	constexpr std::uint32_t actor_move_speed_a_at = 0x180, actor_move_speed_b_at = 0x184, actor_move_goal_at = 0x1d0, actor_move_start_at = 0x1dc,
		actor_follow_handle_at = 0x3ec, actor_entity_flags6_at = 0x6, actor_fast_update_bit = 0x01, actor_vtable = 0x682ccc, actor_move_goal_clear = 0x421b90,
		actor_update_rearm_tail = 0x434fc3, actor_update_rearm_tail_bytes = 0x49, world_entity_by_index_slot = 0x10, move_goal_none_bits = 0x00800000; // FLT_MIN
	// AM / R1: mirror 0x45fe40 via the installed world; never dispatch SetActive or any entity/grid callback.
	constexpr std::uint32_t entity_flags_at = 4, entity_flag_bytes = 16, entity_registered_at = 0x6c, entity_registered_bit = 1,
		world_flag_update_slot = 0x3c, world_flag_members_slot = 0x40, world_flag_record_slot = 0x44, entity_active_flag = 16;
	constexpr std::uint32_t visibility_cache_clear_slot = 0x38;
	inline constexpr std::array<guard, 158> guards{{
		{0x402345,"e8c69215008b108bc8ff5220e88ab90600", "AQ3: main-loop simulation clock unpause before the game update"},
		{0x4029f0,"e8db7b200085c07510e8024320008a480684c90f8447010000", "AQ3: native stall renders only with a network message or its forced-draw byte"},
		{0x46f0a0,"520c8b088b01ff5020f68628070000020f8457050000e885ed0100", "AQ: game update: the world section's gate (bit 1 of game+0x728 tested at 0x46f0a9), its je, the second gate's getter call"},
		{0x46f0b0,"0f8457050000e885ed01008b108bc8ff522884c00f8443050000", "AQ: game update: both world-section gates jump to 0x46f60d (the section's end)"},
		{0x60a690,"568bf18b86f8010000c1e004c78430c8010000000000008b8ef8010000c1e104c78431cc01000000000000", "AQ: native network message expiry of the current slot (+0x1f8 index, +0x1c8 text, +0x1cc priority)"},
		{0x464330,"8b4424046bc03c03c132c988883d08010088883f08010033c90558080100b2ff8850e48850e6890889480489480889480c89481089481489481889481cc20400", "AM/ZF: full grid query-cache clear: only invalid extents and zero bitmap, no calls"},
		{0x686c8c,"30434600", "AM/ZF: grid vt+0x38 is cache clear, not reset/remove"},

		{0x45fe40,"f6416c017418a1308471008d510452518b491c8bd4890a8b108bc8ff523cc3", "AM: registered entity flag notification; only world vt+0x3c"},
		{0x68f848,"90694c0050694c0070694c00", "AM: CWorld flag update, bitmap getter, cache getter slots"},
		{0x4c6990,"8b44240453558bd9568d8b908500005725ff000000bf02000000894c24148bff8d4ffe8bd183e11fc1fa05be01000000d3e68b4c24188b2c918d14828b8c936c75000023ee85ce0f95c185ed0f95c23ad174298b5424148bc8c1f9058d548ae08bc883e11fbe01000000d3e685ed8b0a74040bceeb04f7d623ce890a8d4fff8bd183e11fc1fa05be01000000d3e68b4c24188b2c918d14828b8c936c75000023ee85ce0f95c185ed0f95c23ad174288b5424148bc8c1f9058d148a8bc883e11fbe01000000d3e685ed8b0a74040bceeb04f7d623ce890a8bcf83e11fbe01000000d3e68b4c24188bd7c1fa058b2c918d14828b8c936c75000023ee85ce0f95c185ed0f95c23ad174298b5424148bc8c1f9058d548a208bc883e11fbe01000000d3e685ed8b0a74040bceeb04f7d623ce890a8d4f018bd183e11fc1fa05be01000000d3e68b4c24188b2c918d14828b8c936c75000023ee85ce0f95c185ed0f95c23ad174298b5424148bc8c1f9058d548a408bc883e11fbe01000000d3e685ed8b0a74040bceeb04f7d623ce890a8b74241483c70481c6800000008d4ffe83f970897424140f8c75feffff8b5424186a10c1e004528d84186c75000050e87eaaf3ff83c40c5f5e5d5bc20800", "AM: CWorld flag update: only bitmaps and 16-byte cache copy"},
		{0x4015d0,"8b4c240c568b74240c578b7c240c8bc18bd7c1e902f3a58bc883e103f3a45f8bc25ec3", "AM: flag record copy helper: rep movsd / movsb only"},
		{0x4c6950,"8b442404c1e0058d840870850000c20400", "AM: CWorld flag bitmap getter"},
		{0x4c6970,"8b442404c1e0048d84086c750000c20400", "AM: CWorld dense flag record getter"},

		{0x42a460,"8a81d8030000a82075170c208881d8030000", "actor vt+0x1ec: +0x3d8 bit 0x20 = the static kind 0x6d5e30 is pending; set once, then scheduled"},
		{0x42ef3d,"8a96d803000080e2df8ac2a8018896d80300007511", "kind 0x6d5e30 fire: clears +0x3d8 bit 0x20, branches on the AI bit (player: re-arm through vt+0x1ec; AI: +0x774 := 0)"},
		{0x41da63,"f686d803000001751951", "AI switch-on 0x41da60: tests the AI bit and ORs it (bit 0x20 untouched: a released player hero keeps its pending event)"},
		{0x434fc3,"f64506015f5e5b7540d98580010000d81d30006800dfe0f6c441742dd98584010000d81d30006800dfe0f6c441741a8b450068cdcccc3d8bcdff90f80000005d81c48c000000c20400", "actor update re-arm tail: +6 bit 0 or a positive move speed (+0x180 / +0x184) keeps the actor on every frame, else vt+0xf8(0.1 s)"},
		{0x421bb0,"8d8ed0010000ff152cfa67006800008000680000800068000080008d4c2410ff15c8f96700508d8edc010000ff152cfa670033c0898680010000898684010000" "5e83c40cc3", "clear_goal 0x421b90 body: FLT_MIN into the goal +0x1d0 and the start +0x1dc, 0 into the speeds +0x180 / +0x184 (the values the epoch writes to every NPC)"},
		{0x4685b0,"83ec0c566a006a008bf168caf249718d4c2410ff15c8f96700508d8ef0010000ff152cfa6700", "game vt+0x28 (0x4685b0): the far marker igVec3f(1e30, 0, 0) assigned into game+0x1f0 - the next dormant-entity update examines every record"},
		{0x46f2b7,"8dbef0010000578d4c2430c644241400ff15ccf96700d81dd0726800dfe0f6c441", "game update: all = distance2(camera eye, game+0x1f0) > [0x6872d0] decided before the respawn site (the eye is stored into game+0x1f0 after it)"},
		{0x6872d0,"00401c46", "the respawn sweep threshold 10000.0f"},
		{0x4869c4,"e81773feff8b108bc8ff5228", "zone-loaded finish: game vt+0x28 sets the far marker after the party is bound (the first update after a zone load sweeps everything)"},
		{0x429710,"83ec18568b74242085f60f8484000000", "CActor OnAttached override (vt+0xac): prologue, the child argument, its null test"},
		{0x419f88,"8b10568bc8ff92ac0000008b078986bc000000", "set ground entity: parent->vt+0xac(child) then the +0xbc store"},
		{0x470bb0,"83ec2c5356578bf16a008d4c2410e8ad", "FindGround entry (thiscall, no arguments; the downward ray from +0x20)"},
		{0x4868a5,"e876bcf9ff83c408e89eea00008b106a008bc8ff5204", "zone-loaded finish: the party binding 0x422520, then the argument-less getter call the cold pass bridges"},
		{0x471740,"568bf18d4620508d8ee8010000ff15ccf96700d81d2c016800dfe0f6c4057a2c8d4e2c518d8ef4010000", "entity moved check: position +0x20 vs last-known +0x1e8, orientation +0x2c vs +0x1f4"},
		{0x47178c,"8a8658020000a80275198b542408520c02685c7c6d008bce888658020000e8818ffaff", "entity moved check: +0x258 bit 2 pending flag, the static kind 0x6d7c5c scheduled"},
		{0x4729b0,"568b74240880a658020000fd", "moved event fire: clears +0x258 bit 2 before re-registering the owner"},
		{0x46f2d8,"7505c644241001e80c8404008b4c24108b10518bc8ff5204", "game update: the CSmartEntMgr getter call and its vt+4 update (all flag) every tick"},
		{0x4612e0,"5153555633f6e8d5120000", "spawn by definition / instance names: entry prologue"},
		{0x68e440,"d0764b0000714b0000704b00e0704b00c06f4b00b06f4b00f06f4b00c0754b00", "CSmartEntMgr vtable: destructor, update, allocate, reset, clear, count, at, append"},
		{0x484069,"02889620020000e86b17fcff8b108bc8ff5204e88f750d", "CMap::LoadZone: loading bit 2, camera getter call, camera vt+4, clock getter"},
		{0x4868f4,"bb14b8700033f6eb038d490083fefc7e0783fe10", "zone-loaded finish: the camera anchor walks the controller hero table from slot 0"},
		{0x486968,"e873eefbff8b10578bc8ff5274", "zone-loaded finish: camera vt+0x74 init with the first resolved hero"},
		{0x484101,"808e2002000003e8d316fcff8b108bc8ff5204e8f7740d00", "CMap::LoadMap: loading bits, camera getter call, camera vt+4, clock getter"},
		{0x4867f8,"535556578be9e88d0011008b106a018bc8ff5224e8cf74fe", "zone-loaded finish: prologue and its first call (CAutoMap getter) before players are placed"},
		{0x522f70,"5633f65789b108c4000089b10cc4000089b110c4000033d2", "AI system free-ring init: write/read/free reset then 0..31 pushed"},
		{0x522a10,"8bd18b820cc400008b848284c3000056", "AI brain alloc pops the free ring at the read index"},
		{0x560090,"ff05b4d87a008b0d74086e00", "synchronized draw entry: count then seed"},
		{0x6133a0,"568b74240885f6578bf974318bcee85d", "native participant drop: notice (index, codes) then the lost-participant removal"},
		{0x60959a,"8d4c24188d4424106aff51c6442430478944242066c74424280800c644242e01", "0x47 prepare body (8 bytes, two unused tail bytes at body+6) and its opcode write at [esp+0x30]"},
		{0x445804,"0905ac7671006a00b9d0717100c744240c00000000e8c2f1ffff", "camera singleton: guard bit 0x7176ac, this = 0x7171d0, the one-time constructor call"},
		{0x4449e0,"83ec1053568bf1578d4e04c706c44c68", "camera constructor entry installs vtable 0x684cc4"},
		{0x444e21,"5e5b83c410c20400", "camera constructor is thiscall with one stack argument"},
		{0x4458a0,"8b44240483ec5c85c05356578bf10f84", "zone-start camera init from the hero's facing and the party target"},
		{0x445958,"84c0bb010000007408849eda04000074098b86f4020000894674", "zone-start camera init keeps the current yaw +0x74 unless snapped"},
		{0x440c70,"8b4424085355568bf18b08898e880000008b", "camera reset entry (target, eye); copies yaw history +0x2f0 -> +0x2f4"},
		{0x606b8f,"6a01e8fa37f5ff8bc8e8231cf5ff", "frame gate passed: the input manager runs this frame's inputs"},
        {0x610fe0,"8b891c04000085c97405e951e9ffffc20400","session flush-one-peer wrapper; refusal output before peer retirement"},
		{0x6106f0,"8a442404884119c20400", "native refusal marks the pending peer disconnected"},
		{0x609970,"83ec1c5357", "late-admission policy before participant allocation"},
		{0x614b05,"d940208b4828", "checksum raw actor transform observer"},
		{0x614b77,"8b44247803fe", "checksum native hero contribution observer"},
		{0x614bad,"d900d94004", "checksum raw camera vector observer"},
		{0x614bd4,"e8a7b5f4ff03f8", "checksum native RNG count and final sum observer"},
		{0x560180,"a1b4d87a00c3", "checksum RNG component is the deterministic draw count"},
		{0x560150,"8a44240884c074148b442404a374086e00c705b4d87a0000000000", "deterministic seed setter also clears the draw count"},
		{0x614b8a,"e8510ce3ff8b108d4c2420518bc8ff92d4000000", "checksum camera first vector stored on native stack"},
		{0x614b9e,"e83d0ce3ff8b108bc8ff9284000000", "checksum camera second vector returned by reference"},
        {0x5e4b4e,"e8ad76020084c0", "Team action checks the current cohort not-ready flags"},
        {0x5e4c21,"e8da75020084c0", "Team alternate action checks the current cohort not-ready flags"},
        {0x60aaec,"e80f17000084c0750d", "host pause scheduler resumes when the current cohort is ready"},
        {0x60c200,"56e8da6900008bc8e8534f00008bf085f674148b4e10e865800000f60010", "native any-unready walks participant flags 0x10"},
		{0x5d950c,"e8ffac0300508bceff9758020000", "ready roster: participant index selects held hero display name"},
		{0x5d95a3,"8d9424bc0000005268f8e069008d8424c4010000688000000050e82e6ff8ff83c410", "ready roster: bounded cdecl hero suffix formatting"},
		{0x5604f0,"56578b7c240c85ff750d", "native bounded formatter cdecl entry"},
		{0x559313,"813cc580fb7900840300000f85460100005368cf10000068cf10000068ce10000056e8a6980b008bc8e85fa00b00","slow-player guard: the 900-frame test and the native drop call it replaces"},
		{0x423ed1,"f681650700000875158a91d8030000b00184d07409","manual switch eligibility: target +0x765 bit 3, +0x3d8 bit 0 (AI)"},
		{0x46b493,"e85804feff8b3685f68be8","native seat binding: hero registry getter before its vt+0x40 lookup by name"},
		{0x612b25,"e816b3e7ff8bf88b1f8bcee8db160000508bcfff5314e800b3e7ff8b108bc8ff5268","participant constructor activates and reconciles a hero seat before admission returns"},
		{0x48de40,"8a0d20c57200b80100000084","native hero-seat manager getter entry"},
		{0x48de5f,"b8f0c47200c3","native hero-seat manager singleton return"},
		{0x6898d8,"d0d24800","native hero-seat activation virtual slot"},
		{0x68992c,"20ea4800","native hero-seat reconciliation virtual slot"},
		{0x48d2d0,"568b74240885f6578bf97c1ae8ef4b0c","native hero-seat activation thiscall ABI"},
		{0x48ea20,"83ec28535556578bd9e8b24118008bc8","native hero-seat reconciliation thiscall ABI"},
		{0x55b230,"33c089413089413489412889412cc3","native clock reset clears pending and fixed frame steps before cohort setup installs 30 Hz"},
		{0x55b610,"64a1000000008a0d88c27a00","native clock getter entry must not be detoured by an unowned hook"},
		{0x55b65c,"b848c27a0064890d0000000083c40cc3","native clock getter returns its singleton and restores the exception chain"},
		{0x550d10,"c6412401c3","native controller resume after an existing world's teardown"},
		{0x60c230,"53568bf18a466c8b4c240c33db3ac157","native pause toggle balances input-manager state before reinitialization"},
		{0x614250,"8b4424048b511cf7d023d089511cc20400","native participant flag clear used when resetting the load epoch"},
		{0x60c4df,"8b46105550e8f76600008bc8e8204c00008be86a108bcde8557d0000","native 0x4b clears the loaded participant flag even outside the team menu"},
		{0x60d0b0,"53568bf18a466433db3ac37407","native pause/load initialization is gated by byte +0x64"},
		{0x606fb0,"568bf18b865022000085c00f849f000000","native startloadedonlinegame entry"},
		{0x613e51,"f6c3100f84930000008b862404000083e0fc83c810","network soft reset 0x10 resets clock/input and clears zone phases"},
		{0x60700d,"8b1057518bc8ff920c0200008a8694220000","native startloadedonlinegame buffer/mode call"},
		{0x611270,"558be98b851c04000085c07506","native broadcast entry; retire cohort sender before broadcasting start"},
		{0x6096c0,"53568bf18a0633db3ac30f84d7000000","native campaign manager active byte gates sender cleanup"},
		{0x614a40,"83ec6c538bd9895c2404e891e1ffff","checksum sender entry; held only during a shared campaign restart"},
		{0x614810,"83ec20535533ed56578bf9896c2420","checksum comparison entry; held only during a shared campaign restart"},
		{0x608630,"83ec1453558b6c24208a4500","campaign G/H/I/J receiver entry"},
		{0x5f3c20,"56e8baa0e7ff8b106a006a01","actual native campaign start entry"},
		{0x6051b1,"e85a060100558d542410c644242025c644241e01528bcee80341e6ff","natneg completion: reliable empty 0x25 to the completed peer"},
		{0x604e93,"3c25754f8b4f10e821b800008b4f1050e8f890ebff50e8024900008bc8e85b230000","0x25 mesh reply: completion for the participant being added, at the sender's address"},
		{0x6116d0,"8b442404568b700c8b401057508bcee82cfaffff68000400008bc8e8802b0000","0x5b barrier ack: the sender's record flag 0x400 without a null test"},
		{0x60c4d0,"53568b74240c8a063c4b8b5e0c756b8b46105550e8f76600008bc8e8204c00008be86a108bcde8557d0000","0x4b ready: the sender's record flag 0x10 cleared without a null test"},
		{0x60c57f,"803e4d75198b461050e8536600008bc8e87c4b00006a108bc8e8d37c00005e33c05b","0x4d not ready: the sender's record flag 0x10 set without a null test"},
		{0x609750,"e8abfcffff8bcee8d4deffff8b86b8220000","campaign teardown: unregister G/H/I/J after the first start"},
		{0x46e2c2,"8b7c241483ff017438","native load mode 1 is stats only; mode 2 restores the campaign"},
		{0x484de3,"6800007042e8f3dd18008bc8e81ccb1800","synchronous area load: native online barrier before loading the area"},
		{0x609400,"51a1f8386f005657894424088bf1e89de0ffff8bcee816e1ffff","start-game teardown prologue and admission teardown call"},
		{0x609420,"33ff3bc774118b4e04506a2ce8af5d000089be0c230000","opcode 0x2c unregister"},
		{0x609437,"8b86002300003bc774118b4e04506a18e8945d000089be00230000","opcode 0x18 unregister"},
		{0x6074b0,"568bf18b86c422000057","admission teardown entry"},
		{0x609467,"c7861c220000ffffffffe84ac50000","GameSpy session teardown call"},
		{0x6159c0,"e85bfafeff8bc8e9b4fafeff","GameSpy session teardown thunk"},
		{0x604da0,"8b4424048b0851e894a2020083c404e9dc710300","GameSpy task peer lookup, peerThink and natneg pump"},
		{0x6055a6,"8b0e51e8d299020083c404e81a6a0300891e","GameSpy shutdown clears the peer after removing its task"},
		{0x60aab0,"8b70088b40108b500452ff1083c40485f68bc675eb","ordinary scheduler saves next before dispatch"},
		{0x60542f,"8b0d247ea3000bc833c0890d247ea300a3807da300","online manager static at 0xa37d80"},
		{0x609476,"8b86ac2200003bc7741350","lobby tick task removal"},
		{0x6075db,"2200003bc774118b4e04506a1fe8f3","opcode 0x1f unregister on a started joiner"},
		{0x608350,"83ec78a1f8386f005355568bb4248800","the host's participant add"},
		{0x6099e9,"e822a800008944240ce8d984f4ff8b4c240c8b106a00518bc8ff5238508bcee8a3e8f2ffe86ebb0000","admission: the newcomer's controller slot"},
		{0x6112c7,"8bcee8222f000084c074468bcee8b72f000084c0753b","broadcast mode 0: active and local tests"},
		{0x611365,"740b8bcee8822e000084c074558bcee8172f000084c0","broadcast mode 1: active and local tests"},
		{0x6113fa,"740b8bcee8ed2d000084c074558bcee8822e000084c0","broadcast mode 2: active and local tests"},
		{0x606b08,"8b781085ff8b6808746b8bcfe877d7000084c075608bcfe8ecd60000","frame gate: every remote record's input for this frame"},
		{0x6069b5,"8b78108b58088bcfe8ced8000084c07523","frame resync (mode 2): every remote record's buffered input"},
		{0x6137f0,"8b73108bcee8d65ae5ff8b5b088be885ed0f846a010000","stall watchdog: each record's peer"},
		{0x5591f6,"5556eb068d9b000000008b77108bcee806b00b008b54","frame timing walk over the participant records"},
		{0x5594ec,"8b7c24303bfb0f8508fdffff","frame timing walk's next record"},
		{0x55a2df,"e87c6e0b008bf085f6744553578d6424008b4e108b7e08e815","per-player stream walk"},
		{0x55a327,"85ff8bf775c35f5b5e","per-player stream walk's exit"},
		{0x5e68d2,"e8095f02008bc8e8725a020084c00f84a2050000","load tick all-loaded query"},
		{0x60c350,"56578bf9e8876800008bc8e8004e00008bf085f6","all-loaded query"},
		{0x607a3a,"8bcee8cfc70000506a006a018bcde813e000008b4f38518b","0x1f handler: mesh peer wait"},
		{0x605199,"8d4c24205051e80c4600008bc8e8652000008d4c240c8bf0e85a","natneg completion and its 0x25 reply"},
		{0x605238,"52e8eecc0600508d44242450e8674500008bc8e8c01f00008d4c240c8bf0","natneg completion (result 3) and its 0x25 reply"},
		{0x607210,"83ec1ca1f8386f00568bf18944241c8b","natneg completion for the participant being added"},
		{0x6094f0,"83ec30568bf18d4c240c","Start Game (campaign image sender set-up)"},
		{0x607fb0,"83ec24568d4c2414e853d80000","campaign image sender task"},
		{0x60a600,"53558b6c24108bd9568bb3f801000033","native timed on-screen network message (text, priority, seconds)"},
		{0x4654b0,"568bf1e8682a06008b16518bcc89118b","entity of a handle"},
		{0x5382b0,"8b442404894130c20400","participant record's controller slot at +0x30"},
		{0x486d2e,"8bcfff52288bcb508b44241850ff9588010000","zone load: the seat binding per party slot (game vt+0x188)"},
		{0x686fa4,"f0b14600","game vt+0x188 = the seat binding 0x46b1f0"},
		{0x422609,"8b1f8bceff50288b4c243850518bcfff9388010000","zone start: the controller bindings refreshed (0x422520: the seat binding again, then every hero re-registered)"},
		{0x425bc0,"568b3185f67507b8681968005ec3e86d","a name handle's text, \"\" for 0"},
		{0x48d260,"568b74240885f657","hero-seat manager vt+0x10: the controller is in the session"},
		{0x614210,"8b4118c3","a participant record's index at +0x18"},
		{0x559313,"813cc580fb790084030000","slow-player guard: drop at 900 slow frames (count at 0x79fb80 + 8 * index)"},
		{0x559341,"b884fb79008958fc891883c0083da4fb79007cf1","slow-player guard: its counters (0x79fb80..0x79fba4) reset only after a drop"},
		{0x612d92,"83be78020000027e0f8bcbe8fe140000","lost participant (0x612c40): with two participants or fewer the session ends; more: only that record goes"},
		{0x5bae2e,"8b876c19000083780c027d48","host menu Start Game: refused with fewer than two players in the room list (dialog 0xffc)"},
		{0x46dd0a,"b960997200","the game object is the static 0x729960 (party +0x14, seat binding +0x514/+0x524)"},
		{0x4b2fc0,"8a0db4727600b80100000084c8752f8b","timed-event manager getter (static 0x75d340)"},
		{0x68de14,"a02f4b00d02a4b00202b4b00","timed-event manager vtable: dtor, reset 0x4b2ad0, add 0x4b2b20 (vt+0xc, the pump, is checked at install: retail 0x4b2d60 or the xml1_turrets dispatch hook)"},
		{0x4b2d70,"83ec08578bf98b876c9f000085c0744e538d9f407c000056eb06","timed-event pump: heap count body+0x9f6c, heap body+0x7c40"},
		{0x4b2c10,"53568bf18b8ea47b00008d86086a0000578b3c888b","timed-event record alloc: ring read body+0x7ba4, ring body+0x6a08"},
		{0x4b2b80,"8b442408568b742408578bf98d0c76508d14cf52","timed-event record store: 24-byte records at body+0"},
		{0x4b2c90,"8b442404568bf18b8e2c2300006a08508d14ce52e827e9f4ff8b","timed-event heap push: 8-byte (time, index) entries, count heap+0x232c"},
		{0x4b2a10,"5633f65789b1a07b000089b1a47b000089b1a87b0000","timed-event pool init: ring write/read/count at body+0x7ba0/4/8"},
		{0x41a76b,"8b4c24248b571c8bf08b4424285051518bc48d4c2414896424348910e804f9ffff8b16508bceff5208","timed-event schedule: record {owner handle +0x1c, event*, payload} added through manager vt+8"},
		{0x41e440,"8b442408d900d94104dae9dfe0f6c4447a12c74104000000008b4c24046a00e8dc600100","actor update timed event: fires 0x434540 when the queued time equals the stored one"},
		{0x4b2de0,"568bf18b962c2300004a8bc285c089962c230000","timed-event heap pop: last entry to the root, sift-down by time"},
	}};
	inline constexpr std::array<guard, 5> audit_probe_guards{{
		{0x466dd8,"8b106a018bc8ff525c8b068bce47ff10", "AM probe: visibility activation call and displaced load"},
		{0x466ebe,"8b10538bc8ff525c8b068bce47ff103b", "AM probe: visibility deactivation call and displaced load"},
		{0x466f6a,"8b401c518bcc8901e8798fffff", "AM probe: visibility removal token and call"},
		{0x48a522,"f6867c02000008740e8b066a006a008bceff90d4000000", "AM probe: actonvisible predicate and spawn dispatch"},
		{0x48b041,"f6857c020000107432e891a7fbff8b1033db53538d4d20518bc8ff927801000084c07419", "AM probe: shy predicate, projection call and actual result branch"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image, bool audit_probe = false)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base, g.hex)) return &g;
		if (audit_probe) for (const auto& g : audit_probe_guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base, g.hex)) return &g;
		return nullptr;
	}
	inline bool receiver_composed(const std::uint8_t* entry, std::uint32_t owned_target)
	{
		if (!owned_target || entry[0] != 0xe9) return false;
		std::uint32_t relative = 0;
		std::memcpy(&relative, entry + 1, 4);
		return campaign_receive + 5 + relative == owned_target;
	}
	constexpr std::uint16_t agreement_revision = 7;

	// AR5 optional call-site contracts. These guards participate only with the corresponding switch.
	constexpr std::uint32_t execution_raw = 0x401e81;
	inline constexpr std::array<unsigned char, 8> execution_raw_original{0xff,0x52,0x14,0xc6,0x44,0x24,0x08,0x01};
	constexpr std::uint32_t execution_input = 0x40219e;
	inline constexpr std::array<unsigned char, 5> execution_input_original{0xe8,0x0d,0x81,0x15,0x00};
	constexpr std::uint32_t execution_stall = 0x4021b7;
	inline constexpr std::array<unsigned char, 5> execution_stall_original{0xe8,0x64,0x07,0x00,0x00};
	constexpr std::uint32_t execution_render = 0x402a09;
	inline constexpr std::array<unsigned char, 5> execution_render_original{0xe8,0xf2,0x88,0x1e,0x00};
	constexpr std::uint32_t execution_world = 0x40235a;
	inline constexpr std::array<unsigned char, 6> execution_world_original{0xff,0x92,0x6c,0x01,0x00,0x00};
	constexpr std::uint32_t execution_query = 0x5dae69;
	inline constexpr std::array<unsigned char, 6> execution_query_original{0xff,0x92,0x34,0x01,0x00,0x00};
	constexpr std::uint32_t execution_gate_fail = 0x606cc1;
	inline constexpr std::array<unsigned char, 5> execution_gate_fail_original{0xe8,0xfa,0x1a,0xf5,0xff};
	constexpr std::uint32_t execution_produce = 0x55a31a;
	inline constexpr std::array<unsigned char, 5> execution_produce_original{0xe8,0xe1,0xf3,0xff,0xff};
	constexpr std::uint32_t execution_apply = 0x558da9;
	inline constexpr std::array<unsigned char, 10> execution_apply_original{0xff,0x52,0x24,0x8b,0x14,0xad,0x78,0xad,0xa6,0x00};
	constexpr std::uint32_t execution_retire = 0x5594ce;
	inline constexpr std::array<unsigned char, 8> execution_retire_original{0x88,0x18,0x8b,0x8d,0x70,0x31,0x00,0x00};
	constexpr std::uint32_t execution_visual = 0x5600e0;
	inline constexpr std::array<unsigned char, 6> execution_visual_original{0x8b,0x0d,0xb0,0xd8,0x7a,0x00};
	inline constexpr guard execution_wrapper_guards[]{
		{0x5d8920,"a118ff8a0085c0750ae882ffffffa118ff8a00c3","AR5: menu getter backing pointer (eligibility never calls it)"},
		{0x5eb300,"a1ec138b0085c0750ae882ffffffa1ec138b00c3","AR5: video getter backing pointer (observer never calls it)"},
		{0x6a33a4,"309e5e00","AR5: video active predicate virtual slot"},
		{0x5e9e30,"8b813c40000085c07c1883f8037d1369c0601500008d4408188a805d1500002401c38d41188a805d1500002401c3","AR5: complete read-only active-video predicate"},
		{0x55a3d9,"8b0c24b8b0fb790064890d0000000083c40cc3","AR5: input manager getter returns the observed singleton"},
		{0x606d40,"b8d8bba300c3","AR5: native gate getter returns the observed singleton"},
		{0x610d20,"8a8120040000c3","AR5: native online-state byte"},
		{0x4021a3,"6a03e8564b20008bc8e8cf46200084c0740c8bcee864070000e91a070000806614fe","AR5: native wait test, stalled call, common-cleanup jump"},
		{0x402920,"83ec105356578bf9","AR5: native helper stack frame"},
		{0x4029e9,"e8228820008bc8e8db7b200085c07510e8024320008a480684c90f8447010000e8f2881e00","AR5: message/mode render choice after retained services"},
		{0x402b50,"5f5e5b83c410c3","AR5: native no-render epilogue"},
	};
	inline constexpr guard execution_observer_guards[]{
		{0x551f19,"8b0c24b8c0eb790064890d0000000083c40cc3","AR5: controller getter returns the observed singleton"},
		{0x5cec00,"568bf18a8644190000","AR5: PDA-open entry, observation only"},
		{0x60c230,"53568bf18a466c","AR5: native pause toggle, observation only"},
		{0x402a19,"e8e2881e00","AR5: video render branch"},
		{0x402a7c,"e89f5e1d00","AR5: ordinary render branch"},
		{0x401e81,"ff5214c644240801","AR5: execution_raw"},
		{0x40219e,"e80d811500","AR5: execution_input"},
		{0x40235a,"ff926c010000","AR5: execution_world"},
		{0x5dae69,"ff9234010000","AR5: execution_query"},
		{0x606cc1,"e8fa1af5ff","AR5: execution_gate_fail"},
		{0x55a31a,"e8e1f3ffff","AR5: execution_produce"},
		{0x558da9,"ff52248b14ad78ada600","AR5: execution_apply"},
		{0x5594ce,"88188b8d70310000","AR5: execution_retire"},
		{0x5600e0,"8b0db0d87a00","AR5: execution_visual"},
	};
	inline constexpr std::array<unsigned char,5> execution_video_branch_original{0xe8,0xe2,0x88,0x1e,0x00};
	inline constexpr std::array<unsigned char,5> execution_ordinary_branch_original{0xe8,0x9f,0x5e,0x1d,0x00};
	inline constexpr std::array<unsigned char,9> execution_pda_original{0x56,0x8b,0xf1,0x8a,0x86,0x44,0x19,0,0};
	inline constexpr std::array<unsigned char,7> execution_pause_original{0x53,0x56,0x8b,0xf1,0x8a,0x46,0x6c};
	// AR6 Layer 1 (fable-ar6): the sender 0x559700's full-vs-delta decision (0x5597d2: cmp eax,3; jge 0x5598a0 - 9 bytes;
	// eax = the pending count clamped >= 0, ebx = the local block base set at 0x55972c). The bridge forces the full path
	// (0x5597db) while [ebx+0x317c] (the next frame) < whole_input_frames, else keeps the stock decision (pending < 3 full,
	// else delta at 0x5598a0). Layer 2: the store 0x559560's resolve-against-+0x3148 (0x559660: lea eax,[ebx+0x3148]; je
	// 0x559675 - 8 bytes); when +0x3148 has no resolved record (cl == 0), the bridge points the "previous" at a zero record
	// so 0x558c00 fills the absent fields with zero instead of the store skipping and leaving the decoder's stack garbage.
	constexpr std::uint32_t whole_input_site = 0x5597d2, whole_input_full = 0x5597db, whole_input_delta = 0x5598a0;
	inline constexpr std::array<unsigned char, 9> whole_input_original{0x83,0xf8,0x03,0x0f,0x8d,0xc5,0x00,0x00,0x00};
	constexpr std::uint32_t delta_zero_site = 0x559660, delta_zero_resume = 0x559668;
	inline constexpr std::array<unsigned char, 8> delta_zero_original{0x8d,0x83,0x48,0x31,0x00,0x00,0x74,0x0d};
	inline constexpr guard whole_input_guards[]{
		{0x5597d2,"83f8030f8dc5000000","AR6 L1: sender full-vs-delta decision"},
		{0x5597db,"8b45048d","AR6 L1: sender full-record path target"},
		{0x5598a0,"8b442418","AR6 L1: sender delta path target"},
		{0x559660,"8d8348310000740d","AR6 L2: store resolve-against-+0x3148 skip"},
		{0x559668,"57508bcae8","AR6 L2: store resolve call (push record, push prev, call 0x558c00)"},
	};
	inline const guard* whole_input_mismatch(const std::uint8_t* image) {
		for (const auto& g : whole_input_guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		return nullptr;
	}
	// AR6 (fable-ar6): the pause-code guard's site = the controller apply vt+0x24 (0x550fb0: push ebx; mov ebx,ecx; mov eax,
	// [ebx+2f8h] - 9 bytes re-executed by the bridge); the guard covers the whole prologue up to the one read of the mask
	// argument (0x550fc8 mov edx,[ecx]) and its write to cur (0x550fcb), the apply loop's call (0x558da9, the record's mask at
	// esi+4 pushed as the argument) and the ring layout the pointer test relies on (0x558d0b..0x558d0e, slot = +0x26c + read*0x28).
	constexpr std::uint32_t pause_guard_site = 0x550fb0, pause_guard_resume_at = 0x550fb9;
	inline constexpr std::array<unsigned char, 9> pause_guard_original{0x53,0x8b,0xd9,0x8b,0x83,0xf8,0x02,0x00,0x00};
	inline constexpr guard pause_guard_guards[]{
		{0x550fb0,"538bd98b83f80200008b4c240c568b7424148983740300008b11578993f8020000","AR6: controller apply prologue, mask read and cur write"},
		{0x558d0b,"8d0cb68db4c86c020000","AR6: apply loop's slot address (+0x26c + read*0x28)"},
		{0x558da1,"538d4e045155","AR6: apply loop pushes floats, record+4 (the code mask), index"},
	};
	inline const guard* pause_guard_mismatch(const std::uint8_t* image) {
		for (const auto& g : pause_guard_guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		return nullptr;
	}
	inline const guard* execution_mismatch(const std::uint8_t* image, bool observer, bool wrapper) {
		if (observer || wrapper) for (const auto& g : execution_wrapper_guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		if (observer) for (const auto& g : execution_observer_guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		return nullptr;
	}
}
