#include "common/Interpolation.h"
#include "common/WorldInterpolation.h"
#include "common/VehicleMotion.h"
#include "common/FrameLimit.h"
#include "common/FixedStep.h"
#include "common/SpawnSearch.h"
#include "net/BackgroundClient.h"
#include "ui/Renderer.h"
#include "ui/Overlay.h"
#include <windows.h>
#include <d3d9.h>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <functional>
namespace {
unsigned checks{};
void Check(bool b,const char* message) { ++checks; if(!b) throw std::runtime_error(message); }
void Wire() {
    vc::Message state; state.kind=vc::Kind::State; state.sender=0; state.epoch=1; state.sequence=1;
    state.state.position={123,-826,10}; state.state.flags=vc::kReady;
    state.state.animationCount=1; state.state.animations[0]={0,3,3,.4f,1,1};
    auto bytes=vc::Encode(state); vc::Message decoded;
    Check(vc::Decode(bytes,decoded),"state round trip");
    Check(decoded.state.position.x==123 && decoded.state.animations[0].time==.4f,"state contents");
    for(size_t i=0;i<bytes.size();++i) Check(!vc::Decode(std::span(bytes).first(i),decoded),"truncated state accepted");
    bytes[4]=10; Check(!vc::Decode(bytes,decoded),"ASI protocol accepted by source branch");
    auto invalid=state.state; invalid.position.x=std::numeric_limits<float>::quiet_NaN();
    Check(!vc::ValidState(invalid),"nonfinite position accepted");
    vc::Message npc; npc.kind=vc::Kind::EntityUpdate; npc.entity.kind=vc::EntityKind::Ped; npc.entity.model=21; npc.entity.key=12;
    npc.entity.ped=state.state; npc.entity.ped.model=21; npc.entity.ped.pedType=5; npc.entity.ped.boneMask=vc::kPoseMask;
    npc.entity.ped.animationCount=vc::kAnimationLayers;
    for(unsigned i=0;i<vc::kAnimationLayers;++i) npc.entity.ped.animations[i]={uint16_t(43+i),uint16_t(i),3,.4f,1,1};
    auto full=vc::Encode(npc);Check(full.size()<=vc::kMaxPacket && vc::Decode(full,decoded),"full NPC animation/bone packet does not fit");
    Check(decoded.entity.ped.animationCount==6 && decoded.entity.ped.boneMask==vc::kPoseMask && decoded.entity.ped.pedType==5 && decoded.entity.ped.animations[5].group==48,"NPC animation contents lost");
    npc.entity.ped.bones[0].w=0;Check(!vc::Decode(vc::Encode(npc),decoded),"invalid aim quaternion accepted");
    vc::Message bike;bike.kind=vc::Kind::EntityUpdate;bike.entity.kind=vc::EntityKind::Vehicle;bike.entity.model=191;bike.entity.key=12;bike.entity.vehicle.bikeLean=.34f;
    Check(vc::Decode(vc::Encode(bike),decoded) && decoded.entity.vehicle.bikeLean==.34f,"bike lean lost on wire");
    bike.sampleAgeMs=47;Check(vc::Decode(vc::Encode(bike),decoded) && decoded.sampleAgeMs==47,"vehicle staging age lost on wire");
    bike.sampleAgeMs=251;Check(!vc::Decode(vc::Encode(bike),decoded),"unbounded source sample age accepted");bike.sampleAgeMs=0;
    vc::Message env;env.kind=vc::Kind::Environment;env.sequence=1;env.environment.values={.4f,.5f,2.f,1.f,.7f,.2f,.3f,.1f,.9f,1.f,1.f};
    Check(vc::Decode(vc::Encode(env),decoded) && decoded.environment.values==env.environment.values,"weather transition lost on wire");
    env.environment.values[1]=std::numeric_limits<float>::quiet_NaN();Check(!vc::Decode(vc::Encode(env),decoded),"nonfinite rain accepted");
    env.environment.values[1]=1;env.environment.weather=6;Check(!vc::Decode(vc::Encode(env),decoded),"out-of-range native weather accepted");
    vc::WorldInterpolator ride;ride.Push(1,bike.entity);bike.entity.vehicle.bikeLean=.74f;ride.Push(2,bike.entity);
    Check(std::abs(ride.At(1.5).vehicle.bikeLean-.54f)<.001f,"bike lean not interpolated");
    vc::Message heli;heli.kind=vc::Kind::EntityUpdate;heli.entity.kind=vc::EntityKind::Vehicle;heli.entity.model=165;heli.entity.key=19;heli.entity.vehicle.type=3;
    heli.entity.vehicle.rotor=2.4f;heli.entity.vehicle.searchX=124;heli.entity.vehicle.searchY=-845;heli.entity.vehicle.searchIntensity=.8f;heli.entity.vehicle.heliStatus=1;
    Check(vc::Decode(vc::Encode(heli),decoded) && decoded.entity.vehicle.rotor==2.4f && decoded.entity.vehicle.searchIntensity==.8f,"police heli visual state lost");
    heli.entity.vehicle.searchIntensity=2;Check(!vc::Decode(vc::Encode(heli),decoded),"invalid searchlight intensity accepted");
    vc::Message blast;blast.kind=vc::Kind::Explosion;blast.explosion.position={123,-826,10};blast.explosion.type=3;
    Check(vc::Decode(vc::Encode(blast),decoded) && decoded.explosion.type==3,"explosion round trip");
    blast.explosion.generation=1;Check(!vc::Decode(vc::Encode(blast),decoded),"explosion source generation mismatch accepted");
    blast.explosion.generation=0;blast.explosion.type=12;Check(!vc::Decode(vc::Encode(blast),decoded),"invalid explosion type accepted");
    vc::Message pellet;pellet.kind=vc::Kind::Shot;pellet.shot.weapon=33;pellet.shot.flags=7;pellet.shot.normal={0,0,1};
    Check(vc::Decode(vc::Encode(pellet),decoded) && decoded.shot.flags==7,"impact-only sniper flags lost");
    pellet.shot.flags=8;Check(!vc::Decode(vc::Encode(pellet),decoded),"unknown shot flags accepted");
    state.kind=vc::Kind::Chat; state.text="Привет из reVC";
    Check(vc::Decode(vc::Encode(state),decoded) && decoded.text==state.text,"Cyrillic chat wire");
    vc::Interpolator track; auto a=state.state,b=a; a.position={0,0,10}; b.position={10,0,10};
    track.Push(1,a); track.Push(2,b); Check(std::abs(track.At(1.5).position.x-5)<.001,"interpolation");
    b.position.x=500; track.Push(3,b); Check(track.At(2.5).position.x==500,"teleport interpolated through world");
    b.velocity={10,0,0}; track.Push(4,b); Check(std::abs(track.At(10).position.x-501)<.001,"unbounded extrapolation");
}
void InteractionWire() {
    vc::Message m;m.kind=vc::Kind::Transition;m.state.flags=vc::kReady;
    m.state.actions=vc::kAiming|vc::kPointing;m.state.wanted=2;m.state.wantedCops=3;
    m.state.aimKind=2;m.state.aimTarget=72;m.state.aimGeneration=4;m.state.aimDirection={1,0,0};vc::Message d;
    Check(vc::Decode(vc::Encode(m),d) && d.state.aimTarget==72 && d.state.aimGeneration==4 && d.state.wantedCops==3 && d.state.actions==3,"AI aim/wanted state lost on wire");
    auto a=m.state,b=a;b.actions=0;Check(vc::CriticalTransition(a,b),"aim release not reliable");
    b=a;b.aimGeneration++;Check(vc::CriticalTransition(a,b),"stale aim target survives pool reuse");
    m.state.aimDirection={0,0,0};Check(!vc::Decode(vc::Encode(m),d),"zero aim vector accepted");m.state.aimDirection={0,1,0};
    m.state.aimOffset={0,9,0};Check(!vc::Decode(vc::Encode(m),d),"unbounded camera ray accepted");m.state.aimOffset={0,0,1};
    m.state.aimGeneration=0;Check(!vc::Decode(vc::Encode(m),d),"unversioned AI target accepted");m.state.aimGeneration=4;
    m.state.actions=16;Check(!vc::Decode(vc::Encode(m),d),"unknown action bits accepted");m.state.actions=3;
    m.state.aimKind=1;m.state.aimTarget=1;m.state.aimLife=2;
    Check(vc::Decode(vc::Encode(m),d) && d.state.aimLife==2,"player aim identity lost");
    m.state.aimLife=0;Check(!vc::Decode(vc::Encode(m),d),"unversioned player life accepted");
    m.kind=vc::Kind::Crime;m.crime={1,5,1,2,9,3,74,2,123,{20,30,10}};
    auto bytes=vc::Encode(m);Check(vc::Decode(bytes,d) && d.crime.witness==74 && d.crime.life==3 && d.crime.minimum==2,"witnessed crime identity lost");
    Check(!vc::Decode(std::span(bytes).first(bytes.size()-1),d),"truncated crime accepted");
    m.crime.type=20;Check(!vc::Decode(vc::Encode(m),d),"unknown native crime accepted");m.crime.type=5;
    m.crime.generation=0;Check(!vc::Decode(vc::Encode(m),d),"unversioned witness accepted");m.crime.generation=2;
    m.crime.minimum=6;Check(!vc::Decode(vc::Encode(m),d),"arbitrary wanted stars accepted");
}
void SkeletonPose() {
    vc::Message m;m.kind=vc::Kind::EntityUpdate;m.entity.kind=vc::EntityKind::Ped;
    m.entity.model=m.entity.ped.model=21;m.entity.key=1;
    auto& a=m.entity.ped;a.boneMask=vc::kPoseMask;a.animationCount=vc::kAnimationLayers;
    for(unsigned i=0;i<a.animationCount;++i) a.animations[i]={0,uint16_t(i),0x63,.5f,1,1,1};
    for(unsigned i=0;i<a.bones.size();++i) {float angle=float(i)*.13f;a.bones[i]={0,0,std::sin(angle),std::cos(angle)};a.bonePositions[i]={.0137f*float(i),-.7342f,.0168f*float(i)};}
    auto bytes=vc::Encode(m);vc::Message d;
    Check(bytes.size()<768 && bytes.size()<1200 && vc::Decode(bytes,d),"full skeleton with all layers fragmented or rejected");
    float error=0,dotError=0;
    for(unsigned i=0;i<a.bones.size();++i) {
        auto x=a.bones[i],y=d.entity.ped.bones[i];dotError=std::max(dotError,std::abs(1.f-(x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w)));
        auto p=a.bonePositions[i],q=d.entity.ped.bonePositions[i];error=std::max({error,std::abs(p.x-q.x),std::abs(p.y-q.y),std::abs(p.z-q.z)});
    }
    Check(error<.00025f && dotError<.00005f,"packed skeleton lost submillimetre pose precision");
    auto b=a;b.bones[3]={0,0,-a.bones[3].z,-a.bones[3].w};b.bonePositions[0].x+=.2f;
    vc::Interpolator track;track.Push(1,a);track.Push(1.05,b);auto mid=track.At(1.025);
    Check(std::abs(mid.bones[3].z-a.bones[3].z)<.0001f && std::abs(mid.bonePositions[0].x-a.bonePositions[0].x-.1f)<.0001f,"pose quaternion sign caused a full turn or root was not interpolated");
    a.bonePositions[0].x=9;Check(!vc::Decode(vc::Encode(m),d),"out-of-range skeletal displacement accepted");
    a.bonePositions[0].x=std::numeric_limits<float>::quiet_NaN();Check(!vc::Decode(vc::Encode(m),d),"nonfinite packed displacement accepted");
    a.bonePositions[0].x=0;a.boneMask|=1u<<31;Check(!vc::Decode(vc::Encode(m),d),"unknown skeleton bones accepted");
}
void SnapshotPlayback() {
    vc::SnapshotBuffer<int> history;
    Check(history.Push(1,10,1.02) && !history.Push(1,99,1.03) && !history.Push(.9,99,1.04),"late or duplicate snapshot reset history");
    for(int i=1;i<20;++i) history.Push(1+i*.05,i,1.02+i*.05+(i%2?.04:0));
    Check(history.Delay()>.12 && history.Delay()<=.18 && history.front().state==10,"arrival jitter changed capture spacing or discarded history");
    double played=history.Playout(1.9);
    history.Push(2.1,21,2.4);
    Check(history.Playout(1.901)>=played,"jitter margin played a replica backwards");
    vc::PlayerState a,b;a.position.x=1;b.position.x=2;
    vc::Interpolator ped;ped.Push(1,a);ped.Push(1.1,b);ped.Push(1.05,a);
    Check(std::abs(ped.At(1.075).position.x-1.75f)<.001f,"late NPC state erased interpolation history");
    vc::Entity car;car.id=car.key=1;car.model=130;
    vc::WorldInterpolator world;world.Push(1,car);car.vehicle.transform.position.x=1;world.Push(1.1,car);car.vehicle.transform.position.x=9;world.Push(1.05,car);
    Check(std::abs(world.At(1.075).vehicle.transform.position.x-.75f)<.001f,"late traffic state erased interpolation history");
}
void NetworkClock() {
    vc::NetworkClock c;
    Check(c.LocalTime(0,1000,87)==913 && !c.Ready(),"unsynchronized clock fallback lost source age");
    Check(c.Observe(10000,510040,510045,10085),"four-timestamp clock rejected");
    Check(c.Rtt()==80 && c.Offset()==500000 && c.ServerTime(10000)==510000,"clock offset or remote processing duration wrong");
    Check(c.LocalTime(510000,10070)==10000,"network transit age lost");
    Check(c.Observe(11000,511040,511045,11285) && c.Offset()==500000,"queued reply jumped clock mapping");
    Check(!c.Observe(10,20,60,15) && !c.Observe(0,10,10,20) && !c.Observe(30,20,20,10),"invalid clock exchange accepted");
    Check(!c.Observe(vc::kMaxNetworkTime,10,11,vc::kMaxNetworkTime+1),"overflow clock accepted");
    c.Clear();Check(!c.Ready() && c.Samples()==0,"rejoin kept stale clock");
    Check(c.Observe(500000,20040,20045,500085) && c.Offset()==-480000,"negative uptime offset broken");
    Check(c.LocalTime(20000,500080)==500000,"different machine uptimes changed capture age");
    vc::Message reply;reply.kind=vc::Kind::ClockReply;reply.sequence=1;reply.clockClientMs=10000;reply.clockServerInMs=510040;reply.clockServerOutMs=510045;
    vc::Message decoded;Check(vc::Decode(vc::Encode(reply),decoded) && decoded.clockServerOutMs==510045,"clock wire round trip");
    reply.clockServerInMs=510046;Check(!vc::Decode(vc::Encode(reply),decoded),"reversed server clock accepted on wire");
}
void VehicleMotion() {
    vc::Entity e;e.id=1;e.key=1;e.model=130;e.vehicle.transform.position={10,0,10};e.vehicle.transform.velocity={30,0,0};
    vc::VehicleMotion motion;motion.Push(1,e,1);
    Check(std::abs(motion.At(1.033).vehicle.transform.position.x-10.99f)<.001f,"car has historical collision pose");
    Check(std::abs(motion.At(5).vehicle.transform.position.x-13)<.001f,"car extrapolation unbounded after producer stall");
    e.vehicle.transform.position.x=10.99f;motion.Push(1.033,e,1.033);
    Check(std::abs(motion.At(1.066).vehicle.transform.position.x-11.98f)<.001f,"constant-speed car jitters between updates");
    e.vehicle.transform.position.x=12.2f;e.vehicle.transform.velocity={};motion.Push(1.066,e,1.066);
    Check(std::abs(motion.At(1.3).vehicle.transform.position.x-12.2f)<.001f,"stopped vehicle keeps a permanent correction offset");
    e.vehicle.transform.position.x=100;motion.Push(1.1,e,1.1);
    Check(motion.At(1.1).vehicle.transform.position.x==100,"large correction eased across the world");
    e.vehicle.transform.position.x=101;++e.generation;motion.Push(1.2,e,1.2);
    Check(motion.At(1.2).vehicle.transform.position.x==101,"ownership handoff used old owner's correction");
    e.vehicle.turn={0,0,2};e.vehicle.wheelOmega[0]=12;motion.Clear();motion.Push(2,e,2);
    auto turning=motion.At(2.1);
    Check(std::abs(turning.vehicle.transform.forward.x+std::sin(.2f))<.001f && vc::ValidTransform(turning.vehicle.transform),"turn prediction lost orthonormal vehicle rotation");
    Check(std::abs(turning.vehicle.wheelAngle[0]-1.2f)<.001f,"predicted car wheels freeze between packets");
    motion.Clear();e.vehicle.turn={};e.vehicle.transform.velocity={30,0,0};
    for(int i=0;i<90;++i) {double now=3+i/30.;e.vehicle.transform.position.x=float(30*now);motion.Push(now,e,now);}
    Check(std::abs(motion.At(6).vehicle.transform.position.x-180)<.001f,"vehicle correction drift accumulated");
}
void FixedSimulation() {
    for(int fps:{30,60,144,240,1000}) {
        vc::FixedStep clock;clock.Advance(0,true);unsigned ticks=0;
        bool valid=true;for(int i=0;i<fps*10;++i) {ticks+=clock.Advance(1./fps,true);valid&=clock.Alpha()>=0 && clock.Alpha()<=1;}
        Check(valid,"invalid render interpolation fraction");
        Check(ticks==300,"render FPS changed native simulation speed");
        Check(clock.Advance(1,false)==1 && clock.Advance(0,true)==1,"fixed scheduler failed to reset across disconnect");
    }
    vc::FixedStep clock;clock.Advance(0,true);Check(clock.Advance(10,true)==4,"unbounded physics catchup after stall");
    vc::PlayerState a,b;a.animationCount=b.animationCount=1;a.animations[0]={0,3,3,.95f,2,1,1};b.animations[0]={0,3,3,.05f,2,1,1};
    vc::Interpolator pose;pose.Push(1,a);pose.Push(1.05,b);
    Check(std::abs(pose.At(1.025).animations[0].time-1.f)<.0001f,"walk phase snapped at loop boundary");
    b.animations[0].duration=std::numeric_limits<float>::infinity();Check(!vc::ValidState(b),"invalid animation duration accepted");
}
void FrameLimits() {
    Check(vc::ParseFrameLimit("60")==60 && vc::ParseFrameLimit("144")==144,"custom frame limit ignored");
    Check(vc::ParseFrameLimit("0")==0,"uncapped frame option rejected");
    Check(vc::ParseFrameLimit("-1")==60 && vc::ParseFrameLimit("text")==60 && vc::ParseFrameLimit("")==60,"invalid frame limit accepted");
    Check(vc::ParseFrameLimit("999999999999999999")==60 && vc::ParseFrameLimit("60x")==60,"overflow or partial frame limit accepted");
    vc::SessionFrameLimit session;vc::FrameLimits original{false,75};
    auto online=session.Update(true,60,original);
    Check(online.enabled && online.fps==60,"default online frame limit is not 60");
    online=session.Update(true,144,online);Check(online.fps==144,"custom online cap overwritten");
    auto off=session.Update(false,144,online);Check(!off.enabled && off.fps==75,"disconnect failed to restore pre-session settings");
    online=session.Update(true,0,{true,45});Check(!online.enabled && online.fps>0,"uncapped play enabled limiter or zero menu divisor");
    off=session.Update(false,0,online);Check(off.enabled && off.fps==45,"second session overwrote its original settings");
    off=session.Update(false,0,{false,90});Check(!off.enabled && off.fps==90,"offline state re-applied an obsolete session cap");
}
void SpawnSearch() {
    auto floor=[](vc::Vec3 p,vc::Vec3& out){out=p; return true;};
    auto first=vc::FindNearbySpawn({0,0,10},0,{{0,0,10}},floor);
    Check(first.has_value() && std::hypot(first->x,first->y)>=1.5f,"spawn overlapping host");
    auto wall=vc::FindNearbySpawn({0,0,10},0,{{0,0,10}},[](vc::Vec3 p,vc::Vec3& out){out=p; return p.x>0;});
    Check(wall.has_value() && wall->x>0,"spawn searched through wall");
    Check(!vc::FindNearbySpawn({0,0,10},0,{},[](vc::Vec3,vc::Vec3&){return false;}),"unsafe fallback after no ground/blocked world");
    Check(!vc::FindNearbySpawn({0,0,10},0,{},[](vc::Vec3 p,vc::Vec3& out){out=p; out.z=100; return true;}),"spawn on wrong storey");
    auto next=vc::FindNearbySpawn({0,0,10},0,{*first,{0,0,10}},floor);
    Check(next.has_value() && std::hypot(next->x-first->x,next->y-first->y)>=1.2f,"spawn overlapping another player");
}
void Networking() {
    vc::NetworkRuntime runtime; Check(runtime.Ok(),"ENet initialization");
    vc::Server server; Check(server.Start(0,"127.0.0.1"),"ephemeral server");
    vc::BackgroundClient host,guest;
    Check(host.Connect("127.0.0.1",server.Port(),"Host"),"host connect queue");
    auto until=[&](std::function<bool()> predicate) {
        auto start=vc::NowMs();
        do { server.Poll(); if(predicate()) return true; Sleep(2); } while(vc::NowMs()-start<4000);
        return false;
    };
    Check(until([&]{return host.Read().status==vc::Connection::Online;}),"host handshake");
    Check(guest.Connect("127.0.0.1",server.Port(),"Guest"),"guest connect queue");
    Check(until([&]{return guest.Read().status==vc::Connection::Online;}),"guest handshake");
    auto hs=host.Read(),gs=guest.Read(); Check(hs.self==gs.host && hs.self!=gs.self,"host election");
    vc::PlayerState pose; pose.flags=vc::kReady; pose.position={119,-826,10.46f}; pose.armour=45;
    host.Publish(pose);
    Check(until([&]{auto s=guest.Read(); return s.players[hs.self].hasState && s.players[hs.self].state.armour==45;}),"native pose delivered");
    Check(host.Read().players[hs.self].hasState && host.Read().players[hs.self].state.armour==45,"own player snapshot missing without server echo");
    Check(host.SendChat("Привет из reVC"),"chat queue");
    Check(until([&]{auto s=guest.Read(); return !s.chats.empty() && s.chats.back().text=="Привет из reVC";}),"Cyrillic chat delivery");
    // Pausing the game thread must hold the actor while transport remains alive.
    pose.velocity={5,0,0}; host.Publish(pose);
    Check(until([&]{auto s=guest.Read(); return s.players[hs.self].hasState && s.players[hs.self].state.velocity.x==5;}),"moving pose delivery");
    Check(until([&]{auto s=guest.Read(); return s.players[hs.self].hasState && s.players[hs.self].state.velocity.x==0;}),"stale producer held");
    Check(host.Read().status==vc::Connection::Online,"background transport disconnected");
    Check(until([&]{return host.Read().clockSamples>=4 && guest.Read().clockSamples>=4;}),"live monotonic clock synchronization failed");
    vc::Message environment;environment.kind=vc::Kind::Environment;environment.environment.weather=2;environment.environment.oldWeather=0;
    environment.environment.values={.3f,.5f,.4f,.3f,.3f,0,0,0,0,.4f,.5f};
    Check(host.SendEvent(environment),"host weather queued");
    Check(until([&]{auto s=guest.Read();return s.hasEnvironment && s.environment.values==environment.environment.values && s.environment.weather==2;}),"host weather transition not replicated");

    // Native migration: ownership transfers include the actual NPC driver.
    auto intent=pose;intent.actions=vc::kAiming|vc::kPointing;intent.aimDirection={1,0,0};intent.aimOffset={0,0,1.4f};intent.wanted=2;intent.wantedCops=3;
    guest.Publish(intent);
    Check(until([&]{auto s=host.Read();return s.players[gs.self].hasState && s.players[gs.self].state.actions==3 && s.players[gs.self].state.wantedCops==3 && s.players[gs.self].state.aimOffset.z==1.4f;}),"guest AI intent lost on live transport");
    guest.Publish(pose);
    Check(until([&]{return host.Read().players[gs.self].hasState && host.Read().players[gs.self].state.actions==0;}),"guest ready for world authority");
    vc::Entity car; car.key=123; car.kind=vc::EntityKind::Vehicle; car.model=130; car.vehicle.transform.position=pose.position;
    vc::Message create; create.kind=vc::Kind::EntityCreate; create.entity=car;
    Check(host.SendEntity(create),"vehicle create queued");
    uint32_t carId=0,npcId=0;
    Check(until([&]{for(auto& [id,e]:host.Read().entities) if(e.state.key==123) carId=id;return carId!=0;}),"shared vehicle registered");
    vc::Entity npc;npc.key=124;npc.kind=vc::EntityKind::Ped;npc.model=7;npc.ped=pose;npc.ped.model=7;npc.ped.pedType=4;npc.ped.vehicle=carId;npc.ped.seat=0;npc.ped.flags|=vc::kInVehicle;
    npc.ped.boneMask=vc::kPoseMask;npc.ped.bonePositions[3]={.0123f,.2f,-.3f};
    create.entity=npc;Check(host.SendEntity(create),"NPC driver create queued");
    Check(until([&]{for(auto& [id,e]:host.Read().entities) if(e.state.key==124) npcId=id;return npcId!=0 && guest.Read().entities.contains(npcId);}),"NPC driver registered");
    Check(guest.Read().entities.at(npcId).state.ped.boneMask==vc::kPoseMask && std::abs(guest.Read().entities.at(npcId).state.ped.bonePositions[3].x-.0123f)<.00025f,"native skeleton lost through live UDP relay");
    vc::Message claim;claim.kind=vc::Kind::EntityClaim;claim.entity.id=carId;claim.entity.generation=1;Check(guest.SendEntity(claim),"vehicle authority request");
    Check(until([&]{auto s=host.Read(),g=guest.Read();return s.entities.at(carId).state.owner==gs.self && s.entities.at(npcId).state.owner==gs.self && g.entities.at(carId).state.owner==gs.self && g.entities.at(npcId).state.owner==gs.self;}),"NPC AI owner did not follow claimed car");
    Check(host.Read().entities.at(carId).state.generation==2 && host.Read().entities.at(npcId).state.generation==2,"authority generation did not advance");
    auto ownedCar=guest.Read().entities.at(carId).state;ownedCar.vehicle.health=731;guest.PublishEntities({ownedCar});
    Check(until([&]{return guest.Read().entities.at(carId).state.vehicle.health==731 && host.Read().entities.at(carId).state.vehicle.health==731;}),"local and remote vehicle snapshots diverged without an owner echo");
    auto aged=guest.Read().entities.at(carId).state;aged.vehicle.health=730;
    vc::Message delayed;delayed.kind=vc::Kind::EntityUpdate;delayed.entity=aged;delayed.sampleAgeMs=87;
    Check(guest.SendEntity(delayed),"aged source vehicle update queued");
    Check(until([&]{auto s=host.Read();return s.entities.at(carId).state.vehicle.health==730;}),"aged vehicle update lost in relay");
    Check(vc::NowMs()-host.Read().entities.at(carId).received>=87,"relay timestamp discarded producer staging age");
    vc::Message hit;hit.kind=vc::Kind::EntityHit;hit.entity.id=npcId;hit.entity.generation=2;hit.hit.damage=15;hit.hit.weapon=17;
    Check(host.SendEvent(hit),"shared NPC damage request");
    bool gotHit=false;
    Check(until([&]{for(auto& event:guest.TakeEvents()) if(event.message.kind==vc::Kind::EntityHit && event.message.entity.id==npcId) gotHit=true;return gotHit;}),"NPC damage was not routed to its current owner");
    // Only the actual NPC owner may report its witnessed crime; target epoch/life
    // and witness generation are checked independently of the reliable transport.
    host.Publish(pose);guest.Publish(pose);
    vc::Message crime;crime.kind=vc::Kind::Crime;crime.crime={hs.self,5,1,2,host.Read().players[hs.self].epoch,pose.life,npcId,2,99,pose.position};
    Check(guest.SendEvent(crime),"owner crime report queued");bool gotCrime=false;
    Check(until([&]{for(auto& e:host.TakeEvents()) if(e.message.kind==vc::Kind::Crime && e.message.crime.witness==npcId) gotCrime=true;return gotCrime;}),"NPC owner crime not delivered to aggressor");
    auto rejected=[&](vc::BackgroundClient& from,vc::BackgroundClient& to,const vc::Message& message) {
        to.TakeEvents();if(!from.SendEvent(message)) return false;
        bool received=false;auto end=vc::NowMs()+180;
        while(vc::NowMs()<end) {server.Poll();for(auto& e:to.TakeEvents()) if(e.message.kind==vc::Kind::Crime) received=true;Sleep(2);}
        return !received;
    };
    crime.crime.epoch++;Check(rejected(guest,host,crime),"crime reached reused player slot");crime.crime.epoch--;
    crime.crime.life++;Check(rejected(guest,host,crime),"crime reached respawned player");crime.crime.life--;
    crime.crime.generation=1;Check(rejected(guest,host,crime),"crime accepted stale NPC authority");crime.crime.generation=2;
    crime.crime.target=gs.self;crime.crime.epoch=guest.Read().players[gs.self].epoch;
    Check(rejected(host,guest,crime),"non-owner reported NPC crime");
    crime.crime.target=hs.self;crime.crime.epoch=host.Read().players[hs.self].epoch;crime.crime.position.x+=300;
    Check(rejected(guest,host,crime),"distant crime fabricated against player");
    vc::Message loot;loot.kind=vc::Kind::PickupCreate;loot.pickup.key=900;loot.pickup.model=337;loot.pickup.type=8;loot.pickup.quantity=25;loot.pickup.remaining=30000;loot.pickup.position=pose.position;loot.pickup.source=npcId;loot.pickup.sourceGeneration=2;
    Check(guest.SendEvent(loot),"transferred NPC loot queued");
    Check(until([&]{for(const auto& [id,p]:host.Read().pickups) if(p.source==npcId && p.creator==gs.self) return true;return false;}),"NPC owner loot suppressed by population policy");
    vc::Message seat;seat.kind=vc::Kind::SeatRequest;seat.entity.id=carId;seat.entity.generation=2;seat.id=1;
    Check(host.SendEntity(seat),"passenger reservation queued");
    bool gotSeat=false;
    Check(until([&]{for(auto& event:host.TakeEvents()) if(event.message.kind==vc::Kind::SeatGrant && event.message.id==1) gotSeat=true;return gotSeat;}),"passenger reservation denied");
    vc::BackgroundClient third;Check(third.Connect("127.0.0.1",server.Port(),"Third"),"third player connect queue");
    Check(until([&]{return third.Read().status==vc::Connection::Online;}),"third player handshake");third.Publish(pose);
    Check(until([&]{return host.Read().players[third.Read().self].hasState;}),"third player ready");
    Check(third.SendEntity(seat),"contended seat request queued");bool refused=false;
    Check(until([&]{for(auto& event:third.TakeEvents()) if(event.message.kind==vc::Kind::SeatGrant && event.message.id==255) refused=true;return refused;}),"two players reserved the same seat");
    third.Stop();Check(until([&]{return server.PlayerCount()==2;}),"third player cleanup");
    vc::Entity grenade;grenade.kind=vc::EntityKind::Object;grenade.key=555;grenade.model=259;grenade.object.flags=67;grenade.object.transform.position=pose.position;
    create.entity=grenade;Check(guest.SendEntity(create),"guest projectile create queued");uint32_t projectileId=0;
    Check(until([&]{for(auto& [id,e]:host.Read().entities) if(e.state.key==555) projectileId=id;return projectileId!=0;}),"nearby guest projectile not registered");
    Check(host.Read().entities.at(projectileId).state.owner==gs.self,"projectile authority stolen by population owner");
    vc::Entity prop;prop.kind=vc::EntityKind::Object;prop.key=600;prop.model=343;prop.origin=pose.position;prop.object.transform.position=pose.position;
    create.entity=prop;Check(host.SendEntity(create),"map prop create queued");uint32_t propId=0;
    Check(until([&]{for(auto& [id,e]:host.Read().entities) if(e.state.key==600) propId=id;return propId!=0 && guest.Read().entities.contains(propId);}),"map prop not registered");
    prop.key=601;create.entity=prop;Check(host.SendEntity(create),"duplicate map prop create queued");
    Check(until([&]{return host.Read().entities.contains(propId);}),"canonical map prop disappeared");
    for(int i=0;i<25;++i) {server.Poll();Sleep(2);}
    auto props=host.Read().entities;
    Check(std::count_if(props.begin(),props.end(),[](const auto& item){return item.second.state.kind==vc::EntityKind::Object && item.second.state.model==343;})==1,"same map instance registered twice");
    vc::Message blast;blast.kind=vc::Kind::Explosion;blast.explosion.position=pose.position;blast.explosion.source=projectileId;blast.explosion.generation=1;
    Check(guest.SendEvent(blast),"owned projectile explosion queue");bool gotBlast=false;
    Check(until([&]{for(auto& event:host.TakeEvents()) if(event.message.kind==vc::Kind::Explosion && event.message.explosion.source==projectileId) gotBlast=true;return gotBlast;}),"owned projectile explosion not delivered");

    auto generation=guest.Read().players[hs.self].generation;
    guest.Disconnect();
    Check(until([&]{return server.PlayerCount()==1;}),"disconnect cleanup");
    Check(guest.Connect("127.0.0.1",server.Port(),"GuestAgain"),"reconnect queue");
    Check(until([&]{return guest.Read().status==vc::Connection::Online;}),"reconnect handshake");
    Check(until([&]{return guest.Read().hasEnvironment && guest.Read().environment.weather==2;}),"late join missed environment baseline");
    Check(guest.Read().players[hs.self].generation!=generation,"old session generation reused");
    guest.Stop(); host.Stop();
}
LRESULT CALLBACK ChatProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(vc::ui::Hud().ProcessMessage(h,m,w,l)) return 0;
    return DefWindowProcW(h,m,w,l);
}
void PickupNetworking() {
    vc::Pickup universal;universal.area=13;universal.position={350,-900,11};
    Check(vc::PickupRespawnBlocked(universal,{350,-900,11},0),"everywhere pickup respawned under player");
    Check(!vc::PickupRespawnBlocked(universal,{350,-911,11},0),"distant player blocked pickup respawn");
    universal.area=2;
    Check(!vc::PickupRespawnBlocked(universal,{350,-900,11},0),"other interior blocked pickup respawn");
    Check(vc::PickupRespawnBlocked(universal,{350,-900,11},2),"same interior failed pickup respawn guard");
    vc::NetworkRuntime runtime;Check(runtime.Ok(),"pickup ENet runtime");
    vc::Server server;Check(server.Start(0,"127.0.0.1"),"pickup authority server");vc::BackgroundClient host,guest;
    auto until=[&](auto predicate,unsigned ms=4000){auto end=vc::NowMs()+ms;do{server.Poll();if(predicate())return true;Sleep(2);}while(vc::NowMs()<end);return false;};
    host.Connect("127.0.0.1",server.Port(),"LootHost");Check(until([&]{return host.Read().status==vc::Connection::Online;}),"pickup host connected");
    guest.Connect("127.0.0.1",server.Port(),"LootGuest");Check(until([&]{return guest.Read().status==vc::Connection::Online;}),"pickup guest connected");
    vc::PlayerState pose;pose.flags=vc::kReady;pose.position={350,-900,11};host.Publish(pose);guest.Publish(pose);
    auto hs=host.Read().self,gs=guest.Read().self;
    Check(until([&]{return host.Read().players[gs].hasState && guest.Read().players[hs].hasState;}),"pickup players ready");
    auto spawn=[&](uint32_t key,uint16_t model,uint8_t type,uint32_t amount) {
        vc::Message m;m.kind=vc::Kind::PickupCreate;m.pickup.key=key;m.pickup.area=13;m.pickup.model=model;m.pickup.type=type;m.pickup.quantity=amount;m.pickup.position=pose.position;
        if(type==8) m.pickup.remaining=30000;
        Check(host.SendEvent(m),"pickup create queued");uint32_t id=0;
        Check(until([&]{for(auto& [n,p]:host.Read().pickups) if(p.key==key) id=n;return id && guest.Read().pickups.contains(id);}),"pickup baseline delivered");return id;
    };
    auto claim=[&](vc::BackgroundClient& c,uint32_t id,uint32_t revision,uint8_t mode=0,uint32_t life=1) {
        vc::Message m;m.kind=vc::Kind::PickupRequest;m.pickup.id=id;m.pickup.revision=revision;m.id=mode;m.hit.life=life;Check(c.SendEvent(m),"pickup claim queued");
    };
    auto countAwards=[&](){unsigned count=0;for(auto* c:{&host,&guest}) for(auto& e:c->TakeEvents()) if(e.message.kind==vc::Kind::PickupGrant) ++count;return count;};
    uint32_t cash=spawn(101,337,8,75);claim(host,cash,1);claim(guest,cash,1);
    Check(until([&]{return host.Read().pickups.at(cash).status==2 && guest.Read().pickups.at(cash).status==2;}),"concurrent pickup not committed globally");
    unsigned awards=0;Check(until([&]{awards+=countAwards();return awards>0;}),"pickup recipient not awarded");
    for(int i=0;i<40;++i){server.Poll();Sleep(2);awards+=countAwards();}Check(awards==1,"one pickup rewarded two players");
    claim(host,cash,1);claim(guest,cash,1);for(int i=0;i<40;++i){server.Poll();Sleep(2);}Check(countAwards()==0,"replayed collection awarded twice");
    vc::BackgroundClient late;late.Connect("127.0.0.1",server.Port(),"LateLoot");
    Check(until([&]{return late.Read().pickups.contains(cash) && late.Read().pickups.at(cash).status==2;}),"late join resurrected consumed pickup");late.Stop();
    Check(until([&]{return server.PlayerCount()==2;}),"pickup late peer cleanup");
    uint32_t health=spawn(102,366,2,0);claim(guest,health,1,0,2);
    for(int i=0;i<40;++i){server.Poll();Sleep(2);}Check(host.Read().pickups.at(health).status==0 && countAwards()==0,"old life claimed pickup");
    auto distantPose=pose;distantPose.position.x+=100;guest.Publish(distantPose);Check(until([&]{return host.Read().players[gs].state.position.x==distantPose.position.x;}),"distant looter state");claim(guest,health,1);
    for(int i=0;i<40;++i){server.Poll();Sleep(2);}Check(host.Read().pickups.at(health).status==0 && countAwards()==0,"distant pickup claim accepted");
    guest.Publish(pose);claim(host,health,1);Check(until([&]{return guest.Read().pickups.at(health).status==1;}),"street pickup did not enter shared cooldown");
    Check(guest.Read().pickups.at(health).remaining==30000,"native street respawn delay changed");countAwards();
    uint32_t weapon=spawn(103,274,3,20);claim(guest,weapon,1,1);
    Check(until([&]{return host.Read().pickups.at(weapon).revision==2 && guest.Read().pickups.at(weapon).revision==2;}),"partial ammo not shared");
    Check(host.Read().pickups.at(weapon).status==0 && host.Read().pickups.at(weapon).quantity==0 && (host.Read().pickups.at(weapon).flags&1),"ammo-only collection removed weapon or retained ammo");
    unsigned partial=0;Check(until([&]{partial+=countAwards();return partial>0;}),"ammo collector not awarded");
    claim(guest,weapon,2,1);for(int i=0;i<40;++i){server.Poll();Sleep(2);}Check(countAwards()==0,"empty weapon gave ammo twice");
    vc::Message malformed;malformed.kind=vc::Kind::PickupCreate;malformed.pickup.model=1;malformed.pickup.type=8;vc::Message decoded;
    Check(!vc::Decode(vc::Encode(malformed),decoded),"ped model accepted as inventory pickup");malformed.pickup.model=337;malformed.pickup.type=18;
    Check(!vc::Decode(vc::Encode(malformed),decoded),"property sale accepted as shared loot");
    host.Stop();guest.Stop();
}
void Graphics() {
    WNDCLASSW wc{}; wc.lpfnWndProc=ChatProc; wc.lpszClassName=L"CoopUnicodeRegression"; wc.hInstance=GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    auto window=CreateWindowExW(0,wc.lpszClassName,L"Coop D3D9 regression",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(window!=nullptr,"test window");
    auto* d3d=Direct3DCreate9(D3D_SDK_VERSION); Check(d3d!=nullptr,"D3D9 runtime");
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=window;
    pp.BackBufferWidth=800; pp.BackBufferHeight=600; pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    IDirect3DDevice9* device=nullptr;
    Check(SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)),"D3D9 device");
    device->SetRenderState(D3DRS_FOGENABLE,TRUE); device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);
    device->Clear(0,nullptr,D3DCLEAR_TARGET,0xFF102030,1,0); device->BeginScene();
    Check(IsWindowUnicode(window),"native input must be Unicode");
    auto& g=vc::ui::Gfx(); Check(g.Begin(device),"font atlas on D3D9");
    Check(g.TextWidth(vc::ui::FONT_CHAT,"Привет")>0,"Cyrillic glyphs");
    g.Rect(4,4,30,20,0xFFFF0000); g.Text(vc::ui::FONT_CHAT,50,50,"Привет, Vice City!",0xFFFFFFFF); g.End();
    DWORD fog{},address{}; device->GetRenderState(D3DRS_FOGENABLE,&fog); device->GetSamplerState(0,D3DSAMP_ADDRESSU,&address);
    Check(fog==TRUE && address==D3DTADDRESS_WRAP,"overlay corrupted game render state");
    device->EndScene();
    IDirect3DSurface9 *back=nullptr,*copy=nullptr;
    device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    Check(SUCCEEDED(device->CreateOffscreenPlainSurface(800,600,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr)),"readback surface");
    Check(SUCCEEDED(device->GetRenderTargetData(back,copy)),"D3D9 readback");
    D3DLOCKED_RECT lock{}; Check(SUCCEEDED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)),"readback lock");
    auto pixel=[&](unsigned x,unsigned y){return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lock.pBits)+y*lock.Pitch)[x]&0xFFFFFF;};
    Check(pixel(10,10)==0xFF0000,"overlay rectangle pixels");
    unsigned glyphs=0; for(unsigned y=45;y<90;++y) for(unsigned x=45;x<250;++x) if(pixel(x,y)!=0x102030) ++glyphs;
    Check(glyphs>70,"text did not render pixels");
    copy->UnlockRect(); copy->Release(); back->Release();
    vc::BackgroundClient::Snapshot snapshot; snapshot.status=vc::Connection::Online; snapshot.self=0;
    device->BeginScene();
    vc::ui::Hud().Render(device,snapshot,"Host","127.0.0.1",7781,true,false);
    device->EndScene();
    std::string sent;
    vc::ui::Hud().onAction=[&](const vc::ui::Action& a){if(a.kind==vc::ui::Action::Chat) sent=a.text;};
    struct KeyboardState {BYTE saved[256]{};KeyboardState(){GetKeyboardState(saved);BYTE clear[256]{};SetKeyboardState(clear);}~KeyboardState(){SetKeyboardState(saved);}} keys;
    SendMessageW(window,WM_KEYDOWN,'T',0); SendMessageW(window,WM_CHAR,'t',0);
    for(wchar_t c:std::wstring(L"Привет")) SendMessageW(window,WM_CHAR,c,0);
    SendMessageW(window,WM_KEYDOWN,VK_RETURN,0);
    Check(sent=="Привет","Unicode keyboard input corrupted Cyrillic");
    vc::ui::Hud().Shutdown();
    g.Invalidate(); device->Release(); d3d->Release(); DestroyWindow(window);
}
}
int main() {
    try { Wire(); InteractionWire(); SkeletonPose(); SnapshotPlayback(); NetworkClock(); FixedSimulation(); VehicleMotion(); FrameLimits(); SpawnSearch(); Networking(); PickupNetworking(); Graphics(); std::cout<<checks<<" checks passed (wire, live UDP, Cyrillic D3D9 pixels and render state)\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<"\n"; return 1; }
}
