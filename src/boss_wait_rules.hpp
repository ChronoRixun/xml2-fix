#pragma once

// A failed prerequisite may hold a decision briefly, but never owns the brain
// forever. Observe game time only; repeated calls at a paused clock do not age it.
namespace boss_wait_rules {
struct window {
    float started=-1;
    void reset(){started=-1;}
    bool ready(float now,bool success,float limit){
        if(success){reset();return true;}
        if(started<0)started=now;
        return now-started>limit;
    }
};
}
