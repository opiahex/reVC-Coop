#include "common/Interpolation.h"
#include "common/WorldInterpolation.h"
#include "common/VehicleMotion.h"
#include "common/FrameLimit.h"
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
    npc.entity.ped=state.state; npc.entity.ped.model=21; npc.entity.ped.pedType=5; npc.entity.ped.boneMask=63;
    npc.entity.ped.animationCount=vc::kAnimationLayers;
    for(unsigned i=0;i<vc::kAnimationLayers;++i) npc.entity.ped.animations[i]={uint16_t(43+i),uint16_t(i),3,.4f,1,1};
    auto full=vc::Encode(npc);Check(full.size()<=vc::kMaxPacket && vc::Decode(full,decoded),"full NPC animation/bone packet does not fit");
    Check(decoded.entity.ped.animationCount==6 && decoded.entity.ped.boneMask==63 && decoded.entity.ped.pedType==5 && decoded.entity.ped.animations[5].group==48,"NPC animation contents lost");
    npc.entity.ped.bones[0].w=0;Check(!vc::Decode(vc::Encode(npc),decoded),"invalid aim quaternion accepted");
    vc::Message bike;bike.kind=vc::Kind::EntityUpdate;bike.entity.kind=vc::EntityKind::Vehicle;bike.entity.model=191;bike.entity.key=12;bike.entity.vehicle.bikeLean=.34f;
    Check(vc::Decode(vc::Encode(bike),decoded) && decoded.entity.vehicle.bikeLean==.34f,"bike lean lost on wire");
    bike.sampleAgeMs=47;Check(vc::Decode(vc::Encode(bike),decoded) && decoded.sampleAgeMs==47,"vehicle staging age lost on wire");
    bike.sampleAgeMs=251;Check(!vc::Decode(vc::Encode(bike),decoded),"unbounded source sample age accepted");bike.sampleAgeMs=0;
    vc::WorldInterpolator ride;ride.Push(1,bike.entity);bike.entity.vehicle.bikeLean=.74f;ride.Push(2,bike.entity);
    Check(std::abs(ride.At(1.5).vehicle.bikeLean-.54f)<.001f,"bike lean not interpolated");
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

    // Native migration: ownership transfers include the actual NPC driver.
    guest.Publish(pose);
    Check(until([&]{return host.Read().players[gs.self].hasState;}),"guest ready for world authority");
    vc::Entity car; car.key=123; car.kind=vc::EntityKind::Vehicle; car.model=130; car.vehicle.transform.position=pose.position;
    vc::Message create; create.kind=vc::Kind::EntityCreate; create.entity=car;
    Check(host.SendEntity(create),"vehicle create queued");
    uint32_t carId=0,npcId=0;
    Check(until([&]{for(auto& [id,e]:host.Read().entities) if(e.state.key==123) carId=id;return carId!=0;}),"shared vehicle registered");
    vc::Entity npc;npc.key=124;npc.kind=vc::EntityKind::Ped;npc.model=7;npc.ped=pose;npc.ped.model=7;npc.ped.pedType=4;npc.ped.vehicle=carId;npc.ped.seat=0;npc.ped.flags|=vc::kInVehicle;
    create.entity=npc;Check(host.SendEntity(create),"NPC driver create queued");
    Check(until([&]{for(auto& [id,e]:host.Read().entities) if(e.state.key==124) npcId=id;return npcId!=0 && guest.Read().entities.contains(npcId);}),"NPC driver registered");
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
    vc::Message blast;blast.kind=vc::Kind::Explosion;blast.explosion.position=pose.position;blast.explosion.source=projectileId;blast.explosion.generation=1;
    Check(guest.SendEvent(blast),"owned projectile explosion queue");bool gotBlast=false;
    Check(until([&]{for(auto& event:host.TakeEvents()) if(event.message.kind==vc::Kind::Explosion && event.message.explosion.source==projectileId) gotBlast=true;return gotBlast;}),"owned projectile explosion not delivered");

    auto generation=guest.Read().players[hs.self].generation;
    guest.Disconnect();
    Check(until([&]{return server.PlayerCount()==1;}),"disconnect cleanup");
    Check(guest.Connect("127.0.0.1",server.Port(),"GuestAgain"),"reconnect queue");
    Check(until([&]{return guest.Read().status==vc::Connection::Online;}),"reconnect handshake");
    Check(guest.Read().players[hs.self].generation!=generation,"old session generation reused");
    guest.Stop(); host.Stop();
}
LRESULT CALLBACK ChatProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(vc::ui::Hud().ProcessMessage(h,m,w,l)) return 0;
    return DefWindowProcW(h,m,w,l);
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
    SendMessageW(window,WM_KEYDOWN,'T',0); SendMessageW(window,WM_CHAR,'t',0);
    for(wchar_t c:std::wstring(L"Привет")) SendMessageW(window,WM_CHAR,c,0);
    SendMessageW(window,WM_KEYDOWN,VK_RETURN,0);
    Check(sent=="Привет","Unicode keyboard input corrupted Cyrillic");
    vc::ui::Hud().Shutdown();
    g.Invalidate(); device->Release(); d3d->Release(); DestroyWindow(window);
}
}
int main() {
    try { Wire(); VehicleMotion(); FrameLimits(); SpawnSearch(); Networking(); Graphics(); std::cout<<checks<<" checks passed (wire, live UDP, Cyrillic D3D9 pixels and render state)\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<"\n"; return 1; }
}
