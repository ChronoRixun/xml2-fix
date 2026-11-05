// AR5: protected execution belongs to a restart generation, never a frame-number window.
void restart_execution_units()
{
    restart_execution x;
    restart_wait w{true, true, false, false, 1, 1, 0};
    check(x.classify(w) == restart_wait_policy::native, "AR5: ordinary lobby/offline waits pass through");
    x.begin(true); const auto first_generation = x.generation;
    check(x.phase == restart_execution_phase::restoring && x.classify(w) == restart_wait_policy::native,
          "AR5: deferred late entry starts restoring; no skipped loading callbacks");
    x.image_returned(); x.observe_ready(true, false, false);
    check(x.phase == restart_execution_phase::world_ready && !x.canonical_seen && x.classify(w) == restart_wait_policy::input_only,
          "AR5: world-ready input wait protected before the first canonical gate");
    x.canonical();
    for (const unsigned phase : {1u, 2u}) for (const unsigned mode : {0u, 1u}) {
        w.phase = phase; w.mode = mode;
        check(x.classify(w) == restart_wait_policy::input_only, "AR5: input-only gate/resync in both native rendering modes");
    }
    w.flags = 5; check(x.classify(w) == restart_wait_policy::unclassified, "AR5: unknown gate bit 2 retains native service");
    w.flags = 1; w.phase = 4; check(x.classify(w) == restart_wait_policy::unclassified, "AR5: unknown phase retains native service");
    w.phase = 1; w.run_permission = 1; check(x.classify(w) == restart_wait_policy::unclassified, "AR5: contradictory run permission cannot skip");
    w.run_permission = 0; w.modal = true;
    check(x.classify(w) == restart_wait_policy::native, "AR5: PDA/Team and input-wait overlap retains native service");
    w.modal = false; w.loading = true;
    check(x.classify(w) == restart_wait_policy::native, "AR5: loading and input-wait overlap retains native service");
    w.loading = false; w.readable = false;
    check(x.classify(w) == restart_wait_policy::unclassified, "AR5: unreadable state never skips");
    w.readable = true; x.area_load();
    check(x.phase == restart_execution_phase::off, "AR5: unrelated area load ends a running protected generation");
    x.begin(true); x.area_load(); x.image_returned(); x.observe_ready(true, false, false); x.canonical();
    check(x.generation != first_generation && x.canonical_seen, "AR5: own initial load retains restoring generation; next admission replaces it");
    w.session = false; check(x.classify(w) == restart_wait_policy::native, "AR5: host-left/frontend always pass through");
    x.clear(); w.session = true;
    check(x.classify(w) == restart_wait_policy::native, "AR5: teardown/session rejoin cannot inherit protection");
    x.begin(false); x.canonical(); x.observe_ready(true, false, false);
    check(x.phase == restart_execution_phase::off, "AR5: numerical frame zero or lobby saved START never arms protection");

    join_in_progress_diagnostics::bounded_capture<unsigned,2> ring;
    *ring.append()=11; *ring.append()=22;
    check(!ring.append() && ring.omitted==1 && ring.entries[0]==11 && ring.entries[1]==22,
          "AR5: bounded observer retains first divergence and explicitly counts omissions");
    ring.clear();check(ring.size==0 && ring.omitted==0,"AR5: teardown flush reclaims bounded capture");
    // Membership/admission proof is the existing production message model; execution identity starts AFTER its restart entry.
    const lost_newcomer_model::point shapes[]{lost_newcomer_model::point::before_image,lost_newcomer_model::point::transfer,
        lost_newcomer_model::point::transfer,lost_newcomer_model::point::hold_capped,lost_newcomer_model::point::after_ready,lost_newcomer_model::point::after_start};
    for(unsigned shape=0;shape<6;++shape) for(unsigned order=0;order<2;++order) {
        const auto o=lost_newcomer_model::run(1,1,shapes[shape],order);
        restart_execution host,guest;
        host.begin(true);guest.begin(true);host.image_returned();guest.image_returned();
        host.observe_ready(true,false,false);guest.observe_ready(true,false,false);
        auto& first=order ? guest : host;auto& second=order ? host : guest;
        first.canonical();
        restart_wait wait{true,true,false,false,1,1,0,0};
        bool inert=true;
        for(unsigned n=0;n<20;++n) inert &= first.classify(wait)==restart_wait_policy::input_only;
        second.canonical();
        check(o.same_entry && o.same_frame0 && o.all_entered && o.none_inside && inert && host.canonical_seen && guest.canonical_seen,
              "AR5 shapes/orders: authoritative membership + owed/running image restart, independent execution survives early cohort/late-marker clear");
    }
}
