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
	constexpr std::uint32_t gamespy_thunk = 0x6159c0;
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
		message_init = 0x615810, send_to = 0x610f30, peer_connection = 0x55d7e0, game_getter = 0x46dce0, pause_getter = 0x60c7e0,
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
	inline constexpr std::array<guard, 81> guards{{
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
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
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
	constexpr std::uint16_t agreement_revision = 6;
}
