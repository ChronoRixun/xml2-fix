// Included in the normal CI xml2_test executable; all samples are invented.
void check_linked_frame_rate() {
    using namespace linked_frame_rate_core;
    std::puts("Linked session frame rate:");
    CHECK(host_target(30,60)==30);
    CHECK(host_target(60,30)==30);
    CHECK(host_target(144,60)==60);
    CHECK(host_target(0,60)==60);
    CHECK(host_target(49,60)==49);
    for (unsigned host : {30u,60u}) {
        const unsigned local = host==30 ? 60 : 30;
        session guest;
        CHECK(guest.target()==0);
        const auto bytes=encode(host);unsigned parsed=0;
        CHECK(decode(bytes.data(),bytes.size(),parsed));
        CHECK(guest.adopt(parsed) && guest.target()==host);
        CHECK(!guest.observe_online(false) && guest.target()==host); // late join, no shared frame yet
        CHECK(guest.adopt(host)); // repeated roster / mesh reply
        CHECK(!guest.adopt(local) && guest.target()==host); // fixed through level boundaries
        guest.observe_online(true);
        CHECK(guest.observe_online(false) && guest.target()==0); // native leave
        CHECK(guest.adopt(host));guest.clear();CHECK(guest.target()==0); // room leave / failed join
    }
    for (unsigned bad : {0u,9u,61u,1000u}) {
        const auto b=encode(bad);unsigned fps=77;session s;
        CHECK(!decode(b.data(),b.size(),fps) && !fps && !s.adopt(bad));
    }
    for (unsigned exact : {10u,49u}) {
        const auto bytes=encode(exact);unsigned parsed=0;session peer;
        CHECK(decode(bytes.data(),bytes.size(),parsed) && peer.adopt(parsed) && peer.target()==exact);
    }
    for (unsigned at : {0u,4u,7u}) {
        auto b=encode(60);++b[at];unsigned fps=0;
        CHECK(!decode(b.data(),b.size(),fps));
    }
    unsigned fps=0;auto b=encode(60);
    session unrelated;
    CHECK(!unrelated.observe_online(true));
    CHECK(unrelated.adopt(30) && !unrelated.observe_online(false) && unrelated.target()==30);
    session room;
    CHECK(room.adopt(60,true));room.observe_online(true);
    CHECK(!room.observe_online(false) && room.target()==60); // cohort reset/loading retains room ownership
    CHECK(room.adopt(60)); // native follow-up cannot weaken that lifetime
    CHECK(!room.observe_online(false) && room.target()==60);
    room.clear();CHECK(room.target()==0);
    CHECK(!decode(nullptr,b.size(),fps));
    CHECK(!decode(b.data(),b.size()-1,fps));
    work_meter meter;
    CHECK(meter.state(0)==capacity::unknown);
    // 33 ms of LOCAL work cannot sustain a 60 Hz budget. A short spike alone
    // is insufficient; then two clean three-second windows clear the warning.
    for(unsigned now=0;now<3000;now+=34)meter.sample(now,33000,60);
    CHECK(meter.state(2999)==capacity::unknown);
    meter.sample(3026,33000,60);
    CHECK(meter.state(3026)==capacity::slow);
    auto party_view=meter;for(unsigned t=4000;t<=10000;t+=1000)party_view.pause(t);
    CHECK(party_view.state(10000)==capacity::slow); // opening Party must not erase the warning being read
    for(unsigned t=11000;t<=20000;t+=1000)party_view.pause(t);
    CHECK(party_view.state(20000)==capacity::slow);
    party_view.reset();CHECK(party_view.state(20000)==capacity::unknown); // new zone / changed workload
    auto stale_pause=meter;stale_pause.pause(10000);
    CHECK(stale_pause.state(10000)==capacity::unknown); // a long stall cannot resurrect expired evidence
    for(unsigned now=3043;now<=6052;now+=17)meter.sample(now,4000,60);
    CHECK(meter.state(6052)==capacity::slow);
    for(unsigned now=6069;now<=9095;now+=17)meter.sample(now,4000,60);
    CHECK(meter.state(9095)==capacity::holding);
    CHECK(meter.state(15000)==capacity::unknown); // stale loading/paused report
    meter.reset();
    // A 30 fps shared cadence caused by ANOTHER PC is not evidence against
    // this one: its local work still takes just 4 ms after the barrier.
    for(unsigned now=0;now<=3060;now+=34)meter.sample(now,4000,60);
    CHECK(meter.state(3060)==capacity::holding);
    meter.sample(3077,33000,30);
    CHECK(meter.state(3077)==capacity::unknown); // new target needs new evidence
    CHECK(warning_text(30,60,capacity::unknown)=="Using host's 60 fps");
    CHECK(warning_text(60,30,capacity::unknown)=="Using host's 30 fps");
    CHECK(warning_text(60,60,capacity::slow)=="Cannot hold 60 fps");
    CHECK(warning_text(60,60,capacity::holding)=="Holding session rate");
    // beta.1's rule 29 v1 and the new host-owned v2 contract refuse in BOTH directions.
    gameplay_capabilities::record old_peer,new_peer,other_new;
    old_peer.add(29,1,{60,0,0,0},true);
    new_peer.add(29,revision,{1,1,10,60},true);
    other_new.add(29,revision,{1,1,10,60},true);
    CHECK(old_peer.finalize() && new_peer.finalize() && other_new.finalize());
    CHECK(old_peer.compare(new_peer.bytes())!=gameplay_capabilities::result::match);
    CHECK(new_peer.compare(old_peer.bytes())!=gameplay_capabilities::result::match);
    CHECK(new_peer.compare(other_new.bytes())==gameplay_capabilities::result::match);
}
