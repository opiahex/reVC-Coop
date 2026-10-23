#pragma once
#include "Mission.h"
#include "Radar.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vc {
inline constexpr uint32_t kMagic = 0x50434356; // VCCP, little endian
inline constexpr uint16_t kProtocol = 15;
inline constexpr uint8_t kMaxPlayers = 3, kNoPlayer = 255;
inline constexpr size_t kChannels = 1 + kMaxPlayers, kMaxPacket = 512;
inline constexpr uint8_t kReady = 1, kInVehicle = 2, kDead = 4;
inline constexpr size_t kAnimationLayers = 6;
inline constexpr size_t kMaxEntities = 192, kMaxWorldObjects = 64;
enum class Kind : uint8_t { Hello = 1, Welcome, Joined, Left, State, Rejected, Chat, Hit, Shot,
    EntityCreate,EntityUpdate,EntityRemove,EntityClaim,EntityAuthority,EntityHit,SeatRequest,SeatGrant,VehicleContact,Population,Transition,MissionState,MissionCommand,MissionBarrier,MissionReady,MissionRelease,MissionVote,MissionVoteState,Radar,Explosion };
enum class Reason : uint8_t { None, Protocol, Full, RateLimit, Timeout, MissionBusy, MissionTimeout, MissionHostLeft };
struct Vec3 { float x{}, y{}, z{}; };
struct Quaternion { float x{},y{},z{},w{1}; };
inline constexpr std::array<uint32_t,6> kAimBoneTags{5,3,22,24,32,34};
struct Animation {
    uint16_t group{},id{},flags{};
    float time{},speed{1},blend{};
};
struct PlayerState {
    Vec3 position{}, velocity{}; // metres; velocity is metres/second
    float heading{}, health{100}, armour{};
    uint16_t model{};
    uint8_t pedType{},move{1}, area{}, flags{}, hour{12}, minute{}, weather{}, second{}, oldWeather{};
    uint32_t life{1},vehicle{};
    uint16_t pedState{};
    uint8_t weapon{},weaponState{},seat{255},animationCount{},door{};
    uint16_t vehicleAnimation{65535};
    uint32_t ammo{},clip{};
    uint8_t boneMask{};
    std::array<Quaternion,kAimBoneTags.size()> bones{};
    std::array<Animation,kAnimationLayers> animations{};
};
struct Hit {
    uint8_t target{kNoPlayer},weapon{},piece{},direction{};
    uint32_t targetEpoch{},life{};
    float damage{};
    uint32_t source{},sourceGeneration{},sourceLife{1};
};
struct Shot {
    Vec3 start{},end{}; uint8_t weapon{}; uint32_t source{},sourceGeneration{};
    Vec3 normal{}; uint8_t flags{},surface{}; uint32_t sourceLife{1};
    Vec3 localMuzzle{}; uint8_t impactKind{},impactPiece{};
};
struct Explosion {
    Vec3 position{}; uint32_t source{},generation{},life{1},delay{}; uint8_t type{},sound{1};
};
struct Contact {
    uint32_t source{},sourceGeneration{},target{},targetGeneration{};
    Vec3 move{},turn{}; // change in metres/second and radians/second
};
enum class EntityKind : uint8_t { Vehicle=1,Ped,Object };
struct Transform {
    Vec3 position{},velocity{},right{1,0,0},forward{0,1,0},up{0,0,1};
};
struct VehicleState {
    Transform transform;
    Vec3 turn{};
    float health{1000},steer{},gas{},brake{};
    uint8_t type{},primary{},secondary{},gear{},engine{},flags{},passengers{3};
    std::array<uint8_t,6> doors{};
    std::array<uint8_t,4> wheels{};
    uint32_t panels{},lights{};
    std::array<float,6> doorAngles{};
    std::array<float,4> wheelAngle{},wheelOmega{},wheelPosition{},suspension{1,1,1,1};
    float wheelSteer{},bikeLean{};
};
struct ObjectState { Transform transform; uint8_t flags{1}; }; // visible, collision, broken glass/explosion
struct Entity {
    uint32_t id{},key{},generation{1},ownerEpoch{},creatorEpoch{};
    uint8_t owner{kNoPlayer},creator{kNoPlayer},area{};
    EntityKind kind{EntityKind::Vehicle}; uint16_t model{}; uint8_t mission{};
    Vec3 origin{};
    VehicleState vehicle;
    PlayerState ped;
    ObjectState object;
};
struct Message {
    Kind kind{Kind::Hello};
    uint8_t sender{kNoPlayer}, id{kNoPlayer}, host{kNoPlayer};
    uint32_t sequence{},epoch{};
    uint16_t sampleAgeMs{}; // producer-to-transport staging age, not a remote clock
    uint8_t population{};
    Reason reason{Reason::None};
    std::string name;
    std::string text;
    PlayerState state{};
    Hit hit{};
    Shot shot{};
    Contact contact;
    Explosion explosion;
    Entity entity;
    MissionState mission;
    MissionCommand command;
    CutsceneVote vote;
    RadarTrace radar;
};
inline bool Newer(uint32_t a, uint32_t b) { return a != b && uint32_t(a-b) < 0x80000000u; }
inline bool ValidText(const std::string& text,size_t maximum=192) {
    if (text.empty() || text.size()>maximum) return false;
    for (size_t i=0;i<text.size();) {
        uint32_t c=uint8_t(text[i++]),minimum=0; unsigned more=0;
        if (c>=0xC2 && c<=0xDF) { c&=31; more=1; minimum=0x80; }
        else if (c>=0xE0 && c<=0xEF) { c&=15; more=2; minimum=0x800; }
        else if (c>=0xF0 && c<=0xF4) { c&=7; more=3; minimum=0x10000; }
        else if (c>=0x80) return false;
        if (more>text.size()-i) return false;
        while (more--) { auto next=uint8_t(text[i++]); if ((next&0xC0)!=0x80) return false; c=(c<<6)|(next&63); }
        if (c<minimum || c<32 || (c>=127 && c<=159) || (c>=0xD800 && c<=0xDFFF) || c>0x10FFFF) return false;
    }
    return true;
}
inline bool ValidName(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    for (unsigned char c : s) if (c < 32 || c > 126) return false;
    return true;
}
inline bool ValidState(const PlayerState& s) {
    auto finite = [](float x, float max) { return std::isfinite(x) && std::abs(x) <= max; };
    if (!(finite(s.position.x,20000) && finite(s.position.y,20000) && finite(s.position.z,10000)
        && finite(s.velocity.x,500) && finite(s.velocity.y,500) && finite(s.velocity.z,500)
        && finite(s.heading,1000) && finite(s.health,10000) && s.health >= 0
        && finite(s.armour,10000) && s.armour >= 0 && s.model <= 300
        && s.pedType<=31 && s.move <= 7 && s.area <= 32 && s.flags <= 7 && s.hour < 24 && s.minute < 60 && s.weather <= 6 && s.oldWeather<=6 && s.second<60
        && s.life && s.pedState<=64 && s.weapon<=36 && s.weaponState<=4 && s.ammo<=1000000 && s.clip<=1000000 && s.boneMask<64
        && (s.seat==255 || s.seat<=8) && s.door<20 && (s.vehicleAnimation==65535 || s.vehicleAnimation<512) && s.animationCount<=kAnimationLayers)) return false;
    for(size_t i=0;i<s.bones.size();++i) if(s.boneMask&(1u<<i)) {
        auto q=s.bones[i]; const float length=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        if(!finite(q.x,1.001f) || !finite(q.y,1.001f) || !finite(q.z,1.001f) || !finite(q.w,1.001f) || std::abs(length-1)>.025f) return false;
    }
    for (size_t i=0;i<s.animationCount;++i) {
        const auto& a=s.animations[i];
        if (a.group>=61 || a.id>=512 || (a.flags&~0x3FF3u) || !finite(a.time,600) || a.time<0
            || !finite(a.speed,10) || a.speed<0 || !finite(a.blend,1) || a.blend<0) return false;
        for (size_t j=0;j<i;++j) if (s.animations[j].group==a.group && s.animations[j].id==a.id) return false;
    }
    return true;
}
inline bool ValidHit(const Hit& h) {
    return h.target<kMaxPlayers && h.targetEpoch && h.life && h.weapon<=47 && h.piece<=6 && h.direction<=3
        && std::isfinite(h.damage) && h.damage>0 && h.damage<=1000 && bool(h.source)==bool(h.sourceGeneration) && h.sourceLife;
}
inline bool ValidMuzzleOffset(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
        && p.x*p.x+p.y*p.y+p.z*p.z<=32*32;
}
inline bool ValidShot(const Shot& s) {
    auto point=[](Vec3 p){return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
        && std::abs(p.x)<=20000 && std::abs(p.y)<=20000 && std::abs(p.z)<=10000;};
    const auto dx=s.start.x-s.end.x,dy=s.start.y-s.end.y,dz=s.start.z-s.end.z;
    return ((s.weapon>=17 && s.weapon<=33) || s.weapon==35) && point(s.start) && point(s.end)
        && dx*dx+dy*dy+dz*dz<=4000000 && bool(s.source)==bool(s.sourceGeneration) && s.sourceLife
        && s.flags<=7 && s.impactKind<=5 && ValidMuzzleOffset(s.localMuzzle) && std::isfinite(s.normal.x) && std::isfinite(s.normal.y) && std::isfinite(s.normal.z)
        && std::abs(s.normal.x)<=1.001f && std::abs(s.normal.y)<=1.001f && std::abs(s.normal.z)<=1.001f
        && (!(s.flags&1) || std::abs(s.normal.x*s.normal.x+s.normal.y*s.normal.y+s.normal.z*s.normal.z-1)<.05f);
}
inline bool ValidPoint(Vec3 v,float max=20000) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && std::abs(v.x)<=max && std::abs(v.y)<=max && std::abs(v.z)<=max;
}
inline bool ValidExplosion(const Explosion& e) {
    return ValidPoint(e.position) && e.type<=11 && e.sound<=1 && e.life && bool(e.source)==bool(e.generation) && e.delay<=10000;
}
inline bool ValidContact(const Contact& c) {
    return c.source && c.target && c.source!=c.target && c.sourceGeneration && c.targetGeneration
        && ValidPoint(c.move,30) && ValidPoint(c.turn,12);
}
inline Vec3 EntityPosition(const Entity& e) {
    return e.kind==EntityKind::Ped ? e.ped.position : e.kind==EntityKind::Vehicle ? e.vehicle.transform.position : e.object.transform.position;
}
inline bool ValidTransform(const Transform& t) {
    auto length=[](Vec3 v){return v.x*v.x+v.y*v.y+v.z*v.z;};
    auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    return ValidPoint(t.position) && ValidPoint(t.velocity,500)
        && ValidPoint(t.right,1.1f) && ValidPoint(t.forward,1.1f) && ValidPoint(t.up,1.1f)
        && std::abs(length(t.right)-1)<.15f && std::abs(length(t.forward)-1)<.15f && std::abs(length(t.up)-1)<.15f
        && std::abs(dot(t.right,t.forward))<.15f && std::abs(dot(t.right,t.up))<.15f && std::abs(dot(t.forward,t.up))<.15f;
}
inline bool ValidEntity(const Entity& e) {
    if(!e.key || !e.generation || e.mission>1 || e.area>32 || !ValidPoint(e.origin)) return false;
    if(e.kind==EntityKind::Ped) return e.model<=129 && ValidState(e.ped) && e.ped.model==e.model;
    if(e.kind==EntityKind::Object) return e.model<6500 && ValidTransform(e.object.transform) && e.object.flags<=127;
    if(e.kind!=EntityKind::Vehicle || e.model<130 || e.model>236) return false;
    const auto& v=e.vehicle;
    if(!ValidTransform(v.transform) || !ValidPoint(v.turn,50) || !std::isfinite(v.health) || v.health<0 || v.health>10000
        || !std::isfinite(v.steer) || std::abs(v.steer)>2 || !std::isfinite(v.gas) || std::abs(v.gas)>1.1f
        || !std::isfinite(v.brake) || v.brake<0 || v.brake>1.1f || v.type>5 || v.type==2 || v.gear>10 || v.flags>15 || v.passengers>8
        || (v.panels&0xF0000000) || v.lights>255) return false;
    for(auto door:v.doors) if(door>3) return false;
    for(auto wheel:v.wheels) if(wheel>2) return false;
    for(auto angle:v.doorAngles) if(!std::isfinite(angle) || std::abs(angle)>4) return false;
    for(size_t i=0;i<4;++i) if(!std::isfinite(v.wheelAngle[i]) || std::abs(v.wheelAngle[i])>3.142f
        || !std::isfinite(v.wheelOmega[i]) || std::abs(v.wheelOmega[i])>2000
        || !std::isfinite(v.wheelPosition[i]) || std::abs(v.wheelPosition[i])>10
        || !std::isfinite(v.suspension[i]) || std::abs(v.suspension[i])>5) return false;
    if(!std::isfinite(v.wheelSteer) || std::abs(v.wheelSteer)>2 || !std::isfinite(v.bikeLean) || std::abs(v.bikeLean)>4) return false;
    for(unsigned p=0;p<7;++p) if(((v.panels>>(4*p))&15)>3) return false;
    return true;
}
struct Writer {
    std::vector<uint8_t> bytes;
    void U8(uint8_t x) { bytes.push_back(x); }
    void U16(uint16_t x) { U8(uint8_t(x)); U8(uint8_t(x >> 8)); }
    void U32(uint32_t x) { U16(uint16_t(x)); U16(uint16_t(x >> 16)); }
    void F32(float x) { U32(std::bit_cast<uint32_t>(x)); }
    void String(const std::string& x) { U8(uint8_t(x.size())); bytes.insert(bytes.end(),x.begin(),x.end()); }
    void Vector(Vec3 v) { F32(v.x); F32(v.y); F32(v.z); }
    void Player(const PlayerState& s) {
        Vector(s.position); Vector(s.velocity); F32(s.heading); F32(s.health); F32(s.armour); U16(s.model);
        for(uint8_t b:{s.pedType,s.move,s.area,s.flags,s.hour,s.minute,s.weather,s.second,s.oldWeather}) U8(b);
        U32(s.life); U32(s.vehicle); U16(s.pedState); U8(s.weapon); U8(s.weaponState); U8(s.seat); U8(s.door); U16(s.vehicleAnimation); U32(s.ammo); U32(s.clip); U8(s.boneMask);
        for(size_t i=0;i<s.bones.size();++i) if(s.boneMask&(1u<<i)) { auto q=s.bones[i]; F32(q.x); F32(q.y); F32(q.z); F32(q.w); }
        U8(s.animationCount);
        for(size_t i=0;i<std::min(size_t(s.animationCount),kAnimationLayers);++i) {
            const auto& a=s.animations[i]; U16(a.group); U16(a.id); U16(a.flags); F32(a.time); F32(a.speed); F32(a.blend);
        }
    }
    void Matrix(const Transform& t) { Vector(t.position); Vector(t.velocity); Vector(t.right); Vector(t.forward); Vector(t.up); }
    void World(const Entity& e) {
        U32(e.id); U32(e.key); U32(e.generation); U8(e.owner); U32(e.ownerEpoch); U8(e.creator); U32(e.creatorEpoch);
        U8(uint8_t(e.kind)); U16(e.model); U8(e.mission); U8(e.area); Vector(e.origin);
        if(e.kind==EntityKind::Ped) Player(e.ped);
        else if(e.kind==EntityKind::Object) { Matrix(e.object.transform); U8(e.object.flags); }
        else {
            const auto& v=e.vehicle; Matrix(v.transform); Vector(v.turn); F32(v.health); F32(v.steer); F32(v.gas); F32(v.brake);
            for(auto b:{v.type,v.primary,v.secondary,v.gear,v.engine,v.flags,v.passengers}) U8(b);
            for(auto b:v.doors) U8(b);
            for(auto b:v.wheels) U8(b);
            U32(v.panels); U32(v.lights);
            for(auto angle:v.doorAngles) F32(angle);
            for(size_t i=0;i<4;++i) { F32(v.wheelAngle[i]); F32(v.wheelOmega[i]); F32(v.wheelPosition[i]); F32(v.suspension[i]); }
            F32(v.wheelSteer);F32(v.bikeLean);
        }
    }
};
struct Reader {
    std::span<const uint8_t> bytes; size_t pos{}; bool ok{true};
    uint8_t U8() { if (pos >= bytes.size()) { ok=false; return 0; } return bytes[pos++]; }
    uint16_t U16() { uint16_t a=U8(); return uint16_t(a | uint16_t(U8())<<8); }
    uint32_t U32() { uint32_t a=U16(); return a | uint32_t(U16())<<16; }
    float F32() { return std::bit_cast<float>(U32()); }
    Vec3 Vector() { float x=F32(), y=F32(), z=F32(); return {x,y,z}; }
    std::string String() {
        size_t n=U8(); if (!ok || n > bytes.size()-pos) { ok=false; return {}; }
        std::string s(reinterpret_cast<const char*>(bytes.data()+pos),n); pos+=n; return s;
    }
    PlayerState Player() {
        PlayerState s; s.position=Vector(); s.velocity=Vector(); s.heading=F32(); s.health=F32(); s.armour=F32(); s.model=U16();
        s.pedType=U8(); s.move=U8(); s.area=U8(); s.flags=U8(); s.hour=U8(); s.minute=U8(); s.weather=U8(); s.second=U8(); s.oldWeather=U8();
        s.life=U32(); s.vehicle=U32(); s.pedState=U16(); s.weapon=U8(); s.weaponState=U8(); s.seat=U8(); s.door=U8(); s.vehicleAnimation=U16(); s.ammo=U32(); s.clip=U32(); s.boneMask=U8();
        if(s.boneMask>=64) { ok=false; return s; }
        for(size_t i=0;i<s.bones.size();++i) if(s.boneMask&(1u<<i)) { const float x=F32(),y=F32(),z=F32(),w=F32(); s.bones[i]={x,y,z,w}; }
        s.animationCount=U8();
        if(s.animationCount>kAnimationLayers) { ok=false; return s; }
        for(size_t i=0;i<s.animationCount;++i) {
            auto& a=s.animations[i]; a.group=U16(); a.id=U16(); a.flags=U16(); a.time=F32(); a.speed=F32(); a.blend=F32();
        }
        return s;
    }
    Transform Matrix() { Transform t; t.position=Vector(); t.velocity=Vector(); t.right=Vector(); t.forward=Vector(); t.up=Vector(); return t; }
    Entity World() {
        Entity e; e.id=U32(); e.key=U32(); e.generation=U32(); e.owner=U8(); e.ownerEpoch=U32(); e.creator=U8(); e.creatorEpoch=U32();
        e.kind=EntityKind(U8()); e.model=U16(); e.mission=U8(); e.area=U8(); e.origin=Vector();
        if(e.kind==EntityKind::Ped) e.ped=Player();
        else if(e.kind==EntityKind::Object) { e.object.transform=Matrix(); e.object.flags=U8(); }
        else if(e.kind==EntityKind::Vehicle) {
            auto& v=e.vehicle; v.transform=Matrix(); v.turn=Vector(); v.health=F32(); v.steer=F32(); v.gas=F32(); v.brake=F32();
            v.type=U8(); v.primary=U8(); v.secondary=U8(); v.gear=U8(); v.engine=U8(); v.flags=U8(); v.passengers=U8();
            for(auto& b:v.doors) b=U8();
            for(auto& b:v.wheels) b=U8();
            v.panels=U32(); v.lights=U32();             for(auto& angle:v.doorAngles) angle=F32();
            for(size_t i=0;i<4;++i) { v.wheelAngle[i]=F32(); v.wheelOmega[i]=F32(); v.wheelPosition[i]=F32(); v.suspension[i]=F32(); }
            v.wheelSteer=F32();v.bikeLean=F32();
        } else ok=false;
        return e;
    }
};
inline std::vector<uint8_t> Encode(const Message& m) {
    Writer w; w.U32(kMagic); w.U16(kProtocol); w.U8(uint8_t(m.kind)); w.U8(m.sender); w.U32(m.sequence); w.U32(m.epoch);
    switch (m.kind) {
        case Kind::Hello: w.String(m.name); break;
        case Kind::Welcome: w.U8(m.id); w.U8(m.host); break;
        case Kind::Joined: w.U8(m.id); w.String(m.name); break;
        case Kind::Left: w.U8(m.id); w.U8(m.host); break;
        case Kind::Rejected: w.U8(uint8_t(m.reason)); break;
        case Kind::Chat: w.String(m.text); break;
        case Kind::Hit: {
            const auto& h=m.hit; w.U8(h.target); w.U32(h.targetEpoch); w.U32(h.life);
            w.U8(h.weapon); w.U8(h.piece); w.U8(h.direction); w.F32(h.damage); w.U32(h.source); w.U32(h.sourceGeneration); w.U32(h.sourceLife); break;
        }
        case Kind::Shot: w.Vector(m.shot.start); w.Vector(m.shot.end); w.U8(m.shot.weapon); w.U32(m.shot.source); w.U32(m.shot.sourceGeneration); w.Vector(m.shot.normal); w.U8(m.shot.flags); w.U8(m.shot.surface); w.U32(m.shot.sourceLife); w.Vector(m.shot.localMuzzle); w.U8(m.shot.impactKind); w.U8(m.shot.impactPiece); break;
        case Kind::EntityCreate: case Kind::EntityUpdate: case Kind::EntityAuthority: w.World(m.entity); if(m.kind==Kind::EntityUpdate) w.U16(m.sampleAgeMs); break;
        case Kind::EntityRemove: case Kind::EntityClaim: w.U32(m.entity.id); w.U32(m.entity.generation); break;
        case Kind::SeatRequest: case Kind::SeatGrant: w.U32(m.entity.id); w.U32(m.entity.generation); w.U8(m.id); w.U8(m.host); break;
        case Kind::EntityHit: w.U32(m.entity.id); w.U32(m.entity.generation); w.F32(m.hit.damage); w.U8(m.hit.weapon); w.U8(m.hit.piece); w.U8(m.hit.direction); w.U32(m.hit.source); w.U32(m.hit.sourceGeneration); w.U32(m.hit.sourceLife); break;
        case Kind::VehicleContact:
            w.U32(m.contact.source); w.U32(m.contact.sourceGeneration); w.U32(m.contact.target); w.U32(m.contact.targetGeneration);
            w.Vector(m.contact.move); w.Vector(m.contact.turn); break;
        case Kind::Explosion: w.Vector(m.explosion.position); w.U32(m.explosion.source); w.U32(m.explosion.generation); w.U32(m.explosion.life); w.U32(m.explosion.delay); w.U8(m.explosion.type); w.U8(m.explosion.sound); break;
        case Kind::Population: w.U8(m.population); break;
        case Kind::MissionState: {
            const auto& v=m.mission; w.U32(v.run); w.U32(v.stage); w.U32(v.ip); w.U32(v.cutsceneMs); w.U16(v.mission);
            w.U8(v.active); w.U8(v.result); w.U8(v.cinematic); w.U8(v.skipped);
            w.U8(v.marker); w.U8(v.waitMask); w.U8(v.markerArea); w.U8(v.markerFlags); for(float f:v.bounds) w.F32(f);
            for(char ch:v.thread) w.U8(uint8_t(ch));
            w.U8(v.counterCount);
            for(size_t i=0;i<std::min(size_t(v.counterCount),kMissionCounters);++i) { w.U16(v.offsets[i]); w.U32(v.values[i]); }
            break;
        }
        case Kind::MissionCommand: case Kind::MissionBarrier: {
            const auto& v=m.command; w.U32(v.run); w.U32(v.ip); w.U16(v.opcode);
            if(const auto* op=FindScriptOp(v.opcode)) for(size_t i=0;op->signature[i];++i) { w.U32(v.args[i].value); if(op->signature[i]=='l') w.U32(v.args[i].extra); }
            break;
        }
        case Kind::MissionReady: case Kind::MissionRelease: break;
        case Kind::Radar: {
            const auto& v=m.radar; w.U8(v.slot); w.U8(v.active); w.U8(v.type); w.U8(v.display); w.U8(v.flags); w.U16(v.generation); w.U16(v.sprite); w.U16(v.scale); w.U32(v.colour); w.U32(v.entity); for(float f:v.position) w.F32(f); break;
        }
        case Kind::MissionVote: w.U8(m.id); break;
        case Kind::MissionVoteState: w.U32(m.vote.scene); w.U8(m.vote.members); w.U8(m.vote.yes); w.U8(m.vote.open); w.U8(m.vote.declined); w.U8(m.vote.passed); break;
        case Kind::Transition: case Kind::State: {
            w.Player(m.state);
            break;
        }
    }
    return w.bytes;
}
inline bool Decode(std::span<const uint8_t> bytes, Message& output) {
    if (bytes.size()<16 || bytes.size()>kMaxPacket) return false;
    Reader r{bytes}; Message m;
    if (r.U32()!=kMagic || r.U16()!=kProtocol) return false;
    m.kind=Kind(r.U8()); m.sender=r.U8(); m.sequence=r.U32(); m.epoch=r.U32();
    switch (m.kind) {
        case Kind::Explosion: m.explosion.position=r.Vector();m.explosion.source=r.U32();m.explosion.generation=r.U32();m.explosion.life=r.U32();m.explosion.delay=r.U32();m.explosion.type=r.U8();m.explosion.sound=r.U8();if(!ValidExplosion(m.explosion)) return false;break;
        case Kind::Hello: m.name=r.String(); if (!ValidName(m.name)) return false; break;
        case Kind::Welcome: m.id=r.U8(); m.host=r.U8(); if (m.id>=kMaxPlayers || m.host>=kMaxPlayers) return false; break;
        case Kind::Joined: m.id=r.U8(); m.name=r.String(); if (m.id>=kMaxPlayers || !m.epoch || !ValidName(m.name)) return false; break;
        case Kind::Left: m.id=r.U8(); m.host=r.U8(); if (m.id>=kMaxPlayers || (m.host>=kMaxPlayers && m.host!=kNoPlayer)) return false; break;
        case Kind::Rejected: m.reason=Reason(r.U8()); if (m.reason<Reason::Protocol || m.reason>Reason::MissionHostLeft) return false; break;
        case Kind::Chat: m.text=r.String(); if (!ValidText(m.text)) return false; break;
        case Kind::Hit: {
            auto& h=m.hit; h.target=r.U8(); h.targetEpoch=r.U32(); h.life=r.U32();
            h.weapon=r.U8(); h.piece=r.U8(); h.direction=r.U8(); h.damage=r.F32(); h.source=r.U32(); h.sourceGeneration=r.U32(); h.sourceLife=r.U32();
            if (!ValidHit(h)) return false;
            break;
        }
        case Kind::Shot: {
            m.shot.start=r.Vector(); m.shot.end=r.Vector(); m.shot.weapon=r.U8(); m.shot.source=r.U32(); m.shot.sourceGeneration=r.U32(); m.shot.normal=r.Vector(); m.shot.flags=r.U8(); m.shot.surface=r.U8(); m.shot.sourceLife=r.U32(); m.shot.localMuzzle=r.Vector(); m.shot.impactKind=r.U8(); m.shot.impactPiece=r.U8();
            if (!ValidShot(m.shot)) return false;
            break;
        }
        case Kind::EntityCreate: case Kind::EntityUpdate: case Kind::EntityAuthority:
            m.entity=r.World(); if(m.kind==Kind::EntityUpdate) m.sampleAgeMs=r.U16(); if(!ValidEntity(m.entity) || m.sampleAgeMs>250) return false; break;
        case Kind::EntityRemove: case Kind::EntityClaim:
            m.entity.id=r.U32(); m.entity.generation=r.U32(); if(!m.entity.id || !m.entity.generation) return false; break;
        case Kind::SeatRequest: case Kind::SeatGrant:
            m.entity.id=r.U32(); m.entity.generation=r.U32(); m.id=r.U8(); m.host=r.U8();
            if(!m.entity.id || !m.entity.generation || (m.id!=255 && (m.id<1 || m.id>8)) || (m.host!=kNoPlayer && m.host>=kMaxPlayers)) return false;
            break;
        case Kind::EntityHit:
            m.entity.id=r.U32(); m.entity.generation=r.U32(); m.hit.damage=r.F32(); m.hit.weapon=r.U8(); m.hit.piece=r.U8(); m.hit.direction=r.U8();
            m.hit.source=r.U32(); m.hit.sourceGeneration=r.U32(); m.hit.sourceLife=r.U32();
            if(!m.entity.id || !m.entity.generation || !std::isfinite(m.hit.damage) || m.hit.damage<=0 || m.hit.damage>1000 || m.hit.weapon>47 || m.hit.piece>6 || m.hit.direction>3 || bool(m.hit.source)!=bool(m.hit.sourceGeneration) || !m.hit.sourceLife) return false;
            break;
        case Kind::VehicleContact:
            m.contact.source=r.U32(); m.contact.sourceGeneration=r.U32(); m.contact.target=r.U32(); m.contact.targetGeneration=r.U32();
            m.contact.move=r.Vector(); m.contact.turn=r.Vector(); if(!ValidContact(m.contact)) return false; break;
        case Kind::Population: m.population=r.U8(); if(m.population>=1u<<kMaxPlayers) return false; break;
        case Kind::MissionState: {
            auto& v=m.mission; v.run=r.U32(); v.stage=r.U32(); v.ip=r.U32(); v.cutsceneMs=r.U32(); v.mission=r.U16();
            v.active=r.U8(); v.result=r.U8(); v.cinematic=r.U8(); v.skipped=r.U8();
            v.marker=r.U8(); v.waitMask=r.U8(); v.markerArea=r.U8(); v.markerFlags=r.U8(); for(float& f:v.bounds) f=r.F32();
            for(char& ch:v.thread) ch=char(r.U8());
            v.counterCount=r.U8(); if(v.counterCount>kMissionCounters) return false;
            for(size_t i=0;i<v.counterCount;++i) { v.offsets[i]=r.U16(); v.values[i]=r.U32(); }
            if(!ValidMissionState(v)) return false;
            break;
        }
        case Kind::MissionCommand: case Kind::MissionBarrier: {
            auto& v=m.command; v.run=r.U32(); v.ip=r.U32(); v.opcode=r.U16(); const auto* op=FindScriptOp(v.opcode); if(!op) return false;
            for(size_t i=0;op->signature[i];++i) { v.args[i].value=r.U32(); if(op->signature[i]=='l') v.args[i].extra=r.U32(); }
            if(!ValidMissionCommand(v) || (m.kind==Kind::MissionBarrier && !IsMissionBarrier(v.opcode))) return false;
            break;
        }
        case Kind::MissionReady: case Kind::MissionRelease: if(!m.sequence) return false; break;
        case Kind::Radar: {
            auto& v=m.radar; v.slot=r.U8(); v.active=r.U8(); v.type=r.U8(); v.display=r.U8(); v.flags=r.U8(); v.generation=r.U16(); v.sprite=r.U16(); v.scale=r.U16(); v.colour=r.U32(); v.entity=r.U32(); for(float& f:v.position) f=r.F32(); if(!m.sequence || !ValidRadar(v)) return false; break;
        }
        case Kind::MissionVote: m.id=r.U8(); if(m.id>1 || !m.sequence) return false; break;
        case Kind::MissionVoteState: m.vote.scene=r.U32(); m.vote.members=r.U8(); m.vote.yes=r.U8(); m.vote.open=r.U8(); m.vote.declined=r.U8(); m.vote.passed=r.U8(); if(!m.vote.Valid()) return false; break;
        case Kind::Transition: case Kind::State: {
            m.state=r.Player(); if (!ValidState(m.state)) return false;
            break;
        }
        default: return false;
    }
    if (!r.ok || r.pos!=bytes.size()) return false;
    output=std::move(m); return true;
}
}
