#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The first game's scan turret (scanent + scanturretent), as plain rules the native adapter
// (xml1_turrets.cpp) drives. Every timing, default and decision cites the original executable
// (default.xbe virtual addresses); docs/xml1-controllers.md has the full map. No game data here:
// the builder supplies each placement's authored values (SPEC "XML1 scan turrets").
namespace turret_rules {
constexpr float radians_per_degree=0.0174532924f;   // XBE 0x3c7080
constexpr float pi=3.14159265f,two_pi=6.28318548f;

// Scanner constructor 0xa8600 defaults, overridden by the attribute reader 0xa83e0. The reader
// multiplies the four angles by radians-per-degree; it stores resetdelay twice (+0x2d4 track loss,
// +0x2d0 sweep pause) and reads no turndelay.
struct scanner {
    float visrange=600.f,minspeed=0.f,mintime=.75f,track_loss=10.f,sweep_pause=3.f;
    float visextent=0.523598790f,yawextent=0.785398185f,pitchextent=0.785398185f,turnrate=0.349065840f;
    int filterteam=29; // PC team bit; XML1 0x1b (hero) + 2
};
enum class weapon_kind { bullet=0, flame=1, projectile=2, beam=3 };   // XBE type table 0x44fc44
struct weapon {
    weapon_kind kind=weapon_kind::projectile;
    float recoil=0,spread=10.f,speed=0,accuracy=0,range=0,damage_min=0,damage_max=0;
    float tracetime=0,radius=0;          // prototype +0x1bc (0 unless authored), +0x1c8 (ray half-extent)
    std::uint32_t damage_type=0x80;      // PC damage-type bits (dmg_physical = XML1 type 0, the prototype default)
    std::uint32_t damage_mods=0;         // PC damagemod bits (XML1 and PC share the bit values by name)
    int count=1;
    std::string projectile,entfile;
    std::array<std::string,3> effects;   // muzzlefx, muzzleaccfx (tracer / flame), impactfx: effects/<path>
};
struct sounds { std::string fire,rotate; };   // the weapon's firesound, the turret's rotatesound (may be empty)
struct spec {
    std::string zone,instance;
    bool spawned=false;             // '*name': spawned by a zone script (spawn()), bound by name; rest = its orientation then
    std::array<float,3> position{}; // authored placement (binds the instance; names repeat across entity types)
    std::array<float,3> rest{};   // authored orientation (radians), the scanner's rest basis (+0x30c)
    std::array<float,3> muzzle{}; // the model's Gun1 bolt in model space (0xb9e40 fires from Gun<emitter+1>)
    scanner scan;
    weapon gun;
    sounds sound;
};

// ---- parsing the builder's one-line description ------------------------------------------------
// zone|instance|px,py,pz,rx,ry,rz,mx,my,mz|visrange,minspeed,mintime,resetdelay(-1 unauthored),visextent,yawextent,pitchextent,turnrate,filterteam|
// kind,recoil,count,spread,speed,accuracy,range,damage_min,damage_max,tracetime,radius,damagetype,damagemods|
// projectile|entfile|muzzlefx,muzzleaccfx,impactfx|firesound,rotatesound
// Angles of the scanner block are degrees (as authored); rest is radians (as the map's orient).
// Damage is the resolved draw (XML1 draws one integer in trunc(min*0.7f)..trunc(min*1.3f)).
// damagetype / damagemods are the PC bit values of the weapon's authored names; an effect may be empty.
// An instance written '*name' is spawned by a zone script: it is bound by name anywhere in the zone and
// its rest basis is its orientation when bound (the original constructor reads the spawned orientation).
inline std::vector<std::string_view> split(std::string_view s,char c){
    std::vector<std::string_view> out;std::size_t at=0;
    for(;;){const auto n=s.find(c,at);out.push_back(s.substr(at,n==std::string_view::npos?std::string_view::npos:n-at));if(n==std::string_view::npos)break;at=n+1;}
    return out;
}
inline bool number(std::string_view s,float& v){
    if(s.empty())return false;
    std::string t(s);char* end=nullptr;v=std::strtof(t.c_str(),&end);
    return end==t.c_str()+t.size()&&std::isfinite(v);
}
inline bool bits(std::string_view s,std::uint32_t& v){
    if(s.empty())return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),v);
    return r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
inline bool identifier(std::string_view s,std::size_t max,bool path){
    if(s.empty()||s.size()>max)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||(path&&c=='/')))return false;
    return true;
}
inline std::optional<spec> parse(std::string_view line){
    const auto f=split(line,'|');
    if(f.size()!=9||!identifier(f[0],63,true))return std::nullopt;
    spec r;r.zone=f[0];
    std::string_view inst=f[1];
    if(!inst.empty()&&inst[0]=='*'){r.spawned=true;inst.remove_prefix(1);}
    if(!identifier(inst,31,false))return std::nullopt;
    r.instance=inst;
    const auto rest=split(f[2],',');const auto sc=split(f[3],',');const auto w=split(f[4],',');const auto fx=split(f[7],',');
    const auto snd=split(f[8],',');
    if(rest.size()!=9||sc.size()!=9||w.size()!=13||fx.size()!=3||snd.size()!=2)return std::nullopt;
    for(const auto& v:snd)if(!v.empty()&&!identifier(v,63,true))return std::nullopt;
    r.sound={std::string(snd[0]),std::string(snd[1])};
    for(int i=0;i<3;++i){
        if(!fx[i].empty()&&!identifier(fx[i],63,true))return std::nullopt;
        r.gun.effects[i]=fx[i];
    }
    float trace=0,radius=0;
    if(!number(w[9],trace)||!number(w[10],radius)||!(trace>=0&&trace<=60&&radius>=0&&radius<=1000))return std::nullopt;
    if(!bits(w[11],r.gun.damage_type)||!bits(w[12],r.gun.damage_mods))return std::nullopt;
    r.gun.tracetime=trace;r.gun.radius=radius;
    for(int i=0;i<3;++i)if(!number(rest[i],r.position[i])||!number(rest[i+3],r.rest[i])||!number(rest[i+6],r.muzzle[i]))return std::nullopt;
    for(float m:r.muzzle)if(!(std::fabs(m)<=1000))return std::nullopt;
    std::array<float,9> s{},g{};
    for(int i=0;i<9;++i)if(!number(sc[i],s[i])||!number(w[i],g[i]))return std::nullopt;
    if(!(s[0]>0&&s[1]>=0&&s[2]>=0&&(s[3]>=0||s[3]==-1)&&s[4]>0&&s[4]<=180&&s[5]>=0&&s[5]<=180&&s[6]>=0&&s[6]<=90&&s[7]>=0&&s[7]<=3600))return std::nullopt;
    if(s[8]<0||s[8]>31||s[8]!=std::floor(s[8]))return std::nullopt;
    // resetdelay -1: not authored, the constructor's track loss 10 and sweep pause 3 (0xa8600) stay
    r.scan={s[0],s[1],s[2],s[3]<0?10.f:s[3],s[3]<0?3.f:s[3],s[4]*radians_per_degree,s[5]*radians_per_degree,s[6]*radians_per_degree,s[7]*radians_per_degree,static_cast<int>(s[8])};
    if(g[0]<0||g[0]>3||g[0]!=std::floor(g[0])||!(g[1]>=0&&g[1]<=60)||g[2]<1||g[2]>8||g[2]!=std::floor(g[2]))return std::nullopt;
    if(!(g[3]>=0&&g[3]<=90&&g[4]>=0&&g[4]<=5000&&g[5]>=0&&g[6]>=0&&g[7]>=0&&g[8]>=g[7]&&g[8]<=100000))return std::nullopt;
    r.gun.kind=static_cast<weapon_kind>(static_cast<int>(g[0]));r.gun.recoil=g[1];r.gun.count=static_cast<int>(g[2]);r.gun.spread=g[3];
    r.gun.speed=g[4];r.gun.accuracy=g[5];r.gun.range=g[6];r.gun.damage_min=g[7];r.gun.damage_max=g[8];
    if(r.gun.kind==weapon_kind::projectile){
        if(!identifier(f[5],31,false)||!identifier(f[6],31,false)||r.gun.speed<=0)return std::nullopt;
        r.gun.projectile=f[5];r.gun.entfile=f[6];
    }else if(!f[5].empty()||!f[6].empty())return std::nullopt;
    else if(r.gun.kind!=weapon_kind::beam&&!(r.gun.range>0))return std::nullopt;   // a ray needs its length
    return r;
}

// ---- angles (XBE 0x120680 wrap, 0x1207a0 nearest equivalent, 0x120860 / 0x1209c0) --------------
inline float wrap(float a){
    if(a>pi||a<-pi){a=std::fmod(a+pi,two_pi);if(a<0)a+=two_pi;a-=pi;}
    return a;
}
inline float nearest(float from,float to){
    if((to>0&&from<0)||(to<0&&from>0)){
        if(to>=0){if(from+pi+(pi-to)<std::fabs(to-from))return to-two_pi;}
        else if(to+pi+(pi-from)<std::fabs(to-from))return to+two_pi;
    }
    return to;
}
using vec=std::array<float,3>;
inline vec forward(const vec& e){
    if(e[1]==0)return {std::cos(e[2]),std::sin(e[2]),0};
    return {std::cos(e[2])*std::cos(e[1]),std::sin(e[2])*std::cos(e[1]),-std::sin(e[1])};
}
inline vec angles(const vec& d){
    float yaw;
    if(d[0]==0&&d[1]==0)return {0,d[2]>0?-1.57079637f:1.57079637f,0};
    if(d[0]==0)yaw=d[1]<=0?4.71238899f:1.57079637f;
    else{yaw=std::atan2(d[1],d[0]);if(yaw<0)yaw+=two_pi;}
    return {0,-std::atan2(d[2],std::sqrt(d[0]*d[0]+d[1]*d[1])),yaw};
}
inline float dot(const vec& a,const vec& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
// XBE 0x120b00: per axis, rate*dt towards the goal; a non-zero hint turns that axis in its own
// direction (the sweep) and stops on the goal, otherwise the shorter way round.
inline vec step(vec from,vec goal,float rate,float dt,vec hint){
    for(auto& v:from)v=wrap(v);for(auto& v:goal)v=wrap(v);
    const float n=std::sqrt(dot(hint,hint));if(n>0)for(auto& v:hint)v/=n;
    for(int i=0;i<3;++i){
        if(rate==0)continue;
        if(hint[i]!=0){
            const float old=from[i],raw=old+dt*rate*hint[i],now=wrap(raw);from[i]=now;
            const bool up=hint[i]>0,wrapped=raw!=now;const float g=goal[i];
            if((old<=g&&g<=now&&up)||(g<=old&&now<=g&&!up)||(now<=g&&old<=g&&wrapped)||(g<=now&&g<=old&&wrapped))from[i]=g;
        }else{
            const float g=nearest(from[i],goal[i]);
            if(g>from[i]){const float v=from[i]+dt*rate;from[i]=g<v?g:v;}
            else if(g<from[i]){const float v=from[i]-dt*rate;from[i]=g<=v?v:g;}
        }
    }
    return from;
}

// ---- binding (adapter-side, no original counterpart) ---------------------------------------------
// When to look for a placement's entity again, and the trace timer. The game clock can go back (the
// online session restarts it, timer vt+0x10 at 0x61413d / 0x613f16): every peer must then bind at
// once, so a clock that goes backwards, like a zone change, clears everything learnt earlier in the
// process.
struct binding { float next_bind=0,trace_at=0; bool rest_taken=false; };
inline bool clock_went_back(float now,float last){return now<last;}
inline void forget(binding& b){b=binding{};}
inline bool may_bind(const binding& b,float now){return !(now<b.next_bind);}
inline void bind_failed(binding& b,float now){b.next_bind=now+1;}

// ---- the scanner state (scanent +0x324.. / scanturretent +0x358..) ------------------------------
enum mode { idle=0, scanning=1, tracking=2 };
struct state {
    int mode=idle;
    float acquire_due=0,rotate_due=0,fire_due=0;     // the three native events (+0x344, +0x34c, +0x368)
    float acquire_set=0,rotate_set=0;                // when each was scheduled (+0x334, +0x338)
    float seen=0,last_seen=0,pause=0;                // +0x32c, +0x330, +0x328
    bool sweep_back=false;                           // +0x33c bit 0
    vec aim{};                                       // +0x318
    std::uint32_t target=0;                          // +0x340
    int burst=0;                                     // +0x358
    int emitter=0;                                   // +0x360
    float last_trace=0;                              // +0x364 (ray weapons' trace-time gate)
};
// Constructor + parser + weapon setup: rotation 0.01 s, acquisition 0.3 s, firing 0.3 s.
inline state spawn(float now){
    state s;s.rotate_due=now+.01f;s.acquire_due=now+.3f;s.fire_due=now+.3f;s.rotate_set=now;s.acquire_set=now;return s;
}
// One acquisition (0xa9000). active = enabled and (tracking or the entity's awake bit). sighted: the
// nearest eligible candidate in the cone was visible. Returns the next delay.
struct sight { bool found=false; std::uint32_t target=0; vec aim{}; };
inline float acquire(state& s,const scanner& c,float now,bool active,const sight& seen){
    const float dt=now-s.acquire_set;
    if(!active){s.mode=idle;s.acquire_set=now;return 1.f;}
    if(s.mode==idle)s.mode=scanning;
    if(!seen.found){
        s.seen=0;
        if(s.mode==tracking&&now-s.last_seen>c.track_loss)s.mode=scanning;
    }else{
        s.aim=seen.aim;s.target=seen.target;
        s.seen+=dt;s.last_seen=now;
        if(s.mode==scanning&&s.seen>=c.mintime){s.mode=tracking;s.pause=0;}
        else if(s.mode==tracking)s.pause=0;
    }
    s.acquire_set=now;
    return s.mode==tracking?.1f:1.f;   // 0xa9319 (0.1) / 0xa93ce (1.0)
}
// One rotation (0xa8a90). Returns the next delay; writes the new orientation when it moved.
inline float rotate(state& s,const scanner& c,const vec& rest,float now,bool awake,vec position,vec& orientation,bool& moved,bool& turning){
    float dt=now-s.rotate_set;s.rotate_set=now;moved=false;turning=false;
    if(s.pause>0){s.pause-=dt;if(s.pause<=0)dt=0;}
    if(!(c.turnrate>0)||s.pause>0||s.mode<scanning||(s.mode!=tracking&&!awake))return 1.f;
    vec goal=orientation,hint{0,0,0};
    if(s.mode==tracking){
        vec d{s.aim[0]-position[0],s.aim[1]-position[1],s.aim[2]-position[2]};
        const float n=std::sqrt(dot(d,d));if(n>0)for(auto& v:d)v/=n;
        goal=angles(d);
        const float facing=dot(forward(rest),d);
        if(facing<std::cos(c.pitchextent)){const float lo=rest[1]-c.pitchextent,hi=rest[1]+c.pitchextent;goal[1]=goal[1]<lo?lo:(hi<goal[1]?hi:goal[1]);}
        if(facing<std::cos(c.yawextent)){
            const vec flat{d[0],d[1],0};
            const auto minus=forward({0,0,rest[2]-c.yawextent}),plus=forward({0,0,rest[2]+c.yawextent});
            goal[2]=dot(plus,flat)<=dot(minus,flat)?rest[2]-c.yawextent:rest[2]+c.yawextent;
        }
    }else if(s.sweep_back){hint[2]=-1;goal[1]=rest[1];goal[2]=rest[2]-c.yawextent;}
    else{hint[2]=1;goal[1]=rest[1];goal[2]=rest[2]+c.yawextent;}
    vec next=step(orientation,goal,c.turnrate,dt,hint);
    for(auto& v:goal)v=wrap(v);for(auto& v:next)v=wrap(v);
    float r=0;for(int i=0;i<3;++i)r+=(next[i]-goal[i])*(next[i]-goal[i]);
    if(!(r<.001f))turning=true;
    else if(s.mode==scanning){s.pause=c.sweep_pause;s.sweep_back=!s.sweep_back;}
    moved=next!=orientation;orientation=next;
    return .01f;
}
// The firing event (0xa9a40, 0xa9960, 0xa99e0). fire(emitter) emits one weapon shot and returns the
// weapon's recoil (prototype fire 0xbcba0 returns +0x1b4). emitters: the weapon's emitter count.
enum class fire_result { none, shot, burst_started };
inline fire_result fire(state& s,float now,int emitters,float recoil,float& next){
    if(s.mode!=tracking){next=1.f;return fire_result::none;}
    const int old=s.burst--;
    if(old>0){
        if(++s.emitter>=emitters)s.emitter=0;
        next=recoil;return fire_result::shot;
    }
    if(now-s.last_seen<1.f){s.burst=20;next=1.f;return fire_result::burst_started;}
    next=1.f;return fire_result::none;
}
// XBE 0x5a090: count projectiles fan out by spread degrees about the yaw; each one's speed scales
// by (0.1*k + 1). k is the projectile's offset from the centre of the fan.
// One integer draw lo..hi inclusive from a [0,1) random (XBE 0xbcc47..0xbcc99, RNG 0x1233c0).
inline float draw(float lo,float hi,float random){
    const float span=std::floor(hi)-std::floor(lo)+1;float v=std::floor(lo)+std::floor(random*span);
    return v>std::floor(hi)?std::floor(hi):v;
}
// XBE projectile update 0x96ba0 homing: the velocity direction plus f x the unit direction to the
// target's origin, f = (10 + age) x 0.05 clamped to [0.001, 1.5], at the same speed; nozhoming
// (+0x335 bit 0) zeroes the vertical part. Returns the input when either vector is zero.
inline vec home(const vec& velocity,const vec& to_target,float age,bool level){
    const float speed=std::sqrt(dot(velocity,velocity)),n=std::sqrt(dot(to_target,to_target));
    if(!(speed>0)||!(n>0))return velocity;
    float f=(10.f+age)*.05f;f=f>1.5f?1.5f:(f<.001f?.001f:f);
    vec w{velocity[0]/speed+to_target[0]/n*f,velocity[1]/speed+to_target[1]/n*f,velocity[2]/speed+to_target[2]/n*f};
    if(level)w[2]=0;
    const float m=std::sqrt(dot(w,w));
    if(!(m>0))return velocity;
    for(auto& x:w)x*=speed/m;
    return w;
}
// ---- ray weapons (bullet 0xba420, flame 0xba850) ---------------------------------------------------
// Aim (0xb9e40, the non-character branch 0xba1d2..0xba268): the muzzle frame rotated by a random
// pitch and yaw, each 0x1233a0(-accuracy, accuracy) radians (the reader 0xbc3e3 stores the authored
// degrees x 0.0174533 at +0x1b0); yaw draws first. The local forward (Euler 0, pitch, yaw) is then
// carried by the source's own frame (yaw about z, then pitch, forward() conventions).
inline vec aim(const vec& orientation,float accuracy,float yaw_random,float pitch_random){
    const float yaw=-accuracy+yaw_random*2*accuracy,pitch=-accuracy+pitch_random*2*accuracy;
    const vec l=forward({0,pitch,yaw});
    const float cp=std::cos(orientation[1]),sp=std::sin(orientation[1]),cy=std::cos(orientation[2]),sy=std::sin(orientation[2]);
    const vec p{cp*l[0]+sp*l[2],l[1],-sp*l[0]+cp*l[2]};   // pitch: forward (1,0,0) -> (cos p, 0, -sin p)
    return {cy*p[0]-sy*p[1],sy*p[0]+cy*p[1],p[2]};
}
// A model-space point carried by the entity frame (yaw about z, then pitch; forward() conventions):
// the bolt's world position (0xb9e40: bolt matrix x entity matrix).
inline vec frame_point(const vec& position,const vec& orientation,const vec& local){
    const float cp=std::cos(orientation[1]),sp=std::sin(orientation[1]),cy=std::cos(orientation[2]),sy=std::sin(orientation[2]);
    const vec p{cp*local[0]+sp*local[2],local[1],-sp*local[0]+cp*local[2]};
    return {position[0]+cy*p[0]-sy*p[1],position[1]+sy*p[0]+cy*p[1],position[2]+p[2]};
}
// The traced segment (0xba5ad.. / 0xbaa06..): from 36 units behind the muzzle (x -36.0 at 0x3d37bc)
// to range (prototype +0x1c4) ahead; extents 0 for a bullet, the weapon radius (+0x1c8) for a flame.
struct segment{vec from,to,extent;};
inline segment trace_segment(const vec& origin,const vec& direction,float range,float radius,weapon_kind kind){
    segment g;
    for(int i=0;i<3;++i){g.from[i]=origin[i]-36.f*direction[i];g.to[i]=origin[i]+range*direction[i];}
    const float e=kind==weapon_kind::flame?static_cast<float>(static_cast<short>(radius)):0.f;
    g.extent={e,e,e};
    return g;
}
// Trace-time gate of a scanturretent source (0xba4e2..0xba551 / 0xba92a..): while the last trace
// time (+0x364) is not negative, a fire within tracetime of it traces nothing; a trace records now.
inline bool trace_ready(float& last,float now,float tracetime){
    if(!(last>=0))return true;
    if(now-last<tracetime)return false;
    last=now;return true;
}
inline float fan_offset(int i,int count){
    if(count==1)return 0;
    if((count&1)==0)return static_cast<float>(i-count/2)+.5f;
    return static_cast<float>(i-(count-1)/2);
}
}
