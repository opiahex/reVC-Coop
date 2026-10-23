#include "net/BackgroundClient.h"
#include "NativeWorld.h"
#include "common/Interpolation.h"
#include "common/SpawnSearch.h"
#include "common/FrameLimit.h"
#include "ui/Overlay.h"
#include <fstream>
#include <memory>
#include <sstream>

// Source-engine adapter. All calls below execute on reVC's game thread.
#define WITH_D3D
#include "common.h"
#include "Coop.h"
#undef lerp
#include "main.h"
#include "Game.h"
#include "Frontend.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "CivilianPed.h"
#include "Pools.h"
#include "World.h"
#include "Camera.h"
#include "CarCtrl.h"
#include "Population.h"
#include "Particle.h"
#include "SpecialFX.h"
#include "PointLights.h"
#include "DMAudio.h"
#include "soundlist.h"
#include "Object.h"
#include "SurfaceTable.h"
#include "skeleton.h"
#include "Streaming.h"
#include "CutsceneMgr.h"
#include "Clock.h"
#include "Weather.h"
#include "Pad.h"
#include "Radar.h"
#include "Sprite.h"
#include "VisibilityPlugins.h"
#include "ColPoint.h"
#include "Hud.h"
#include "WeaponInfo.h"
#include "TxdStore.h"
#include "ModelInfo.h"
#include "AnimManager.h"
#include "AnimBlendAssocGroup.h"
#include "AnimBlendAssociation.h"
#include "AnimBlendClumpData.h"
#include "RwHelper.h"
#include "Explosion.h"
#include "ProjectileInfo.h"
#include "Automobile.h"
#include "Projectile.h"
#include "RpAnimBlend.h"
#include <d3d9.h>

namespace vccoop {
namespace {
// No additional fields: native CPed pool allocation remains correctly sized.
class CCoopPed final : public CCivilianPed {
public:
    explicit CCoopPed(uint32 model) : CCivilianPed(PEDTYPE_CIVMALE,model) {
        CharCreatedBy=MISSION_CHAR;
        bUsesCollision=false; bAffectedByGravity=false;
        bStreamingDontDelete=true; bIsVisible=true;
        bBulletProof=bCollisionProof=bExplosionProof=bFireProof=bMeleeProof=true;
        SetPedState(PED_IDLE);
        CVisibilityPlugins::SetClumpAlpha(GetClump(),255);
    }
    void ProcessControl() override { bIsInSafePosition=true; CVisibilityPlugins::SetClumpAlpha(GetClump(),255); }
    void ProcessCollision() override { bIsInSafePosition=true; }
    void ProcessShift() override { bIsInSafePosition=true; }
};
static_assert(sizeof(CCoopPed)==sizeof(CCivilianPed),"Native pool slot size");
struct Actor {
    CCoopPed* ped{}; int32 handle{-1},blip{-1};
    uint64_t generation{}; uint32_t sequence{}; vc::Interpolator track;
};
struct App {
    vc::NetworkRuntime runtime; // Must outlive both server and transport worker.
    vc::Server server;
    vc::BackgroundClient network;
    NativeWorld world; uint32_t worldFrame{UINT32_MAX};
    vc::BackgroundClient::Snapshot snapshot;
    std::array<Actor,vc::kMaxPlayers> actors;
    std::string name{"Tommy"},address{"127.0.0.1"};
    uint16_t port{7781};
    bool spawnWaiting{}; uint64_t lastSpawnAttempt{};
    bool sandbox{},autoConnect{},autoHost{},loading{},worldReady{},initialised{},shutdown{};
    uint64_t lastPump{},lastLog{},lastCommand{};
    uint32_t life{1}; float previousHealth{100}; bool applyingDamage{},replayingExplosion{},explosionDamage{};uint64_t explosionsSent{},explosionsReplayed{};
    CEntity* combatSource{}; uint64_t shotsSent{},shotsReplayed{},hitsSent{},hitsApplied{};
    bool captureFrame{};uint64_t testDriveUntil{},testLockUntil{},testCameraUntil{},testTraceUntil{}; int testGas{},testSteer{};
    uint64_t testMoveUntil{},testJumpUntil{},testFireUntil{};int testForward{},testSide{};bool testSprint{};
    uint64_t recordUntil{},recordNext{};unsigned recordInterval{100},recordIndex{};std::string recordPath;std::ofstream recordManifest;
    vc::SessionFrameLimit frameLimit;int onlineFrameLimit{vc::kDefaultOnlineFps};
    std::ofstream vehicleTrace;uint64_t lastVehicleTrace{};unsigned vehicleTraceFrames{},vehicleTracePage{};
    vc::Connection lastStatus{vc::Connection::Offline};
    std::wstring ini;
};
App& State() { static App state; return state; }
void Log(const std::string& s) {
    std::ofstream f("vc-coop-native.log",std::ios::app);
    f<<vc::NowMs()<<" "<<s<<"\n";
}
std::string Setting(App& a,const char* key,const char* fallback) {
    char result[256]{};
    auto path=vc::ui::Narrow(a.ini);
    GetPrivateProfileStringA("native",key,fallback,result,sizeof(result),path.c_str());
    return result;
}
void ClearActor(Actor& a) {
    if(a.blip!=-1) CRadar::ClearBlip(a.blip);
    if(a.ped && CPools::GetPedPool() && CPools::GetPed(a.handle)==a.ped) {
        CWorld::Remove(a.ped); delete a.ped;
    }
    a=Actor{};
}
void Action(const vc::ui::Action& action) {
    auto& a=State();
    if(action.kind==vc::ui::Action::Chat) { a.network.SendChat(action.text); return; }
    if(action.kind==vc::ui::Action::Disconnect) {
        a.network.Disconnect(); a.server.Stop(); return;
    }
    if(a.snapshot.status!=vc::Connection::Offline) return;
    a.name=action.name; a.address=action.address; a.port=action.port;
    if(action.kind==vc::ui::Action::Host && !a.server.Start(a.port)) {
        vc::ui::Hud().Add("Не удалось открыть UDP-порт",0xFFFF81B7); return;
    }
    if(action.kind==vc::ui::Action::Host) a.address="127.0.0.1";
    a.network.Connect(a.address,a.port,a.name);
    Log("connect requested name="+a.name);
}
void Initialise() {
    auto& a=State(); if(a.initialised || a.shutdown) return;
    a.initialised=true; InstallDiagnostics();
    wchar_t path[MAX_PATH]{}; GetFullPathNameW(L"vc-coop.ini",MAX_PATH,path,nullptr); a.ini=path;
    a.name=Setting(a,"name","Tommy"); a.address=Setting(a,"address","127.0.0.1");
    a.port=uint16_t(std::clamp(std::atoi(Setting(a,"port","7781").c_str()),1,65535));
    a.sandbox=Setting(a,"sandbox","0")=="1";
    a.autoHost=Setting(a,"host","0")=="1";
    a.autoConnect=Setting(a,"auto_connect","0")=="1";
    a.onlineFrameLimit=vc::ParseFrameLimit(Setting(a,"fps_limit","60"));
    a.server.log=[](const std::string& s){Log("server "+s);};
    vc::ui::Hud().Configure(a.ini); vc::ui::Hud().onAction=Action;
    a.network.Start();
    a.world.log=Log;
    Log("VC:COOP reVC 0.2.2 source adapter started protocol=15");
}
void Pump() {
    Initialise(); auto& a=State(); if(a.shutdown) return;
    auto now=vc::NowMs(); if(now==a.lastPump) return; a.lastPump=now;
    a.server.Poll(); a.snapshot=a.network.Read();
    if(a.snapshot.status!=a.lastStatus) {
        Log("connection="+std::to_string(int(a.snapshot.status))+" self="+std::to_string(a.snapshot.self)+" host="+std::to_string(a.snapshot.host)+" "+a.snapshot.error);
        a.lastStatus=a.snapshot.status;
        if(a.lastStatus==vc::Connection::Offline) {
            a.world.Clear();
            for(auto& actor:a.actors) ClearActor(actor);
            a.worldReady=false;
        }
    }
    auto limit=a.frameLimit.Update(Active(),a.onlineFrameLimit,{FrontEndMenuManager.m_PrefsFrameLimiter!=0,RsGlobal.maxFPS});
    FrontEndMenuManager.m_PrefsFrameLimiter=limit.enabled;RsGlobal.maxFPS=limit.fps;
    if(a.autoConnect && FrontEndMenuManager.m_bMenuActive && FrontEndMenuManager.m_bGameNotLoaded) {
        a.autoConnect=false;
        vc::ui::Action request{}; request.kind=a.autoHost?vc::ui::Action::Host:vc::ui::Action::Join;
        request.name=a.name; request.address=a.address; request.port=a.port; Action(request);
    }
}
vc::PlayerState Capture(CPed* p) {
    vc::PlayerState s;
    auto pos=p->GetPosition(),vel=p->GetMoveSpeed();
    s.position={pos.x,pos.y,pos.z}; s.velocity={vel.x*50,vel.y*50,vel.z*50};
    if(p==FindPlayerPed()) s.life=State().life;
    s.heading=p->m_fRotationCur; s.health=p->m_fHealth; s.armour=p->m_fArmour;
    s.pedType=uint8_t(p->m_nPedType); s.model=p->GetModelIndex(); s.area=p->m_area; s.move=p->m_nMoveState;
    s.pedState=p->GetPedState(); s.flags=vc::kReady;
    if(p->DyingOrDead()) s.flags|=vc::kDead;
    s.hour=CClock::GetHours(); s.minute=CClock::GetMinutes(); s.second=uint8(std::clamp(int(CClock::GetSeconds()),0,59));
    s.weather=uint8(std::clamp(int(CWeather::NewWeatherType),0,6));
    s.oldWeather=uint8(std::clamp(int(CWeather::OldWeatherType),0,6));
    auto* w=p->GetWeapon(); s.weapon=uint8(w->m_eWeaponType); s.weaponState=uint8(w->m_eWeaponState);
    s.ammo=uint32(std::max(w->m_nAmmoTotal,0)); s.clip=uint32(std::max(w->m_nAmmoInClip,0));
    if(p->GetClump()) {
        std::vector<vc::Animation> list;
        for(auto* n=RpAnimBlendClumpGetFirstAssociation(p->GetClump());n;n=RpAnimBlendGetNextAssociation(n)) {
            if(n->groupId<0 || n->groupId>=NUM_ANIM_ASSOC_GROUPS || n->animId<0 || n->animId>=512 || n->blendAmount<=.001f) continue;
            list.push_back({uint16(n->groupId),uint16(n->animId),uint16(n->flags&0x3FF3),std::max(n->currentTime,0.f),std::clamp(n->speed,0.f,10.f),std::clamp(n->blendAmount,0.f,1.f)});
        }
        std::stable_sort(list.begin(),list.end(),[](auto& x,auto& y){return x.blend>y.blend;});
        for(auto& n:list) {
            bool duplicate=false;
            for(unsigned i=0;i<s.animationCount;++i) if(s.animations[i].group==n.group && s.animations[i].id==n.id) duplicate=true;
            if(!duplicate && s.animationCount<vc::kAnimationLayers) s.animations[s.animationCount++]=n;
        }
    }
    // Only procedural aiming needs bone overrides. Offscreen idle NPCs may retain bind-pose quaternions.
    if((p->bIsAimingGun || p->bIsPointingGunAt) && p->GetClump() && IsClumpSkinned(p->GetClump())) for(size_t i=0;i<vc::kAimBoneTags.size();++i) {
        auto* frame=RpAnimBlendClumpFindBone(p->GetClump(),vc::kAimBoneTags[i]);
        if(!frame || !frame->hanimFrame) continue;
        auto q=frame->hanimFrame->q;float length=std::sqrt(q.imag.x*q.imag.x+q.imag.y*q.imag.y+q.imag.z*q.imag.z+q.real*q.real);
        if(!std::isfinite(length) || length<.01f) continue;
        s.boneMask|=uint8_t(1u<<i);s.bones[i]={q.imag.x/length,q.imag.y/length,q.imag.z/length,q.real/length};
    }
    return s;
}
void Animations(CPed* p,const vc::PlayerState& s) {
    if(s.weapon<WEAPONTYPE_TOTALWEAPONS) {
        auto* info=CWeaponInfo::GetWeaponInfo(eWeaponType(s.weapon));
        bool loaded=info->m_nModelId<=0 || CStreaming::HasModelLoaded(info->m_nModelId);
        if(!loaded) CStreaming::RequestModel(info->m_nModelId,STREAMFLAGS_DONT_REMOVE);
        else {
            if(p->GetWeapon()->m_eWeaponType!=s.weapon) {p->GiveWeapon(eWeaponType(s.weapon),s.ammo);p->SetCurrentWeapon(eWeaponType(s.weapon));}
            p->GetWeapon()->m_nAmmoTotal=s.ammo;p->GetWeapon()->m_nAmmoInClip=s.clip;
        }
    }
    auto* clump=p->GetClump(); if(!clump) return;
    for(auto* n=RpAnimBlendClumpGetFirstAssociation(clump);n;n=RpAnimBlendGetNextAssociation(n)) {
        bool keep=false;
        for(unsigned j=0;j<s.animationCount;++j) if(n->groupId==s.animations[j].group && n->animId==s.animations[j].id) keep=true;
        if(!keep) { n->blendDelta=-8; n->flags|=ASSOC_DELETEFADEDOUT; n->callback=nullptr; n->callbackType=CAnimBlendAssociation::CB_NONE; }
    }
    for(unsigned j=0;j<s.animationCount;++j) {
        const auto& anim=s.animations[j];
        if(anim.group>=NUM_ANIM_ASSOC_GROUPS) continue;
        auto& group=CAnimManager::GetAnimAssocGroups()[anim.group];
        if(!group.assocList || anim.id<group.firstAnimId || anim.id>=group.firstAnimId+group.numAssociations) continue;
        CAnimBlendAssociation* n=nullptr;
        for(auto* candidate=RpAnimBlendClumpGetFirstAssociation(clump);candidate;candidate=RpAnimBlendGetNextAssociation(candidate))
            if(candidate->groupId==anim.group && candidate->animId==anim.id) { n=candidate; break; }
        if(!n) n=CAnimManager::AddAnimation(clump,AssocGroupId(anim.group),AnimationId(anim.id));
        if(!n || !n->hierarchy || n->hierarchy->totalLength<=0) continue;
        n->callback=nullptr; n->callbackType=CAnimBlendAssociation::CB_NONE;
        n->blendAmount=anim.blend; n->blendDelta=0; n->speed=anim.speed;
        n->flags=anim.flags & ~(ASSOC_DELETEFADEDOUT|ASSOC_FADEOUTWHENDONE|ASSOC_HAS_TRANSLATION|ASSOC_HAS_X_TRANSLATION);
        float time=anim.time;
        if(n->flags&ASSOC_REPEAT) time=std::fmod(time,n->hierarchy->totalLength);
        n->SetCurrentTime(std::clamp(time,0.f,n->hierarchy->totalLength));
    }
}
bool SpawnLocation(CPlayerPed* p,CVector anchor,float heading,CVector& output) {
    auto& a=State();
    std::vector<vc::Vec3> occupied;
    for(const auto& other:a.snapshot.players) if(other.active && other.hasState && (other.state.flags&vc::kReady)) occupied.push_back(other.state.position);
    auto result=vc::FindNearbySpawn({anchor.x,anchor.y,anchor.z},heading,occupied,[&](vc::Vec3 candidate,vc::Vec3& grounded) {
        CColPoint hit; CEntity* entity=nullptr;
        if(!CWorld::ProcessVerticalLine(CVector(candidate.x,candidate.y,anchor.z+1.f),anchor.z-3.f,hit,entity,true,false,false,true,false,false,nullptr) || hit.normal.z<.6f) return false;
        grounded={candidate.x,candidate.y,hit.point.z+FEET_OFFSET+.04f};
        CVector position(grounded.x,grounded.y,grounded.z);
        for(float height:{-.45f,0.f,.55f})
            if(CWorld::TestSphereAgainstWorld(position+CVector(0,0,height),.38f,p,true,true,false,true,false,false)) return false;
        // Prevent a candidate behind a wall from selecting another closed room.
        if(!CWorld::GetIsLineOfSightClear(anchor,position,true,false,false,true,false,false,false)) return false;
        return true;
    });
    if(!result) return false;
    output=CVector(result->x,result->y,result->z); return true;
}
void PrepareSandbox(CPlayerPed* p) {
    auto& a=State();
    if(a.spawnWaiting && vc::NowMs()-a.lastSpawnAttempt<1000) return;
    a.lastSpawnAttempt=vc::NowMs();
    if(CCutsceneMgr::HasLoaded() || CCutsceneMgr::IsCutsceneProcessing()) CCutsceneMgr::DeleteCutsceneData();
    CVector anchor(119.f,-826.f,10.46f),position; float heading=0;
    if(a.snapshot.self!=a.snapshot.host && a.snapshot.host<vc::kMaxPlayers) {
        const auto& host=a.snapshot.players[a.snapshot.host];
        if(host.hasState && (host.state.flags&vc::kReady)) { anchor=CVector(host.state.position.x,host.state.position.y,host.state.position.z); heading=host.state.heading; }
    }
    CGame::currArea=AREA_MAIN_MAP; p->m_area=AREA_MAIN_MAP;
    CStreaming::RemoveBuildingsNotInArea(AREA_MAIN_MAP);
    CStreaming::LoadSceneCollision(anchor); CStreaming::LoadScene(anchor);
    if(!SpawnLocation(p,anchor,heading,position)) {
        if(!a.spawnWaiting) { Log("waiting for safe spawn near host"); vc::ui::Hud().Add("Рядом с хостом нет свободного места. Ожидаем безопасную точку."); }
        a.spawnWaiting=true;
        CWorld::Players[CWorld::PlayerInFocus].MakePlayerSafe(true);
        TheCamera.RestoreWithJumpCut(); TheCamera.SetWideScreenOff(); TheCamera.Fade(0,FADE_IN);
        return;
    }
    a.spawnWaiting=false;
    p->Teleport(position); p->SetPedState(PED_IDLE); p->SetMoveState(PEDMOVE_STILL);
    p->bIsVisible=true; p->bUsesCollision=true; p->bAffectedByGravity=true;
    CWorld::Players[CWorld::PlayerInFocus].MakePlayerSafe(false);
    CPad::GetPad(0)->SetEnablePlayerControls(PLAYERCONTROL_CAMERA|PLAYERCONTROL_CUTSCENE|PLAYERCONTROL_PHONE);
    TheCamera.SetWideScreenOff(); TheCamera.RestoreWithJumpCut(); TheCamera.Fade(0,FADE_IN);
    CTimer::EndUserPause();
    CCarCtrl::CarDensityMultiplier=1.f;CPopulation::PedDensityMultiplier=1.f;
    CWorld::Players[CWorld::PlayerInFocus].m_fRoadDensity=1.f;
    p->GiveWeapon(WEAPONTYPE_COLT45,200);p->SetCurrentWeapon(WEAPONTYPE_COLT45);p->m_fArmour=100;
    a.worldReady=true; a.loading=false;
    Log("sandbox ready position="+std::to_string(position.x)+","+std::to_string(position.y)+","+std::to_string(position.z));
}
void ApplyRemote(unsigned id) {
    auto& a=State(); auto& actor=a.actors[id]; const auto& remote=a.snapshot.players[id];
    if(id==a.snapshot.self || !remote.active || !remote.hasState || !(remote.state.flags&vc::kReady) || vc::NowMs()-remote.received>3000) {
        if(actor.ped) ClearActor(actor); return;
    }
    if(actor.generation!=remote.generation || (actor.ped && CPools::GetPed(actor.handle)!=actor.ped)) ClearActor(actor);
    if(actor.sequence!=remote.sequence || actor.track.Empty()) {
        actor.track.Push(double(remote.received)*.001,remote.state); actor.sequence=remote.sequence;
    }
    auto s=actor.track.At(double(vc::NowMs())*.001-.1);
    if(s.model>129 || s.area!=CGame::currArea) { if(actor.ped) ClearActor(actor); return; }
    if(!actor.ped) {
        CStreaming::RequestModel(s.model,STREAMFLAGS_DONT_REMOVE);
        if(!CStreaming::HasModelLoaded(s.model)) return;
        actor.ped=new CCoopPed(s.model);
        if(!actor.ped) return;
        actor.ped->SetPosition(CVector(s.position.x,s.position.y,s.position.z));
        actor.ped->m_area=s.area; CWorld::Add(actor.ped);
        actor.handle=CPools::GetPedRef(actor.ped); actor.generation=remote.generation;
        actor.blip=CRadar::SetEntityBlip(BLIP_CHAR,actor.handle,id==0?RADAR_TRACE_MAGENTA:RADAR_TRACE_CYAN,BLIP_DISPLAY_BLIP_ONLY);
        CRadar::ChangeBlipScale(actor.blip,2);
        CRadar::ChangeBlipColour(actor.blip,id==0?RADAR_TRACE_MAGENTA:RADAR_TRACE_CYAN);
        Log("remote created id="+std::to_string(id)+" handle="+std::to_string(actor.handle));
    }
    auto* p=actor.ped;
    // Teleport updates sector membership through the engine API.
    p->Teleport(CVector(s.position.x,s.position.y,s.position.z));
    p->SetHeading(s.heading); p->m_fRotationCur=p->m_fRotationDest=s.heading;
    p->m_fHealth=s.health; p->m_fArmour=s.armour; p->m_nMoveState=eMoveState(s.move);
    // CEntity::UpdateRwFrame only updates frame objects; it does NOT copy CMatrix.
    // Native physics normally commits this matrix, but replicas intentionally bypass physics.
    p->GetMatrix().UpdateRW(); p->UpdateRwFrame(); Animations(p,s);
    RpAnimBlendClumpUpdateAnimations(p->GetClump(),0.f,true);p->UpdateRpHAnim();
}
void* WeaponIcon(unsigned type) {
    if(type>=WEAPONTYPE_TOTALWEAPONS) return nullptr;
    auto* info=CWeaponInfo::GetWeaponInfo(eWeaponType(type)); RwTexture* icon=nullptr;
    if(info->m_nModelId<=0) icon=CHud::Sprites[type].m_pTexture;
    else {
        auto* model=CModelInfo::GetModelInfo(info->m_nModelId);
        if(model) {
            auto* dict=CTxdStore::GetSlot(model->GetTxdSlot())->texDict;
            if(dict) icon=RwTexDictionaryFindNamedTexture(dict,model->GetModelName());
        }
    }
    return icon && icon->raster ? GETD3DRASTEREXT(icon->raster)->texture : nullptr;
}

void ReplayShot(const vc::Message& m) {
    auto& a=State();const auto& shot=m.shot;
    CEntity* shooter=shot.source?a.world.Pointer(shot.source):(m.sender<vc::kMaxPlayers?a.actors[m.sender].ped:nullptr);
    if(!shooter) return;
    if(shot.source && a.world.Generation(shot.source)!=shot.sourceGeneration) return;
    if(!shot.source && a.snapshot.players[m.sender].state.life!=shot.sourceLife) return;
    CVector start=shooter->GetMatrix()*CVector(shot.localMuzzle.x,shot.localMuzzle.y,shot.localMuzzle.z),end(shot.end.x,shot.end.y,shot.end.z);
    CVector forward=end-start;if(forward.MagnitudeSqr()<.0001f) return;forward.Normalise();
    if(!(shot.flags&2)) {
    if(shot.weapon==WEAPONTYPE_M4 || shot.weapon==WEAPONTYPE_RUGER || shot.weapon==WEAPONTYPE_M60 || shot.weapon==WEAPONTYPE_MINIGUN)
        CWeapon::AddGunFlashBigGuns(start,end);
    else {
        const float sizes[]={.06f,.04f,.02f};CVector flash=start;
        for(float size:sizes) {CParticle::AddParticle(PARTICLE_GUNFLASH_NOANIM,flash,CVector(0,0,0),nullptr,size);flash+=forward*.05f;}
        CPointLights::AddLight(CPointLights::LIGHT_POINT,start,CVector(0,0,0),5.f,1.f,.8f,0.f,CPointLights::FOG_NONE,false);
    }
    CParticle::AddParticle(PARTICLE_GUNSMOKE2,start,forward*.12f,nullptr,.015f);
    }
    if(!(shot.flags&4)) CBulletTraces::AddTrace(&start,&end,int(shot.weapon),shooter);
    if(!(shot.flags&2) && shooter->IsPed()) DMAudio.PlayOneShot(static_cast<CPed*>(shooter)->m_audioEntityId,SOUND_WEAPON_SHOT_FIRED,0.f);
    if(shot.flags&1) {
        CVector normal(shot.normal.x,shot.normal.y,shot.normal.z);
        if(shot.impactKind==2) {
            for(unsigned i=0;i<4;++i) CParticle::AddParticle(PARTICLE_BLOOD,end,normal*.06f+forward*.03f);
            CParticle::AddParticle(PARTICLE_BLOOD_SMALL,end,normal*.03f);
        } else {
            for(unsigned i=0;i<4;++i) CParticle::AddParticle(PARTICLE_SPARK,end,normal*.12f);
            CParticle::AddParticle(PARTICLE_SMOKE,end,normal*.015f,nullptr,.04f);
        }
    }
    ++a.shotsReplayed;
}
void ApplyCombatEvent(const vc::Message& m) {
    auto& a=State();if(a.world.Event(m)) return;
    if(m.kind==vc::Kind::Shot) {ReplayShot(m);return;}
    if(m.kind==vc::Kind::Explosion) {
        auto pos=m.explosion.position;if(pos.x<=WORLD_MIN_X || pos.x>=WORLD_MAX_X || pos.y<=WORLD_MIN_Y || pos.y>=WORLD_MAX_Y) return;
        CEntity* culprit=m.explosion.source?a.world.Pointer(m.explosion.source):m.sender<vc::kMaxPlayers?a.actors[m.sender].ped:nullptr;
        a.replayingExplosion=true;CExplosion::AddExplosion(nullptr,culprit,eExplosionType(m.explosion.type),CVector(pos.x,pos.y,pos.z),m.explosion.delay,m.explosion.sound!=0);a.replayingExplosion=false;++a.explosionsReplayed;return;
    }
    if(m.kind!=vc::Kind::Hit || m.hit.target!=a.snapshot.self || m.hit.targetEpoch!=a.snapshot.players[a.snapshot.self].epoch || m.hit.life!=a.life) return;
    auto* p=FindPlayerPed();if(!p || p->m_fHealth<=0) return;
    CEntity* attacker=m.hit.source?a.world.Pointer(m.hit.source):m.sender<vc::kMaxPlayers?a.actors[m.sender].ped:nullptr;
    a.applyingDamage=true;
    p->InflictDamage(attacker,eWeaponType(m.hit.weapon),m.hit.damage,ePedPieceTypes(m.hit.piece),m.hit.direction);
    a.applyingDamage=false;++a.hitsApplied;
}
void TraceVehicles() {
    auto& a=State();auto now=vc::NowMs();if(!a.sandbox || now-a.lastVehicleTrace<100) return;a.lastVehicleTrace=now;
    // Two bounded, per-process pages retain roughly the last 20 minutes, even
    // if the test windows close before the player reports a divergence.
    if(!a.vehicleTrace.is_open() || a.vehicleTraceFrames>=6000) {
        a.vehicleTrace.close();
        auto path="vc-coop-vehicles-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(a.vehicleTracePage++%2)+".csv";
        a.vehicleTrace.open(path,std::ios::trunc);a.vehicleTraceFrames=0;
        a.vehicleTrace<<"tick,self,id,owner,generation,sequence,sourceAgeMs,x,y,z,renderX,renderY,renderZ,vx,vy,vz,health\n";
    }
    a.world.TraceVehicles(a.vehicleTrace,now);a.vehicleTrace.flush();++a.vehicleTraceFrames;
}
void Diagnostics(CPlayerPed* p) {
    auto& a=State(); auto now=vc::NowMs(); if(now-a.lastLog<(a.sandbox && now<a.testTraceUntil?30:250)) return; a.lastLog=now;
    auto s=Capture(p);
    std::ofstream f("vc-coop-state.json",std::ios::trunc);
    f<<"{\"tick\":"<<now<<",\"pid\":"<<GetCurrentProcessId()<<",\"commandSerial\":"<<a.lastCommand<<",\"build\":\"reVC 0.2.2\",\"self\":"<<unsigned(a.snapshot.self)<<",\"online\":"<<Active()<<",\"ready\":"<<a.worldReady<<",\"sandbox\":"<<a.sandbox
     <<",\"fpsLimit\":"<<RsGlobal.maxFPS<<",\"limiterEnabled\":"<<unsigned(FrontEndMenuManager.m_PrefsFrameLimiter)<<",\"validState\":"<<vc::ValidState(s)<<",\"frameStep\":"<<CTimer::GetTimeStep()<<",\"pedState\":"<<unsigned(s.pedState)<<",\"move\":"<<unsigned(s.move)<<",\"weapon\":"<<unsigned(s.weapon)<<",\"ammo\":"<<s.ammo<<",\"position\":["<<s.position.x<<","<<s.position.y<<","<<s.position.z<<"],\"animations\":"<<unsigned(s.animationCount)<<",\"remotes\":[";
    bool comma=false;
    for(unsigned i=0;i<vc::kMaxPlayers;++i) if(a.actors[i].ped) {
        if(comma) f<<","; comma=true; auto pos=a.actors[i].ped->GetPosition();
        auto rendered=RwFrameGetMatrix(RpClumpGetFrame(a.actors[i].ped->GetClump()))->pos;
        f<<"{\"id\":"<<i<<",\"alpha\":"<<CVisibilityPlugins::GetClumpAlpha(a.actors[i].ped->GetClump())<<",\"visible\":"<<a.actors[i].ped->bIsVisible<<",\"renderPosition\":["<<rendered.x<<","<<rendered.y<<","<<rendered.z<<"],\"position\":["<<pos.x<<","<<pos.y<<","<<pos.z<<"]}";
    }
    f<<"]"; a.world.Diagnostics(f);
    f<<",\"life\":"<<a.life<<",\"health\":"<<p->m_fHealth<<",\"armour\":"<<p->m_fArmour<<",\"inVehicle\":"<<p->bInVehicle<<",\"vehicle\":"<<a.world.Id(p->m_pMyVehicle)<<",\"shotsSent\":"<<a.shotsSent<<",\"shotsReplayed\":"<<a.shotsReplayed<<",\"hitsSent\":"<<a.hitsSent<<",\"hitsApplied\":"<<a.hitsApplied<<",\"explosionsSent\":"<<a.explosionsSent<<",\"explosionsReplayed\":"<<a.explosionsReplayed<<"}\n";
}
// Explicit opt-in development commands run through engine APIs, never memory writes.
void TestCommands(CPlayerPed* p) {
    auto& a=State(); if(!a.sandbox) return;
    std::ifstream f("vc-coop-test-command.txt"); uint64_t serial{}; std::string command;
    if(!(f>>serial>>command) || serial<=a.lastCommand) return; a.lastCommand=serial;
    if(command=="capture") a.captureFrame=true;
    else if(command=="inspect") {unsigned id;if(f>>id) if(auto* e=a.world.Pointer(id)) {
        auto target=e->GetPosition()+CVector(0,0,.7f);TheCamera.SetCamPositionForFixedMode(target+CVector(3,-4,1.3f),CVector(0,0,0));TheCamera.TakeControlNoEntity(target,JUMP_CUT,CAMCONTROL_SCRIPT);a.testCameraUntil=vc::NowMs()+8000;
    }}
    else if(command=="record") {
        std::string tag;unsigned ms,fps;
        if(f>>tag>>ms>>fps && !tag.empty() && tag.size()<40 && tag.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")==std::string::npos) {
            CreateDirectoryA("vc-coop-captures",nullptr);a.recordPath="vc-coop-captures/"+tag;CreateDirectoryA(a.recordPath.c_str(),nullptr);
            a.recordManifest.close();a.recordManifest.open(a.recordPath+"/frames.csv",std::ios::trunc);a.recordManifest<<"tick,file\n";
            a.recordUntil=vc::NowMs()+std::clamp(ms,100u,12000u);a.recordNext=0;a.recordIndex=0;a.recordInterval=1000/std::clamp(fps,5u,30u);
        }
    }
    else if(command=="camera") {
        float x,y,z,tx,ty,tz;unsigned ms;
        if(f>>x>>y>>z>>tx>>ty>>tz>>ms && vc::ValidPoint({x,y,z}) && vc::ValidPoint({tx,ty,tz})) {
            TheCamera.SetCamPositionForFixedMode(CVector(x,y,z),CVector(0,0,0));TheCamera.TakeControlNoEntity(CVector(tx,ty,tz),JUMP_CUT,CAMCONTROL_SCRIPT);a.testCameraUntil=vc::NowMs()+std::clamp(ms,100u,30000u);
        }
    }
    else if(command=="cameraoff") {TheCamera.RestoreWithJumpCut();a.testCameraUntil=0;}
    else if(command=="presentation") {unsigned clean;if(f>>clean) CHud::m_Wants_To_Draw_Hud=clean==0;}
    else if(command=="clearchat") vc::ui::Hud().ClearMessages();
    else if(command=="environment") {unsigned hour,minute,weather;if(f>>hour>>minute>>weather && hour<24 && minute<60 && weather<6 && a.snapshot.self==a.snapshot.host) {CClock::SetGameClock(hour,minute);CWeather::ForceWeatherNow(weather);}}
    else if(command=="scene") {float x,y,z,heading;if(f>>x>>y>>z>>heading && vc::ValidPoint({x,y,z}) && !p->bInVehicle) {CStreaming::LoadScene(CVector(x,y,z));CStreaming::LoadSceneCollision(CVector(x,y,z));bool ground=false;float height=CWorld::FindGroundZFor3DCoord(x,y,z+20,&ground);if(ground) z=height+1;p->Teleport(CVector(x,y,z));p->SetHeading(heading);p->m_fRotationCur=p->m_fRotationDest=heading;}}
    else if(command=="move") {unsigned ms,sprint;int forward,side;if(f>>ms>>forward>>side>>sprint) {a.testMoveUntil=vc::NowMs()+std::min(ms,10000u);a.testForward=std::clamp(forward,-128,128);a.testSide=std::clamp(side,-128,128);a.testSprint=sprint!=0;}}
    else if(command=="jump") a.testJumpUntil=vc::NowMs()+100;
    else if(command=="fire") {unsigned ms;if(f>>ms) a.testFireUntil=vc::NowMs()+std::min(ms,10000u);}
    else if(command=="trace") {unsigned ms;if(f>>ms) a.testTraceUntil=vc::NowMs()+std::min(ms,30000u);}
    else if(command=="vehiclepose") {unsigned id;float x,y,z,heading;if(f>>id>>x>>y>>z>>heading) a.world.TestVehiclePose(id,x,y,z,heading);}
    else if(command=="testlock") {unsigned ms;if(f>>ms) a.testLockUntil=vc::NowMs()+std::min(ms,30000u);}
    else if(command=="position") { float x,y,z; if(f>>x>>y>>z && vc::ValidPoint({x,y,z}) && std::abs(z)<1000) p->Teleport(CVector(x,y,z)); }
    else if(command=="chat") { std::string text; std::getline(f>>std::ws,text); a.network.SendChat(text); }
    else if(command=="drive") {int ms,gas,steer;if(f>>ms>>gas>>steer) {a.testDriveUntil=vc::NowMs()+std::clamp(ms,0,10000);a.testGas=std::clamp(gas,-255,255);a.testSteer=std::clamp(steer,-128,128);}}
    else if(command=="weapon") {int type;if(f>>type && type>=0 && type<WEAPONTYPE_TOTALWEAPONS) {p->GiveWeapon(eWeaponType(type),200);p->SetCurrentWeapon(eWeaponType(type));}}
    else if(command=="hit") {unsigned target;float damage;if(f>>target>>damage && target<vc::kMaxPlayers && a.actors[target].ped) a.actors[target].ped->InflictDamage(p,WEAPONTYPE_COLT45,std::clamp(damage,1.f,1000.f),PEDPIECE_TORSO,0);}
    else if(command=="dent") {unsigned id,door,status;if(f>>id>>door>>status && door<6 && status<4) {auto* e=a.world.Pointer(id);if(e && e->IsVehicle() && static_cast<CVehicle*>(e)->IsCar() && a.world.Owns(e)) {auto* car=static_cast<CAutomobile*>(e);car->Damage.SetDoorStatus(door,status);car->SetupDamageAfterLoad();}}}
    else if(command=="windscreen") {unsigned id;if(f>>id) if(auto* e=a.world.Pointer(id);e && e->IsVehicle() && static_cast<CVehicle*>(e)->IsCar() && a.world.Owns(e)) {
        auto* car=static_cast<CAutomobile*>(e);car->Damage.ApplyDamage(COMPONENT_PANEL_WINDSCREEN,400.f,1.f);car->SetupDamageAfterLoad();
    }}
    else if(command=="shoot") {unsigned target;if(f>>target && target<vc::kMaxPlayers && a.actors[target].ped) {
        CEntity* victim=a.actors[target].ped;CVector start=p->GetPosition()+CVector(0,0,.5f),end=victim->GetPosition()+CVector(0,0,.4f);CColPoint point{};point.point=end;point.normal=start-end;point.normal.Normalise();point.surfaceB=SURFACE_PED;point.pieceB=PEDPIECE_TORSO;
        p->GetWeapon()->DoBulletImpact(p,victim,&start,&end,&point,CVector2D(0,1));}}
    else if(command=="explode") {int type;float x,y,z;if(f>>type>>x>>y>>z && type>=0 && type<=11) CExplosion::AddExplosion(nullptr,p,eExplosionType(type),CVector(x,y,z),0);}
    else if(command=="projectile") {int type;if(f>>type && type>=12 && type<=16) CProjectileInfo::AddProjectile(p,eWeaponType(type),p->GetPosition()+CVector(0,0,.5f),.5f);}
    else if(command=="entityhit") {unsigned id;float damage;if(f>>id>>damage) if(auto* e=a.world.Pointer(id)) {
        CombatScope scope(p);if(e->IsPed()) static_cast<CPed*>(e)->InflictDamage(p,WEAPONTYPE_COLT45,damage,PEDPIECE_TORSO,0);
        else if(e->IsVehicle()) static_cast<CVehicle*>(e)->InflictDamage(p,WEAPONTYPE_COLT45,damage,e->GetPosition());
        else static_cast<CObject*>(e)->ObjectDamage(damage);
    }}
    else if(command=="objectdamage") {unsigned id;float damage;if(f>>id>>damage) {auto* e=a.world.Pointer(id);if(e && e->IsObject()) {CombatScope scope(p);static_cast<CObject*>(e)->ObjectDamage(damage);}}}
    else if(command=="vehicle") {int model;float x,y,z;if(f>>model>>x>>y>>z) a.world.SpawnTestVehicle(model,x,y,z);}
    else if(command=="enter") {unsigned id,seat,warp;if(f>>id>>seat>>warp) a.world.TestEnter(id,seat,warp!=0);}
    else if(command=="exit") {if(p->bInVehicle && p->m_pMyVehicle) p->SetExitCar(p->m_pMyVehicle,p->m_vehDoor);}
    else if(command=="disconnect") a.network.Disconnect();
    Log("test command="+command);
}
}
bool Active() { return State().initialised && !State().shutdown && State().snapshot.status==vc::Connection::Online; }
bool Sandbox() { return Active() && State().sandbox && FindPlayerPed()!=nullptr; }
void FrontendTick() {
    Pump(); auto& a=State();
    if(!Active() || !a.sandbox || a.loading || !FrontEndMenuManager.m_bGameNotLoaded) return;
    if(a.snapshot.self!=a.snapshot.host) {
        if(a.snapshot.host>=vc::kMaxPlayers) return;
        const auto& host=a.snapshot.players[a.snapshot.host];
        if(!host.hasState || !(host.state.flags&vc::kReady)) return;
    }
    a.loading=true;
    FrontEndMenuManager.m_bWantToLoad=false;
    FrontEndMenuManager.DoSettingsBeforeStartingAGame();
    Log("native world load requested");
}
void UpdateWorld() {
    auto& a=State(); if(!a.worldReady || a.worldFrame==CTimer::GetFrameCounter()) return;
    std::array<CPed*,vc::kMaxPlayers> players{};
    for(unsigned i=0;i<vc::kMaxPlayers;++i) players[i]=i==a.snapshot.self?static_cast<CPed*>(FindPlayerPed()):static_cast<CPed*>(a.actors[i].ped);
    a.world.Begin(a.snapshot,a.network,players,Capture,Animations);a.worldFrame=CTimer::GetFrameCounter();
}
void BeforeProcess() {
    Pump();
    auto window=GetActiveWindow();
    if(!window || GetForegroundWindow()!=window) { CPad::GetPad(0)->Clear(false); CPad::NewMouseControllerState.Clear(); }
    if(State().testCameraUntil && vc::NowMs()>State().testCameraUntil) {TheCamera.RestoreWithJumpCut();State().testCameraUntil=0;}
    if(Active()) UpdateWorld();
    if(Active() && (vc::ui::Hud().Capturing() || (State().sandbox && vc::NowMs()<State().testLockUntil))) CPad::GetPad(0)->Clear(false);
    auto& a=State();
    if(a.sandbox && vc::NowMs()<a.testMoveUntil) {
        auto* pad=CPad::GetPad(0);pad->NewState.LeftStickY=a.testForward;pad->NewState.LeftStickX=a.testSide;pad->NewState.Cross=a.testSprint?255:0;
    }
    if(a.sandbox && vc::NowMs()<a.testFireUntil) {auto* pad=CPad::GetPad(0);pad->NewState.Circle=255;pad->NewState.RightShoulder1=255;}
    if(a.sandbox && vc::NowMs()<a.testJumpUntil) CPad::GetPad(0)->NewState.Square=255;
    if(a.sandbox && vc::NowMs()<a.testDriveUntil) {
        auto* pad=CPad::GetPad(0);pad->NewState.Cross=a.testGas>0?a.testGas:0;pad->NewState.Square=a.testGas<0?-a.testGas:0;pad->NewState.LeftStickX=a.testSteer;
    }
}
void AfterWorld() {if(Active() && State().worldReady) State().world.Reconcile();}
void AfterProcess() {
    auto& a=State(); if(!Active()) return;
    auto* p=FindPlayerPed(); if(!p) return;
    if(a.sandbox && !a.worldReady) PrepareSandbox(p);
    if(!a.sandbox) a.worldReady=!CCutsceneMgr::IsCutsceneProcessing();
    if(!a.worldReady) return;
    if(a.previousHealth<=0 && p->m_fHealth>0) ++a.life;
    a.previousHealth=p->m_fHealth;
    TestCommands(p);
    UpdateWorld();
    static uint64_t invalidStateLog=0;
    auto s=Capture(p); a.world.After(s); if(vc::ValidState(s)) a.network.Publish(s);
    else if(vc::NowMs()-invalidStateLog>2000) {invalidStateLog=vc::NowMs();Log("invalid player state move="+std::to_string(s.move)+" pedState="+std::to_string(s.pedState)+" health="+std::to_string(s.health)+" flags="+std::to_string(s.flags)+" anims="+std::to_string(s.animationCount));}
    for(unsigned i=0;i<vc::kMaxPlayers;++i) { ApplyRemote(i); if(a.actors[i].ped) a.world.Attach(a.actors[i].ped,a.snapshot.players[i].state); }
    for(auto& event:a.network.TakeEvents()) ApplyCombatEvent(event.message);
    if(a.snapshot.self!=a.snapshot.host && a.snapshot.host<vc::kMaxPlayers) {
        auto& host=a.snapshot.players[a.snapshot.host];
        if(host.hasState) {
            CClock::ms_nGameClockHours=host.state.hour; CClock::ms_nGameClockMinutes=host.state.minute;
            CClock::ms_nGameClockSeconds=host.state.second;
            CWeather::OldWeatherType=host.state.oldWeather; CWeather::NewWeatherType=host.state.weather;
        }
    }
    TraceVehicles();Diagnostics(p);
}
void Render() {
    if(!Scene.camera || !rw::d3d::d3ddevice) return;
    Initialise(); auto& a=State(); if(a.shutdown) return;
    if(!FrontEndMenuManager.m_bMenuActive && !a.worldReady && !a.spawnWaiting) return;
    std::vector<vc::ui::PlayerIndicator> indicators;
    if(a.worldReady && FindPlayerPed() && !FrontEndMenuManager.m_bMenuActive) {
        for(unsigned i=0;i<vc::kMaxPlayers;++i) {
            auto& actor=a.actors[i]; if(!actor.ped) continue;
            auto pos=actor.ped->GetPosition(); float distance=(pos-FindPlayerPed()->GetPosition()).Magnitude();
            if(distance>80) continue; pos.z+=1.2f;
            RwV3d input{pos.x,pos.y,pos.z},screen{}; float w,h;
            if(!CSprite::CalcScreenCoors(input,&screen,&w,&h,true)) continue;
            const auto& s=a.snapshot.players[i].state;
            indicators.push_back({uint8_t(i),actor.generation,screen.x,screen.y,distance,s.health,s.armour,s.weapon,WeaponIcon(s.weapon),a.snapshot.players[i].name});
        }
    }
    vc::ui::Hud().Render(rw::d3d::d3ddevice,a.snapshot,a.name,a.address,a.port,a.worldReady||a.spawnWaiting,FrontEndMenuManager.m_bMenuActive,indicators);
    if(a.sandbox && a.captureFrame) {a.captureFrame=false;Log(CaptureNativeFrame(rw::d3d::d3ddevice)?"native render readback saved":"native render readback failed");}
    if(a.sandbox && a.recordUntil) {
        auto now=vc::NowMs();
        if(now>=a.recordUntil) {a.recordUntil=0;a.recordManifest.close();Log("native frame sequence saved frames="+std::to_string(a.recordIndex));}
        else if(a.recordInterval<=33 || now>=a.recordNext) {
            char name[32];sprintf_s(name,"frame-%05u.bmp",a.recordIndex++);
            if(CaptureNativeFrame(rw::d3d::d3ddevice,(a.recordPath+"/"+name).c_str())) {a.recordManifest<<now<<','<<name<<'\n';a.recordManifest.flush();}
            a.recordNext=now+a.recordInterval;
        }
    }
}
bool CapturingInput() { return State().initialised && !State().shutdown && vc::ui::Hud().Capturing(); }
bool WindowMessage(void* window,unsigned message,uintptr_t wp,intptr_t lp) {
    auto& a=State(); if(!a.initialised || a.shutdown) return false;
    return vc::ui::Hud().ProcessMessage(static_cast<HWND>(window),message,wp,lp);
}


void ApplyRemoteBones(CPed* ped) {
    if(!Active() || !ped || !ped->GetClump() || !IsClumpSkinned(ped->GetClump())) return;
    vc::PlayerState state;bool found=false;
    for(auto& actor:State().actors) if(actor.ped==ped) {state=actor.track.At(double(vc::NowMs())*.001-.1);found=true;break;}
    if(!found) found=State().world.BoneState(ped,state);
    if(!found) return;
    // Rebuild all bone transforms when the replica returns into view, even if
    // the authoritative NPC was culled on its owner's camera.
    Animations(ped,state);RpAnimBlendClumpUpdateAnimations(ped->GetClump(),0.f,true);
    for(size_t i=0;i<vc::kAimBoneTags.size();++i) if(state.boneMask&(1u<<i)) {
        auto* frame=RpAnimBlendClumpFindBone(ped->GetClump(),vc::kAimBoneTags[i]);if(!frame || !frame->hanimFrame) continue;
        const auto& q=state.bones[i];frame->hanimFrame->q.imag.x=q.x;frame->hanimFrame->q.imag.y=q.y;frame->hanimFrame->q.imag.z=q.z;frame->hanimFrame->q.real=q.w;
    }
}
void BeforePedRender(CPed* ped) {
    if(!Active() || !ped) return;
    State().world.Rendered(ped);
    if(!ReplicaPed(ped)) return;
    // A vehicle renders its occupants directly; commit skin matrices here as
    // well as PreRender, after both the vehicle and ped transforms are current.
    ped->GetMatrix().UpdateRW();ped->UpdateRwFrame();
    ApplyRemoteBones(ped);ped->UpdateRpHAnim();
}
ExplosionDamageScope::ExplosionDamageScope():previous_(State().explosionDamage) {State().explosionDamage=true;}
ExplosionDamageScope::~ExplosionDamageScope() {State().explosionDamage=previous_;}
bool RemoteVehicle(CVehicle* v) {return Active() && State().world.Hold(v);}
bool ExplosionTarget(CPhysical* p) {
    if(!Active()) return true;
    if(!p || ReplicaPed(p->IsPed()?static_cast<CPed*>(p):nullptr) || State().world.Hold(p)) return false;
    return p==FindPlayerPed() || State().world.Owns(p) || PopulationOwner();
}
void RecordExplosion(CEntity* exploding,CEntity* culprit,int type,const CVector& position,uint32_t delay,bool sound) {
    auto& a=State();if(!Active() || !a.worldReady || a.replayingExplosion) return;
    CEntity* source=exploding && a.world.Owns(exploding)?exploding:culprit;
    if(source && source!=FindPlayerPed() && !a.world.Owns(source)) return;
    if(!source && !PopulationOwner()) return;
    vc::Message message;message.kind=vc::Kind::Explosion;auto& blast=message.explosion;
    blast.position={position.x,position.y,position.z};blast.type=uint8_t(type);blast.delay=std::min(delay,10000u);blast.sound=sound;
    blast.source=source==FindPlayerPed()?0:a.world.Id(source);blast.generation=a.world.Generation(blast.source);blast.life=a.life;
    if(vc::ValidExplosion(blast) && a.network.SendEvent(message)) ++a.explosionsSent;
}
void ShareProjectile(CObject* p) {if(Active()) State().world.Projectile(p);}
CombatScope::CombatScope(CEntity* source):previous_(State().combatSource) {State().combatSource=source;}
CombatScope::~CombatScope() {State().combatSource=previous_;}
bool RoutePedDamage(CPed* victim,CEntity* attacker,int weapon,float damage,int piece,uint8_t direction) {
    auto& a=State();if(!Active() || a.applyingDamage) return false;
    if(a.world.RouteDamage(victim,attacker,weapon,damage,piece,direction)) return true;
    for(unsigned id=0;id<vc::kMaxPlayers;++id) if(a.actors[id].ped==victim) {
        if((attacker==FindPlayerPed() || a.world.Owns(attacker)) && damage>0) {
            vc::Message hit;hit.kind=vc::Kind::Hit;hit.hit.target=uint8_t(id);hit.hit.targetEpoch=a.snapshot.players[id].epoch;hit.hit.life=a.snapshot.players[id].state.life;
            hit.hit.weapon=uint8_t(weapon);hit.hit.damage=std::clamp(damage,0.01f,1000.f);hit.hit.piece=uint8_t(std::clamp(piece,0,6));hit.hit.direction=direction;
            hit.hit.source=attacker==FindPlayerPed()?0:a.world.Id(attacker);hit.hit.sourceGeneration=a.world.Generation(hit.hit.source);hit.hit.sourceLife=a.life;
            if(a.network.SendEvent(hit)) ++a.hitsSent;
        }
        return true;
    }
    // Damage generated by a remote shooter is delivered through its owner event.
    if(!a.explosionDamage && victim==FindPlayerPed() && attacker && ReplicaPed(attacker->IsPed()?static_cast<CPed*>(attacker):nullptr)) return true;
    return false;
}
bool RouteVehicleDamage(CVehicle* victim,CEntity* attacker,int weapon,float damage) {
    return Active() && State().world.RouteDamage(victim,attacker,weapon,damage,0,0);
}
bool RouteObjectDamage(CEntity* victim,float damage) {
    if(!Active()) return false;
    auto* object=static_cast<CObject*>(victim);
    auto* source=State().combatSource?State().combatSource:object->m_pDamageEntity;
    return State().world.RouteDamage(victim,source,WEAPONTYPE_UNIDENTIFIED,damage,0,0);
}
void RecordShot(CEntity* shooter,CEntity* victim,int weapon,const CVector& start,const CVector& target,const CVector& normal,int surface,int piece,uint8_t options) {
    auto& a=State();if(!Active() || !a.worldReady || !shooter || (shooter!=FindPlayerPed() && !a.world.Owns(shooter))) return;
    vc::Message m;m.kind=vc::Kind::Shot;m.shot.weapon=uint8_t(weapon);
    m.shot.start={start.x,start.y,start.z};m.shot.end={target.x,target.y,target.z};m.shot.source=shooter==FindPlayerPed()?0:a.world.Id(shooter);m.shot.sourceGeneration=a.world.Generation(m.shot.source);m.shot.sourceLife=shooter==FindPlayerPed()?a.life:1;
    CVector delta=start-shooter->GetPosition();m.shot.localMuzzle={DotProduct(delta,shooter->GetRight()),DotProduct(delta,shooter->GetForward()),DotProduct(delta,shooter->GetUp())};
    m.shot.flags=options;
    if(victim) {
        m.shot.flags=uint8_t(options|1);m.shot.normal={normal.x,normal.y,normal.z};m.shot.surface=uint8_t(surface);m.shot.impactPiece=uint8_t(piece);
        m.shot.impactKind=victim->IsPed()?2:victim->IsVehicle()?3:victim->IsObject()?4:1;
    }
    if(vc::ValidShot(m.shot) && a.network.SendEvent(m)) ++a.shotsSent;
}
bool PopulationOwner() {return !Active() || State().snapshot.self==State().snapshot.host || (State().snapshot.self<vc::kMaxPlayers && (State().snapshot.populationMask&(1u<<State().snapshot.self)));}
bool HoldReplica(CPhysical* p) {return Active() && State().world.Hold(p);}
bool ReplicaPed(CPed* p) {
    if(!Active() || !p) return false;
    for(auto& actor:State().actors) if(actor.ped==p) return true;
    return State().world.ReplicaPed(p);
}
bool CanEnterVehicle(CPed* p,CVehicle* v) {return !Active() || State().world.CanEnter(p,v);}
bool VehicleOccupants(CEntity* a,CEntity* b) {
    if(!Active() || !a || !b) return false;
    if(a->IsPed() && b->IsVehicle()) return static_cast<CPed*>(a)->bInVehicle && static_cast<CPed*>(a)->m_pMyVehicle==b;
    if(b->IsPed() && a->IsVehicle()) return static_cast<CPed*>(b)->bInVehicle && static_cast<CPed*>(b)->m_pMyVehicle==a;
    return false;
}
void CollisionContact(CPhysical* a,CPhysical* b,const CVector& move,const CVector& turn) {if(Active()) State().world.Contact(a,b,move,turn);}
void WorldReset() {
    auto& a=State(); if(!a.initialised) return;
    a.world.Clear();
    for(auto& actor:a.actors) ClearActor(actor);
    a.worldFrame=UINT32_MAX;
    a.worldReady=false; a.loading=false; a.spawnWaiting=false;
}
void Shutdown() {
    auto& a=State(); if(!a.initialised || a.shutdown) return;
    auto limit=a.frameLimit.Update(false,a.onlineFrameLimit,{FrontEndMenuManager.m_PrefsFrameLimiter!=0,RsGlobal.maxFPS});
    FrontEndMenuManager.m_PrefsFrameLimiter=limit.enabled;RsGlobal.maxFPS=limit.fps;
    a.network.Stop(); a.server.Stop(); vc::ui::Hud().Shutdown(); a.shutdown=true;
    Log("source adapter shutdown");
}
}
