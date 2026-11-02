// p060fix/AR: a newcomer lost between its admission and the shared start, for 2-4 peers (included by
// join_in_progress_test.cpp inside its anonymous namespace). A message-level model of the cohort: every peer runs the
// PRODUCTION decision functions (gns_transport_core: newcomer_lost wire, newcomer_ledger, classify_newcomer_loss;
// join_in_progress_core: admission_losses, restart_removal_held, owed_restart_due, machine stages). Each host link is
// one ordered reliable channel (GNS reliable delivery), so the host's decision and the restart's 0x26 arrive in the
// order sent. The checks: every surviving peer begins its restart with the SAME participants, nobody removes anyone
// inside its restart, every hold is released, and a second newcomer still reaches the shared start.
namespace lost_newcomer_model
{
	enum class point { before_image, transfer, hold_capped, after_ready, after_start };
	const char* point_name(point p)
	{
		switch (p)
		{
		case point::before_image: return "before the image";
		case point::transfer: return "during the transfer";
		case point::hold_capped: return "during the hold (after its cap)";
		case point::after_ready: return "after ready (image in, before the 0x26)";
		case point::after_start: return "after the restart's 0x26 (before frame 0)";
		}
		return "?";
	}
	struct wire_message { enum kind_t { decision, image, start } kind; std::vector<std::uint8_t> bytes; };
	struct peer
	{
		int index = 0;
		bool host = false, newcomer = false, alive = true, ended = false;
		std::uint8_t session = 0;             // participant bit set (bit i = session index i)
		gns_transport_core::newcomer_ledger ledger;
		cohort epoch;
		bool held = false, has_image = false, entered = false, removed_inside_restart = false;
		std::uint8_t at_entry = 0;            // participants at this peer's restart entry
		std::uint8_t at_frame0 = 0;           // participants when its frame-0 gate passed
		std::deque<wire_message> inbox;       // the host link's ordered reliable channel
		bool lost_seen = false;               // its own transport saw the lost newcomer's link end
		unsigned own_removals = 0;            // removals by its own cut-frame path
	};
	struct world
	{
		std::vector<peer> peers;
		machine m;                            // the host's seats
		admission_losses admission;           // the host's admission
		bool sender = false;                  // the host's native sender runs (image captured)
		bool announced = false;               // the host broadcast the 0x26
		std::uint8_t jays = 0;                // participants whose image completion (J) the sender has
		std::uint32_t now = 1000;
		unsigned decisions_sent = 0, owed_restarts = 0;
		peer& host() { return peers[0]; }
		peer* by_index(int i) { for (auto& p : peers) if (p.index == i) return &p; return nullptr; }
	};
	std::uint8_t bit(int i) { return std::uint8_t(1u << i); }
	// Mutations for the sensitivity check (each must make the model fail somewhere): a guest that ignores the host's
	// decision; a guest whose own timeout ignores the ledger and the restart window (the 060zj behaviour).
	bool mut_no_carry = false, mut_own_timeout = false;
	// The host's admission at the area boundary: boundary mask to every old peer, run_add for every newcomer.
	world admit(int existing, int newcomers)
	{
		world w;
		const int total = 1 + existing + newcomers;
		for (int i = 0; i < total; ++i) { peer p; p.index = i; p.host = i == 0; p.newcomer = i > existing; w.peers.push_back(p); }
		std::uint8_t all = 0, mask = 0;
		for (int i = 0; i < total; ++i) all |= bit(i);
		for (int i = existing + 1; i < total; ++i) mask |= bit(i);
		w.m.started = true;
		for (int i = existing + 1; i < total; ++i) w.m.seats[std::size_t(i - existing - 1)] = seat{reinterpret_cast<const void*>(std::uintptr_t(0x1000 + i)), i, stage::adding, w.now};
		w.admission.announced = mask;
		for (auto& p : w.peers)
		{
			p.session = all;
			p.epoch.begin();
			if (!p.newcomer) p.held = true;            // host + every old peer hold from the admission (AQ3)
			if (!p.host && !p.newcomer) p.ledger.admit(mask);
		}
		return w;
	}
	void send_all(world& w, wire_message::kind_t k, const std::vector<std::uint8_t>& bytes, int except)
	{
		for (auto& p : w.peers) if (!p.host && p.alive && p.index != except && (w.host().session & bit(p.index))) p.inbox.push_back({k, bytes});
	}
	bool restart_begun_host(world& w) { return restart_removal_held(w.host().epoch.active, w.host().epoch.announced, w.host().epoch.requested, w.host().epoch.started); }
	// The host notices the newcomer's loss (its link ends, or the native removal / a give-up drop runs).
	void host_loss(world& w, int lost)
	{
		auto& h = w.host();
		const bool newcomer = w.admission.newcomer(w.m, lost);
		if (gns_transport_core::classify_newcomer_loss(true, newcomer, restart_begun_host(w)) == gns_transport_core::newcomer_loss::before_restart)
		{
			const auto remaining = std::uint8_t(w.admission.remaining(w.m, lost));
			send_all(w, wire_message::decision, gns_transport_core::newcomer_lost(std::uint8_t(lost), remaining), lost);
			++w.decisions_sent;
			w.admission.decided(lost, w.now);
			h.session &= std::uint8_t(~bit(lost)); // removed at once (hand_over)
			for (auto& s : w.m.seats) if (s.index == lost) s = seat{};   // "left while ..."
			w.jays &= std::uint8_t(~bit(lost));
		}
		// else: the ordinary in-epoch path (agreed cut frame after the restored zone runs)
	}
	// The host's sender completes once every listed remote participant has its J: the 0x26 to every peer, in order.
	void host_sender(world& w)
	{
		auto& h = w.host();
		if (!w.sender || w.announced) return;
		for (int i = 1; i < 4; ++i) if ((h.session & bit(i)) && !(w.jays & bit(i))) return;
		w.announced = true;
		h.epoch.announce();
		send_all(w, wire_message::start, {0x26}, -1);
	}
	void host_capture(world& w)
	{
		w.sender = true;
		send_all(w, wire_message::image, {0x47}, -1);
		for (auto& s : w.m.seats) if (s.at == stage::adding) { s.at = stage::streaming; s.since = w.now; }
	}
	// The owed restart (every newcomer lost before the image): production owed_restart_due, then a capture.
	void host_owed(world& w)
	{
		bool left = false, others = false;
		for (const auto& s : w.m.seats) left |= s.at == stage::adding || s.at == stage::streaming || s.at == stage::starting;
		for (int i = 1; i < 4; ++i) if ((w.host().session & bit(i)) && !(w.admission.lost & bit(i))) others = true;
		if (owed_restart_due(w.admission.lost, restart_begun_host(w), w.sender, left, others, false, false, w.now - w.admission.lost_at, w.admission.owed_attempts))
		{
			++w.admission.owed_attempts;
			++w.owed_restarts;
			host_capture(w);
		}
	}
	// A non-host peer: its next transport tick (decisions carried out while its zone runs and it is not inside its restart).
	void carry(world&, peer& p, bool last_chance)
	{
		if (!p.alive || p.ended || mut_no_carry) return;
		if (!last_chance && p.epoch.active && p.epoch.started) return;
		for (int i = 1; i < 4; ++i)
			if (p.ledger.must_remove(i)) { if (p.entered && p.epoch.active) p.removed_inside_restart = true; p.session &= std::uint8_t(~bit(i)); p.ledger.removed(i); }
	}
	// Its own cut-frame path for a loss it saw itself: never for an admission newcomer, never inside its restart.
	void own_timeout(world&, peer& p, int lost)
	{
		if (!p.alive || p.ended || p.host || !(p.session & bit(lost)) || !p.lost_seen) return;
		if (!mut_own_timeout && !p.ledger.own_removal_allowed(lost)) return;
		if (!mut_own_timeout && removal_window(p.epoch.active, p.epoch.announced, p.epoch.requested, p.epoch.started, p.epoch.saw_loading)) return;
		if (p.entered && p.epoch.active) p.removed_inside_restart = true;
		p.session &= std::uint8_t(~bit(lost));
		++p.own_removals;
	}
	// The restart entry (deferred_campaign_start): the last chance for the host's decisions, then the restart.
	void restart_entry(world& w, peer& p)
	{
		if (p.host) { if (!p.epoch.requested) p.epoch.request(); }
		carry(w, p, true);
		p.ledger.restart_entry();
		p.entered = true;
		p.at_entry = p.session;
		p.held = false;       // hold_end(restart): every role, first thing
		p.epoch.start();
	}
	// One guest step: process its next inbox message, or (tick) carry decisions; the order is the scenario's choice.
	bool guest_message(world& w, peer& p)
	{
		if (p.inbox.empty() || !p.alive || p.ended) return false;
		const auto msg = p.inbox.front();
		p.inbox.pop_front();
		if (msg.kind == wire_message::decision)
		{
			const auto parsed = gns_transport_core::parse(msg.bytes.data(), msg.bytes.size());
			if (!parsed.valid || parsed.k != gns_transport_core::kind::newcomer_lost) { p.ended = true; return false; } // a bad wire ends the run (checked)
			if (gns_transport_core::newcomer_lost_authorized(true, true, 0)) p.ledger.decide(int(parsed.index));
		}
		else if (msg.kind == wire_message::image) { p.has_image = true; w.jays |= bit(p.index); if (p.newcomer) for (auto& s : w.m.seats) if (s.index == p.index) s.at = stage::starting; }
		else { p.epoch.request(); restart_entry(w, p); }
		return true;
	}
	// Frame 0 of the restarted epoch, MODELLED (not the production cut-frame code): every peer's gate needs every
	// listed participant's frame-0 input, so a participant lost after the 0x26 is removed by the existing agreed cut
	// frame at frame 0 on every peer once its restored zone runs. What the model checks here is that nobody removed it
	// earlier, inside its restart, and that every peer entered its restart with the same list.
	void frame0(world& w, int lost)
	{
		for (auto& p : w.peers)
		{
			if (!p.alive || p.ended || !p.entered) continue;
			p.epoch = cohort{};                    // the restored zone runs: the cohort epoch ends ("cohort synchronized")
			if (lost > 0 && (p.session & bit(lost))) p.session &= std::uint8_t(~bit(lost)); // agreed at its frame 0 on every peer
			p.at_frame0 = p.session;
		}
	}
	struct outcome { bool same_entry = true, same_frame0 = true, all_entered = true, holds_released = true, none_inside = true, second_in = true, old_peers_waited = true; std::uint8_t entry = 0; };
	outcome run(int existing, int newcomers, point at, unsigned order, bool host_dies = false, bool both = false)
	{
		world w = admit(existing, newcomers);
		const int lost = existing + 1;            // the first newcomer is the one lost
		const bool after_image = at != point::before_image;
		if (after_image) host_capture(w);
		// existing peers and other newcomers receive the image before the loss in the transfer/hold/ready cases
		auto deliver_images = [&]() { for (auto& p : w.peers) if (!p.host) while (!p.inbox.empty() && p.inbox.front().kind == wire_message::image) guest_message(w, p); };
		if (at == point::after_ready || at == point::after_start || (order & 1)) deliver_images();
		if (at == point::hold_capped) for (auto& p : w.peers) p.held = false; // the cap released every hold (simulates again until the restart)
		if (at == point::after_ready && newcomers == 1) { /* the lost one had its J: the sender completes now */ }
		if (at == point::after_start) { host_sender(w); for (auto& p : w.peers) if (!p.host) while (guest_message(w, p)) {} restart_entry(w, w.host()); }
		// the loss
		w.peers[std::size_t(lost)].alive = false;
		if (both) w.peers[std::size_t(lost + 1)].alive = false; // both newcomers lost at once
		w.now += 300;
		if (host_dies) w.host().alive = false;
		for (auto& p : w.peers) if (!p.host) p.lost_seen = (order >> 1) & 1;   // a guest may see the link end before the host's decision
		for (auto& p : w.peers) { own_timeout(w, p, lost); if (both) own_timeout(w, p, lost + 1); } // its own 8.5 s timeout may fire early
		if (!host_dies)
		{
			host_loss(w, lost);
			if (both) host_loss(w, lost + 1);
			if (!after_image)
			{
				bool adding = false;
				for (const auto& s : w.m.seats) adding |= s.at == stage::adding;
				w.now += 3000;
				if (adding) host_capture(w); else host_owed(w); // a remaining newcomer's own image, or the owed restart
			}
			for (int round = 0; round < 8; ++round)
			{
				host_sender(w);
				for (auto& p : w.peers)
				{
					if (p.host || !p.alive) continue;
					if ((order >> 2) & 1) carry(w, p, false);             // a transport tick before its next message
					own_timeout(w, p, lost);
					if (both) own_timeout(w, p, lost + 1);
					guest_message(w, p);
				}
			}
			if (w.announced && !w.host().entered) restart_entry(w, w.host());
			frame0(w, lost);
			if (both) frame0(w, lost + 1);
		}
		else
		{
			// the host is lost too: no decision and no 0x26 ever arrive; every guest's session ends (host-left, the existing
			// native path - modelled). Checked: no old peer removed the newcomer by its own timeout meanwhile.
			for (auto& p : w.peers) if (!p.host && p.alive) { own_timeout(w, p, lost); p.ended = true; p.held = false; }
		}
		outcome o;
		bool first = true;
		for (auto& p : w.peers)
		{
			if (!p.alive) continue;
			if (p.ended) { o.all_entered = false; if (p.held) o.holds_released = false; if (!p.newcomer && p.own_removals) o.old_peers_waited = false; continue; }
			if (!p.entered) o.all_entered = false;
			if (p.held) o.holds_released = false;
			if (first) { o.entry = p.at_entry; first = false; }
			if (p.at_entry != o.entry) o.same_entry = false;
			if (p.at_frame0 != w.host().at_frame0) o.same_frame0 = false;
			if (p.removed_inside_restart) o.none_inside = false;
		}
		if (newcomers == 2 && !host_dies) { const auto* second = w.by_index(lost + 1); o.second_in = second && second->entered && (second->at_frame0 & bit(lost + 1)) && (w.host().at_frame0 & bit(lost + 1)); }
		return o;
	}
	void run_all()
	{
		const point points[] = {point::before_image, point::transfer, point::hold_capped, point::after_ready, point::after_start};
		unsigned scenarios = 0, orders = 0;
		for (int existing = 1; existing <= 2; ++existing)
			for (int newcomers = 1; newcomers <= 2; ++newcomers)
			{
				if (1 + existing + newcomers > 4) continue;   // four participant slots
				for (const auto at : points)
				{
					bool ok = true;
					for (unsigned order = 0; order < 8; ++order)
					{
						const auto o = run(existing, newcomers, at, order);
						ok = ok && o.same_entry && o.same_frame0 && o.all_entered && o.holds_released && o.none_inside && o.second_in;
						// before the 0x26 the lost one is gone from every peer AT the restart entry; after it, at frame 0
						if (at != point::after_start) ok = ok && !(o.entry & bit(existing + 1));
						++orders;
					}
					char what[256];
					std::snprintf(what, sizeof what, "AR model: %d existing guest(s) + %d newcomer(s), one lost %s - every peer enters its restart with the same participants (the lost one %s), nobody removes inside a restart, every hold released%s (8 delivery orders)",
						existing, newcomers, point_name(at), at == point::after_start ? "still listed: the modelled agreed frame 0 removes it" : "removed before every restart entry", newcomers == 2 ? ", the second newcomer enters and is listed at frame 0" : "");
					check(ok, what);
					++scenarios;
				}
			}
		// the host lost at the same time: nobody waits on a decision that never comes; every guest's session ends, no hold stays
		for (int existing = 1; existing <= 2; ++existing)
			for (int newcomers = 1; newcomers + existing <= 3; ++newcomers)
			{
				bool ok = true;
				for (unsigned order = 0; order < 8; ++order) { const auto o = run(existing, newcomers, point::transfer, order, true); ok = ok && o.holds_released && o.old_peers_waited; }
				char what[200];
				std::snprintf(what, sizeof what, "AR model: host lost with the newcomer (%d existing, %d newcomer(s)) - no old peer removes it by its own timeout while it waits for a decision that never comes (the session end is the native host-left path)", existing, newcomers);
				check(ok, what);
			}
		// both newcomers of one admission lost at once (1 old guest + 2 newcomers): the restart still follows, without either
		for (const auto at : {point::before_image, point::transfer, point::after_ready})
		{
			bool ok = true;
			for (unsigned order = 0; order < 8; ++order)
			{
				const auto o = run(1, 2, at, order, false, true);
				ok = ok && o.same_entry && o.same_frame0 && o.all_entered && o.holds_released && o.none_inside && !(o.entry & 0x0c);
			}
			char what[220];
			std::snprintf(what, sizeof what, "AR model: both newcomers lost at once %s (1 old guest) - host and guest enter the restart with the same participants, neither newcomer listed, holds released (8 delivery orders)", point_name(at));
			check(ok, what);
		}
		char done[160];
		std::snprintf(done, sizeof done, "AR model: %u loss scenarios x 8 delivery orders = %u runs", scenarios, orders);
		check(scenarios == 15 && orders == 120, done);
	}
	// Sensitivity: each mutation must fail at least one of the 120 runs (otherwise the checks above prove nothing).
	unsigned failing_runs()
	{
		const point points[] = {point::before_image, point::transfer, point::hold_capped, point::after_ready, point::after_start};
		unsigned failing = 0;
		for (int existing = 1; existing <= 2; ++existing)
			for (int newcomers = 1; newcomers <= 2; ++newcomers)
			{
				if (1 + existing + newcomers > 4) continue;
				for (const auto at : points)
					for (unsigned order = 0; order < 8; ++order)
					{
						const auto o = run(existing, newcomers, at, order);
						bool ok = o.same_entry && o.same_frame0 && o.all_entered && o.holds_released && o.none_inside && o.second_in;
						if (at != point::after_start) ok = ok && !(o.entry & bit(existing + 1));
						failing += !ok;
					}
			}
		return failing;
	}
	void sensitivity()
	{
		mut_no_carry = true; const auto a = failing_runs(); mut_no_carry = false;
		mut_own_timeout = true; const auto b = failing_runs(); mut_own_timeout = false;
		const auto clean = failing_runs();
		char what[220];
		std::snprintf(what, sizeof what, "AR model sensitivity: a guest ignoring the host's decision fails %u of 120 runs, a guest timing out by itself inside its restart fails %u; the production rules fail %u", a, b, clean);
		check(a > 0 && b > 0 && clean == 0, what);
	}
	// The failing-first control: the 060zj behaviour (no decision; the guest's own timeout removes the lost newcomer
	// whenever it fires, the host only after its 20 s fallback, just before its restart) parts the peers' restart sets.
	void control_060zj()
	{
		world w = admit(1, 1);
		host_capture(w);
		auto& g = w.peers[1];
		while (!g.inbox.empty()) guest_message(w, g);
		w.peers[2].alive = false;
		// host: native fallback removes it, the sender completes, the 0x26 goes out; the guest gets the 0x26 first and
		// its own timeout fires only after resetgame (ZJ-7j 25.289), inside its restart
		w.host().session &= std::uint8_t(~bit(2)); w.jays &= std::uint8_t(~bit(2));
		host_sender(w);
		g.ledger = {};                           // 060zj had no admission ledger
		guest_message(w, g);                     // 0x26 -> restart entry with participant 2 still listed
		restart_entry(w, w.host());
		check(g.at_entry != w.host().at_entry, "AR control: the 060zj order (host's fallback before its restart, the guest's timeout inside its restart) gives different restart participants - the model sees the live failure");
	}
}
