// AM: bounded read-only diagnostics, installed only with [Dev] LateJoinProbe.
// Slot/handle widths always come from the installed world; never the retail 0xff mask or pool offsets.
std::array<std::uint8_t, 1u << 16> audit_entities{};
std::uint32_t audit_counts[6]{}, audit_vis_hash = 2166136261u;
unsigned audit_samples = 0, audit_epoch = 0;
bool audit_collecting = false;
unsigned audit_index_bits()
{
	const auto bits = entity_capacity::installed().tokens.index_bits;
	return bits >= 1 && bits <= 16 ? bits : 8;
}
void audit_hash_word(std::uint32_t& h, std::uint32_t v)
{
	for (unsigned i = 0; i < 4; ++i) { h ^= (v >> (i * 8)) & 255; h *= 16777619u; }
}
void audit_zone_load()
{
	if (!probe_enabled) return;
	audit_collecting = false; // every new zone closes the preceding zone's partial trace
	if (!late_restart || !restart.armed) return;
	audit_entities.fill(0); for (auto& n : audit_counts) n = 0;
	audit_vis_hash = 2166136261u; audit_samples = 0; audit_epoch = checksum_window.epoch; audit_collecting = true;
}
// Saved pushad frame: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX,EFLAGS,bridge-return,kind,... .
// Visibility records are dispatches at the exact retail call sites, not a guess from a later entity diff.
void __cdecl audit_note_saved(const std::uint32_t* saved)
{
	if (!probe_enabled || !audit_collecting || !probe_epoch_late) return;
	__try
	{
		unsigned kind = saved[10];
		std::uint32_t token = 0, context = 0;
		if (kind == 2) token = saved[12]; // cdecl remove wrapper: outer return, token by value
		else
		{
			const void* entity = reinterpret_cast<const void*>(kind == 4 ? saved[2] : saved[6]);
			token = at<std::uint32_t>(entity, 0x1c);
			context = kind >= 3 ? at<std::uint32_t>(entity, 0x27c) : at<std::uint32_t>(entity, entity_flags_at);
			if (kind == 4 && !(saved[7] & 255)) kind = 5; // shy projection false: permit; true: inhibit
		}
		if (kind >= 6) return;
		const auto index = token & ((1u << audit_index_bits()) - 1);
		++audit_counts[kind]; audit_entities[index] |= std::uint8_t(1u << kind);
		audit_hash_word(audit_vis_hash, kind); audit_hash_word(audit_vis_hash, index); audit_hash_word(audit_vis_hash, context);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:audit-note-fault", "jip audit: visibility/spawner observation faulted"); }
}
// Preserve all integer/flags/x87/SSE state around the observer. No allocation, logging or native service on the normal path.
__declspec(naked) void audit_observe_bridge()
{
	__asm {
		pushfd
		pushad
		mov ebp, esp
		sub esp, 210h
		and esp, 0fffffff0h
		fxsave [esp]
		push ebp
		call audit_note_saved
		add esp, 4
		fxrstor [esp]
		mov esp, ebp
		popad
		popfd
		ret 4
	}
}
std::uint32_t audit_activate_resume_at = 0x466de3, audit_deactivate_resume_at = 0x466ec8,
	audit_remove_at = 0x45fef0, audit_shy_resume_at = 0x48b061;
// JMP replaces the three-byte call and following two-byte load; the original callee still gets its original argument.
__declspec(naked) void audit_activate_bridge() { __asm {
	push 0
	call audit_observe_bridge
	call dword ptr [edx+5ch]
	mov eax, [esi]
	jmp dword ptr [audit_activate_resume_at]
} }
__declspec(naked) void audit_deactivate_bridge() { __asm {
	push 1
	call audit_observe_bridge
	call dword ptr [edx+5ch]
	mov eax, [esi]
	jmp dword ptr [audit_deactivate_resume_at]
} }
// CALL replacements tail-dispatch with the site's return/arguments intact (six-byte indirect call has a NOP tail).
__declspec(naked) void audit_remove_bridge() { __asm {
	push 2
	call audit_observe_bridge
	jmp dword ptr [audit_remove_at]
} }
__declspec(naked) void audit_actonvisible_bridge() { __asm {
	push 3
	call audit_observe_bridge
	jmp dword ptr [eax+0d4h]
} }
// JMP around the original projection call: EBP is the spawner and AL is its actual camera-test result.
__declspec(naked) void audit_shy_bridge() { __asm {
	call dword ptr [edx+178h]
	push 4
	call audit_observe_bridge
	jmp dword ptr [audit_shy_resume_at]
} }
void probe_audit_frame(unsigned frame, bool epoch_sample)
{
	if (!probe_enabled || !probe_epoch_late || !audit_collecting) return;
	if (!epoch_sample && !(audit_samples & 1u)) return; // frames 1/30 belong to the consumed epoch
	const unsigned sample = epoch_sample ? 1u : frame == 1 ? 2u : frame == 30 ? 4u : 0u;
	if (!sample || (audit_samples & sample)) return;
	__try
	{
		alignas(16) std::uint32_t party[16]{};
		void* game = fn<getter_t>(game_getter)();
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
		const auto bits = audit_index_bits(), capacity = 1u << bits;
		void* world = fn<getter_t>(world_getter_at)(); const auto vt = at<void*>(world, 0);
		const auto entity_at = fn<void*(__thiscall*)(void*, std::uint32_t)>(at<std::uint32_t>(vt, world_entity_by_index_slot));
		const auto record_at = fn<const std::uint32_t*(__thiscall*)(void*, std::uint32_t)>(at<std::uint32_t>(vt, world_flag_record_slot));
		const auto* active = fn<const std::uint32_t*(__thiscall*)(void*, std::uint32_t)>(at<std::uint32_t>(vt, world_flag_members_slot))(world, entity_active_flag);
		unsigned entities = 0, registered = 0, differences = 0, affected = 0; std::uint32_t hash = 2166136261u;
		char diff[300]{}, changed[120]{}; int dn = 0, cn = 0;
		for (unsigned i = 0; i < capacity; ++i)
		{
			if (audit_entities[i]) { if (affected < 8) cn += std::snprintf(changed + cn, sizeof changed - cn, "%s%x:%02x", cn ? "," : "", i, unsigned(audit_entities[i])); ++affected; }
			const auto* entity = static_cast<const std::uint8_t*>(entity_at(world, i)); if (!entity) continue;
			bool hero = false; for (unsigned k = 0; k < 4 && k < party[party_list_count_at / 4]; ++k) hero |= party[k] && party[k] == at<std::uint32_t>(entity, 0x1c);
			if (hero) continue;
			++entities; const bool reg = (at<std::uint8_t>(entity, entity_registered_at) & entity_registered_bit) != 0;
			// No vtable/pointer value enters the hash: a fix-owned or heap vtable may have a per-process address.
			audit_hash_word(hash, i); audit_hash_word(hash, reg);
			for (unsigned off = 0; off < entity_flag_bytes; off += 4) audit_hash_word(hash, at<std::uint32_t>(entity, entity_flags_at + off));
			if (!reg) continue; // no indexed entry exists for an unregistered object
			++registered; const auto* cached = record_at(world, i); const auto bitmap = (active[i >> 5] >> (i & 31)) & 1u;
			for (unsigned k = 0; k < entity_flag_bytes / 4; ++k) audit_hash_word(hash, cached[k]); audit_hash_word(hash, bitmap);
			if (std::memcmp(entity + entity_flags_at, cached, entity_flag_bytes) || ((at<std::uint32_t>(entity, entity_flags_at) >> entity_active_flag) & 1u) != bitmap)
			{
				if (differences < 8) dn += std::snprintf(diff + dn, sizeof diff - dn, "%s%x:%08x/%08x/%u", dn ? "," : "", i, at<std::uint32_t>(entity, entity_flags_at), cached[0], bitmap);
				++differences;
			}
		}
		const auto td = xml1_turrets::probe_digest(bits);
		logger::write("jip audit: epoch=%u frame=%s entities=%u/%u flags=%08x diff=%u[%s] vis=%u/%u/%u/%u/%u/%u hash=%08x ids=%u[%s] turrets=%u/%u/%u:%08x",
			audit_epoch, epoch_sample ? "epoch" : frame == 1 ? "1" : "30", entities, registered, hash, differences, dn ? diff : "none",
			audit_counts[0], audit_counts[1], audit_counts[2], audit_counts[3], audit_counts[4], audit_counts[5], audit_vis_hash, affected, cn ? changed : "none", td.placements, td.live, td.flights, td.hash);
		audit_samples |= sample; if (frame == 30 && !epoch_sample) audit_collecting = false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:audit-frame-fault", "jip audit: manifest read fault"); }
}
