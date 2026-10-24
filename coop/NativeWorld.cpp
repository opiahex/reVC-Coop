#include "NativeWorld.h"
#include <set>
#include <numbers>
#include <sstream>
#include "common.h"
#undef lerp
#include "Coop.h"
#ifdef GetObject
#undef GetObject
#endif
#include "Pools.h"
#include "World.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "CivilianPed.h"
#include "Population.h"
#include "CopPed.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"
#include "Heli.h"
#include "Plane.h"
#include "Object.h"
#include "DummyObject.h"
#include "CarCtrl.h"
#include "CarAI.h"
#include "ModelInfo.h"
#include "Streaming.h"
#include "VisibilityPlugins.h"
#include "AnimBlendAssociation.h"
#include "RpAnimBlend.h"
#include "AnimBlendClumpData.h"
#include "RwHelper.h"
#include "WeaponInfo.h"
#include "Pad.h"
#include "Particle.h"
#include "ColModel.h"
#include "Coronas.h"
#include "EventList.h"

namespace vccoop {
namespace {
vc::Vec3 Wire(const CVector& p) { return {p.x,p.y,p.z}; }
CVector Native(vc::Vec3 p) { return CVector(p.x,p.y,p.z); }
bool InWorld(vc::Vec3 p) {
    return vc::ValidPoint(p) && p.x>WORLD_MIN_X+3 && p.x<WORLD_MAX_X-3 && p.y>WORLD_MIN_Y+3 && p.y<WORLD_MAX_Y-3;
}
float Distance(vc::Vec3 a,vc::Vec3 b) { float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z; return x*x+y*y+z*z; }
CEntity* Resolve(vc::EntityKind kind,int handle) {
    if(handle<0) return nullptr;
    switch(kind) {
    case vc::EntityKind::Ped: return CPools::GetPedPool()?CPools::GetPed(handle):nullptr;
    case vc::EntityKind::Vehicle: return CPools::GetVehiclePool()?CPools::GetVehicle(handle):nullptr;
    case vc::EntityKind::Object: return CPools::GetObjectPool()?CPools::GetObject(handle):nullptr;
    }
    return nullptr;
}
int Handle(vc::EntityKind kind,CEntity* p) {
    return kind==vc::EntityKind::Ped?CPools::GetPedRef(static_cast<CPed*>(p)):kind==vc::EntityKind::Vehicle?CPools::GetVehicleRef(static_cast<CVehicle*>(p)):CPools::GetObjectRef(static_cast<CObject*>(p));
}
uint32_t Key(vc::EntityKind kind,int handle) { return (uint32_t(kind)<<24)|uint32_t(handle); }
void Detach(CPed* p) {
    auto* v=p->m_pMyVehicle;
    if(v && IsVehiclePointerValid(v)) {
        if(v->pDriver==p) v->RemoveDriver(); else v->RemovePassenger(p);
    }
    p->m_pMyVehicle=nullptr; p->bInVehicle=false;
    p->bUsesCollision=true; p->bAffectedByGravity=true;
}
void Transform(CPhysical* p,const vc::Transform& t) {
    if(!InWorld(t.position)) return;
    p->Remove();
    p->GetMatrix().GetRight()=Native(t.right); p->GetMatrix().GetForward()=Native(t.forward); p->GetMatrix().GetUp()=Native(t.up);
    p->SetPosition(Native(t.position));
    p->m_vecMoveSpeed=Native(t.velocity)*.02f;
    p->GetMatrix().UpdateRW(); p->UpdateRwFrame(); p->Add();
}
vc::Transform SampleTransform(CPhysical* p) {
    vc::Transform t;
    t.position=Wire(p->GetPosition()); t.velocity=Wire(p->m_vecMoveSpeed*50.f);
    t.right=Wire(p->GetRight()); t.forward=Wire(p->GetForward()); t.up=Wire(p->GetUp()); return t;
}
CVehicle* NewVehicle(unsigned model) {
    if(!CModelInfo::GetModelInfo(model) || CModelInfo::GetModelInfo(model)->GetModelType()!=MITYPE_VEHICLE) return nullptr;
    if(CModelInfo::IsBikeModel(model)) return new CBike(model,MISSION_VEHICLE);
    if(CModelInfo::IsBoatModel(model)) return new CBoat(model,MISSION_VEHICLE);
    if(CModelInfo::IsHeliModel(model)) return new CHeli(model,MISSION_VEHICLE);
    if(CModelInfo::IsPlaneModel(model)) return new CPlane(model,MISSION_VEHICLE);
    if(CModelInfo::IsCarModel(model)) return new CAutomobile(model,MISSION_VEHICLE);
    return nullptr;
}
unsigned DoorForSeat(unsigned seat) { return seat==0?CAR_DOOR_LF:seat==1?CAR_DOOR_RF:seat==2?CAR_DOOR_LR:CAR_DOOR_RR; }
unsigned SeatForDoor(unsigned door) { return door==CAR_DOOR_RF?1:door==CAR_DOOR_LR?2:door==CAR_DOOR_RR?3:255; }
void LightDiagnostics(CVehicle* v,std::ostream& out) {
    if(!v->IsCar() || !v->GetClump()) return;
    auto* info=static_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(v->GetModelIndex()));
    auto right=v->GetMatrix()*info->m_positions[CAR_POS_HEADLIGHTS];
    auto left=right-v->GetRight()*(2.f*info->m_positions[CAR_POS_HEADLIGHTS].x);
    float error=0;unsigned count=0;
    for(unsigned index:{0u,1u,6u,7u})
        if(auto* corona=CCoronas::Find(uintptr(v)+index);corona && corona->registeredThisFrame) {
            error=std::max(error,(corona->coors-((index==0 || index==6)?left:right)).Magnitude());++count;
        }
    out<<",\"headlightError\":"<<error<<",\"headlightCount\":"<<count;
}
}
struct NativeWorld::Impl {
    struct Record {
        vc::Entity state; CEntity* pointer{}; int handle{-1}; bool allocated{},owned{},initialised{},tombstone{};
        bool originalCollision{},originalStreaming{}; uint8_t originalStatus{};
        vc::WorldInterpolator track; vc::VehicleMotion motion; uint64_t received{},removedAt{}; uint32_t sequence{}; uint64_t invalidAt{},renderedAt{};
        bool Valid() const { return pointer && Resolve(state.kind,handle)==pointer && pointer->GetModelIndex()==state.model; }
    };
    std::map<uint32_t,Record> records;
    std::map<uint32_t,uint64_t> pending,requests;
    vc::BackgroundClient::Snapshot net; vc::BackgroundClient* network{};
    std::array<CPed*,vc::kMaxPlayers> players{};
    CapturePedFn capture{}; AnimatePedFn animate{};
    vc::ContactHistory contacts; std::map<std::pair<uint32_t,uint32_t>,uint64_t> contactSent;
    uint64_t scanned{},lastRetire{},lastInput{}; uint32_t pendingDriver{},lastSeatVehicle{}; unsigned seatDoor{},lastSeatIndex{}; uint64_t lastSeatUntil{};
    bool gHeld{},applying{};
    NativeWorld* outer{};
    uint32_t Id(CEntity* pointer) const {
        if(!pointer) return 0;
        for(auto& [id,r]:records) if(r.pointer==pointer && r.Valid()) return id;
        return 0;
    }
    bool Owns(CEntity* pointer) const { auto id=Id(pointer); return id && records.at(id).state.owner==net.self; }
    bool IsPlayer(CEntity* p) const { return std::find(players.begin(),players.end(),p)!=players.end(); }
    void Destroy(Record& r) {
        if(!r.Valid()) {r.pointer=nullptr; return;}
        auto* p=r.pointer;
        if(r.state.kind==vc::EntityKind::Vehicle) {
            for(auto* player:players) if(player && player->m_pMyVehicle==p) Detach(player);
            for(auto& [id,npc]:records) if(npc.state.kind==vc::EntityKind::Ped && npc.Valid() && static_cast<CPed*>(npc.pointer)->m_pMyVehicle==p) Detach(static_cast<CPed*>(npc.pointer));
        }
        if(r.allocated) {
            if(r.state.kind==vc::EntityKind::Ped) Detach(static_cast<CPed*>(p));
            CWorld::Remove(p); delete p;
        } else {
            p->bStreamingDontDelete=r.originalStreaming;
            // A destroyed map original must not regain an invisible collider
            // merely because its replication record was retired/disconnected.
            if(!p->IsObject()) p->bUsesCollision=r.originalCollision;
            if(p->IsVehicle() && !r.owned) p->SetStatus(r.originalStatus);
        }
        r.pointer=nullptr; r.handle=-1;
    }
    void Bind(Record& r,CEntity* pointer,bool allocated) {
        r.pointer=pointer; r.handle=Handle(r.state.kind,pointer); r.allocated=allocated;
        r.originalCollision=pointer->bUsesCollision; r.originalStatus=pointer->GetStatus(); r.originalStreaming=pointer->bStreamingDontDelete;
        pointer->bStreamingDontDelete=true;
    }
    bool Create(Record& r) {
        const auto& e=r.state;
        if(!InWorld(vc::EntityPosition(e))) return false;
        if(e.creator==net.self && net.self<vc::kMaxPlayers && e.creatorEpoch==net.players[net.self].epoch)
            if(auto* original=Resolve(e.kind,int(e.key&0xFFFFFF));original && original->GetModelIndex()==e.model && !Id(original)) { Bind(r,original,false); return true; }
        if(e.kind==vc::EntityKind::Object && !e.mission && !(e.object.flags&64)) {
            auto* pool=CPools::GetObjectPool();
            for(int i=0;i<pool->GetSize();++i) if(auto* object=pool->GetSlot(i))
                if(!Id(object) && object->GetModelIndex()==e.model && Distance(Wire(object->m_objectMatrix.GetPosition()),e.origin)<.01f) { Bind(r,object,false); return true; }
            auto* dummies=CPools::GetDummyPool();
            for(int i=0;i<dummies->GetSize();++i) if(auto* dummy=dummies->GetSlot(i))
                if(dummy->GetModelIndex()==e.model && Distance(Wire(dummy->GetPosition()),e.origin)<.01f) {
                    CStreaming::RequestModel(e.model,STREAMFLAGS_DEPENDENCY);
                    if(!CStreaming::HasModelLoaded(e.model)) return false;
                    if(auto* object=CPopulation::ConvertToRealObject(static_cast<CDummyObject*>(dummy),true)) {
                        if(!object->m_rwObject) object->CreateRwObject();
                        Bind(r,object,false);return true;
                    }
                    return false;
                }
            // Map streaming may not have instantiated its original yet. Wait for
            // that instance instead of spawning a clone which would duplicate it later.
            return false;
        }
        if(!CModelInfo::GetModelInfo(e.model)) return false;
        CStreaming::RequestModel(e.model,STREAMFLAGS_DONT_REMOVE);
        if(!CStreaming::HasModelLoaded(e.model)) return false;
        CEntity* p=nullptr;
        if(e.kind==vc::EntityKind::Vehicle) p=NewVehicle(e.model);
        else if(e.kind==vc::EntityKind::Ped) {
            unsigned type=e.model;int modifier=0;
            if(e.ped.pedType==PEDTYPE_COP) {
                type=e.model==MI_FBI?COP_FBI:e.model==MI_SWAT?COP_SWAT:e.model==MI_ARMY?COP_ARMY:COP_STREET;
                if(e.model>=MI_VICE1 && e.model<=MI_VICE8) {type=COP_MIAMIVICE;modifier=e.model-MI_VICE1;}
            }
            p=CPopulation::AddPed(ePedType(e.ped.pedType),type,Native(e.ped.position),modifier);
        }
        else {auto* object=new CObject(e.model,true);if(object) object->ObjectCreatedBy=MISSION_OBJECT;p=object;}
        if(!p) return false;
        if(e.kind==vc::EntityKind::Ped) {
            auto* ped=static_cast<CPed*>(p); ped->CharCreatedBy=MISSION_CHAR;
            CVisibilityPlugins::SetClumpAlpha(ped->GetClump(),255);
            // AddPed already inserted it into CWorld.
        } else {
            p->SetPosition(Native(vc::EntityPosition(e))); CWorld::Add(p);
        }
        p->m_area=e.area; Bind(r,p,true); return true;
    }
    bool Capture(Record& r,vc::Entity& e) {
        if(!r.Valid()) return false;
        auto* p=r.pointer; e.area=p->m_area; e.model=p->GetModelIndex();
        if(e.kind==vc::EntityKind::Ped) {
            auto* ped=static_cast<CPed*>(p); e.ped=capture(ped); outer->DescribeVehicle(ped,e.ped);
        } else if(e.kind==vc::EntityKind::Vehicle) {
            auto* car=static_cast<CVehicle*>(p); auto& v=e.vehicle;
            v.transform=SampleTransform(car); v.turn=Wire(car->m_vecTurnSpeed*50.f);
            v.health=std::max(car->m_fHealth,0.f); v.steer=car->m_fSteerAngle; v.gas=car->m_fGasPedal; v.brake=std::max(car->m_fBrakePedal,0.f);
            v.type=uint8_t(car->m_vehType); v.primary=car->m_currentColour1; v.secondary=car->m_currentColour2;
            if(car->IsHeli()) {auto* h=static_cast<CHeli*>(car);v.rotor=h->m_fRotorRotation;v.searchX=h->m_fSearchLightX;v.searchY=h->m_fSearchLightY;v.searchIntensity=h->m_fSearchLightIntensity;v.heliStatus=uint8_t(h->m_heliStatus);}
            v.gear=uint8_t(car->m_nCurrentGear); v.passengers=uint8_t(std::min<unsigned>(car->m_nNumMaxPassengers,8));
            v.flags=(car->bEngineOn?1:0)|(car->bLightsOn?2:0)|(car->bRenderScorched?4:0)|(car->GetStatus()==STATUS_WRECKED?8:0);
            const float step=std::max(CTimer::GetTimeStepInSeconds(),.001f);
            if(car->IsCar()) {
                auto* a=static_cast<CAutomobile*>(car); v.engine=a->Damage.m_engineStatus; v.panels=a->Damage.m_panelStatus;
                // Native panel damage also writes a non-light bit for the windscreen
                // into this storage. Encode only the four actual light slots; never
                // let that unused bit suppress the vehicle's entire moving snapshot.
                v.lights=0;for(unsigned light=0;light<4;++light) v.lights|=uint32_t(a->Damage.GetLightStatus(eLights(light))&3)<<(light*2);
                for(unsigned i=0;i<6;++i) {v.doors[i]=uint8_t(a->Damage.m_doorStatus[i]);v.doorAngles[i]=a->Doors[i].m_fAngle;}
                for(unsigned i=0;i<4;++i) {v.wheels[i]=a->Damage.m_wheelStatus[i];v.wheelAngle[i]=std::remainder(a->m_aWheelRotation[i],2*std::numbers::pi_v<float>);v.wheelOmega[i]=a->m_aWheelSpeed[i]/step;v.wheelPosition[i]=a->m_aWheelPosition[i];v.suspension[i]=a->m_aSuspensionSpringRatio[i];}
            } else if(car->IsBike()) {
                auto* b=static_cast<CBike*>(car); v.wheelSteer=b->m_fWheelAngle;v.bikeLean=b->m_fLeanLRAngle;
                for(unsigned i=0;i<2;++i) {v.wheels[i]=b->m_wheelStatus[i];v.wheelAngle[i]=std::remainder(b->m_aWheelRotation[i],2*std::numbers::pi_v<float>);v.wheelOmega[i]=b->m_aWheelSpeed[i]/step;v.wheelPosition[i]=b->m_aWheelPosition[i];}
                for(unsigned i=0;i<4;++i) v.suspension[i]=b->m_aSuspensionSpringRatio[i];
            }
        } else {
            auto* o=static_cast<CObject*>(p);e.object.transform=SampleTransform(o);e.object.turn=Wire(o->m_vecTurnSpeed*50.f);
            e.object.flags=(e.object.flags&64)|(o->bIsVisible?1:0)|(o->bUsesCollision?2:0)|(o->bGlassCracked?4:0)|(o->bGlassBroken?8:0)|(o->bRenderDamaged?16:0)|(o->bHasBeenDamaged?32:0);
        }
        bool valid=vc::ValidEntity(e);
        if(!valid && vc::NowMs()-r.invalidAt>10000) {
            r.invalidAt=vc::NowMs();std::ostringstream reason;auto& v=e.vehicle;
            reason<<"invalid native state id="<<e.id<<" kind="<<int(e.kind)<<" model="<<e.model<<" area="<<int(e.area)<<" health="<<v.health<<" steering="<<v.steer<<" gas="<<v.gas<<" brake="<<v.brake<<" type="<<int(v.type)<<" gear="<<int(v.gear)<<" panels="<<v.panels<<" lights="<<v.lights;
            for(unsigned i=0;i<6;++i) reason<<" door"<<i<<"="<<int(v.doors[i])<<"@"<<v.doorAngles[i];
            for(unsigned i=0;i<4;++i) reason<<" wheel"<<i<<"="<<v.wheelAngle[i]<<"/"<<v.wheelOmega[i]<<"/"<<v.wheelPosition[i]<<"/"<<v.suspension[i];
            if(outer->log) outer->log(reason.str());
        }
        return valid;
    }
    void Pose(Record& r,const vc::Entity& e) {
        if(!r.Valid()) return;
        auto* p=r.pointer; p->m_area=e.area;
        if(e.kind==vc::EntityKind::Ped) {
            auto* ped=static_cast<CPed*>(p);
            ped->Teleport(Native(e.ped.position));ped->SetHeading(e.ped.heading);ped->m_fRotationCur=ped->m_fRotationDest=e.ped.heading;
            ped->m_fHealth=e.ped.health;ped->m_fArmour=e.ped.armour;ped->SetMoveState(eMoveState(e.ped.move));
            animate(ped,e.ped);
            // Occupants are rendered through their vehicle and can bypass the
            // ordinary ped PreRender path. Always commit their actual skeleton.
            RpAnimBlendClumpUpdateAnimations(ped->GetClump(),0.f,true);
            outer->Attach(ped,e.ped);ped->UpdateRpHAnim();
            CVisibilityPlugins::SetClumpAlpha(ped->GetClump(),255);
        } else if(e.kind==vc::EntityKind::Vehicle) {
            auto* car=static_cast<CVehicle*>(p);const auto& v=e.vehicle;
            auto* local=LocalPlayer();
            if(local && local->bInVehicle && local->m_pMyVehicle==car && Distance(Wire(car->GetPosition()),v.transform.position)>50*50)
                CStreaming::LoadSceneCollision(Native(v.transform.position));
            Transform(car,v.transform);car->m_vecTurnSpeed=Native(v.turn)*.02f;
            car->m_fHealth=v.health;car->m_fSteerAngle=v.steer;car->m_fGasPedal=v.gas;car->m_fBrakePedal=v.brake;car->m_nCurrentGear=v.gear;
            car->m_currentColour1=v.primary;car->m_currentColour2=v.secondary;
            car->bEngineOn=(v.flags&1)!=0;car->bLightsOn=(v.flags&2)!=0;car->bRenderScorched=(v.flags&4)!=0;
            car->SetStatus(v.flags&8?STATUS_WRECKED:STATUS_PHYSICS);
            car->bIsVisible=true;car->bUsesCollision=true;car->bIsInSafePosition=true;
            if(car->IsCar()) {
                auto* a=static_cast<CAutomobile*>(car);
                bool changed=a->Damage.m_panelStatus!=v.panels;
                for(unsigned i=0;i<6;++i) changed|=a->Damage.m_doorStatus[i]!=v.doors[i];
                if(changed) {
                    a->Fix(); a->Damage.m_panelStatus=v.panels;
                    for(unsigned i=0;i<6;++i) a->Damage.m_doorStatus[i]=v.doors[i];
                    // A snapshot reconstructs the body; replaying flying parts
                    // on every later door/panel change duplicates old debris.
                    a->SetupDamageAfterLoad(true);
                }
                a->Damage.m_engineStatus=v.engine;a->Damage.m_lightStatus=v.lights;
                const unsigned nodes[]={CAR_BONNET,CAR_BOOT,CAR_DOOR_LF,CAR_DOOR_RF,CAR_DOOR_LR,CAR_DOOR_RR};
                for(unsigned i=0;i<6;++i) if(a->m_aCarNodes[nodes[i]]) {
                    float full=std::abs(a->Doors[i].m_fMaxAngle)>std::abs(a->Doors[i].m_fMinAngle)?a->Doors[i].m_fMaxAngle:a->Doors[i].m_fMinAngle;
                    a->OpenDoor(nodes[i],eDoors(i),std::abs(full)>.001f?std::clamp(v.doorAngles[i]/full,0.f,1.f):0.f);
                }
                for(unsigned i=0;i<4;++i) {a->Damage.m_wheelStatus[i]=v.wheels[i];a->m_aWheelRotation[i]=v.wheelAngle[i];a->m_aWheelSpeed[i]=v.wheelOmega[i]*CTimer::GetTimeStepInSeconds();a->m_aWheelPosition[i]=v.wheelPosition[i];a->m_aSuspensionSpringRatio[i]=v.suspension[i];}
            } else if(car->IsBike()) {
                auto* b=static_cast<CBike*>(car);b->m_fWheelAngle=v.wheelSteer;b->m_fLeanLRAngle=b->m_fLeanLRAngle2=v.bikeLean;b->bLeanMatrixClean=false;b->CalculateLeanMatrix();
                for(unsigned i=0;i<2;++i) {b->m_wheelStatus[i]=v.wheels[i];b->m_aWheelRotation[i]=v.wheelAngle[i];b->m_aWheelSpeed[i]=v.wheelOmega[i]*CTimer::GetTimeStepInSeconds();b->m_aWheelPosition[i]=v.wheelPosition[i];}
                for(unsigned i=0;i<4;++i) b->m_aSuspensionSpringRatio[i]=v.suspension[i];
            }
            if(car->IsHeli()) {auto* h=static_cast<CHeli*>(car);h->m_fRotorRotation=v.rotor;h->m_fSearchLightX=v.searchX;h->m_fSearchLightY=v.searchY;h->m_fSearchLightIntensity=v.searchIntensity;h->m_heliStatus=int8_t(v.heliStatus);}
            car->m_fHealth=v.health;
        } else {
            auto* o=static_cast<CObject*>(p);Transform(o,e.object.transform);
            o->bIsVisible=(e.object.flags&1)!=0;o->bUsesCollision=(e.object.flags&2)!=0;o->bGlassCracked=(e.object.flags&4)!=0;o->bGlassBroken=(e.object.flags&8)!=0;o->bRenderDamaged=(e.object.flags&16)!=0;o->bHasBeenDamaged=(e.object.flags&32)!=0;
            if(e.object.flags&64) {
                if(e.model==MI_MISSILE) CParticle::AddParticle(PARTICLE_ROCKET_SMOKE,o->GetPosition(),CVector(0,0,.02f),nullptr,.12f);
                else if(e.model==MI_TEARGAS) CParticle::AddParticle(PARTICLE_TEARGAS,o->GetPosition(),CVector(0,0,.02f),nullptr,.15f);
            }
        }
        p->GetMatrix().UpdateRW();p->UpdateRwFrame();
    }
    vc::Entity VehiclePose(const Record& r,double now) const {
        // Play actual traffic paths/rotation on the NPC animation timeline.
        // Human-driven cars keep a predicted pose for immediate local contacts.
        for(const auto& player:net.players) if(player.active && player.hasState && player.state.vehicle==r.state.id) return r.motion.At(now);
        return r.track.At(now-.1);
    }
    void Scan() {
        if(!capture || !network || !players[net.self]) return;
        const auto now=vc::NowMs(); if(now-scanned<250) return; scanned=now;
        auto local=Wire(players[net.self]->GetPosition());unsigned sent=0,cars=0,peds=0,objects=0;
        auto candidate=[&](vc::EntityKind kind,CEntity* p) {
            if(!p || sent>=16 || Id(p) || IsPlayer(p) || p->bRemoveFromWorld || !InWorld(Wire(p->GetPosition()))) return;
            bool selected=outer->PopulationOwner();
            if(!selected && !(p->IsVehicle() && players[net.self]->m_pMyVehicle==p)) return;
            bool nearbyViewer=false;const float range=p->IsVehicle() && static_cast<CVehicle*>(p)->IsHeli()?600.f:180.f;
            for(auto* player:players) if(player && player->m_area==p->m_area && Distance(Wire(p->GetPosition()),Wire(player->GetPosition()))<range*range) nearbyViewer=true;
            if(!nearbyViewer) return;
            if(kind==vc::EntityKind::Vehicle && (++cars>45 || static_cast<CVehicle*>(p)->IsTrain())) return;
            if(kind==vc::EntityKind::Ped && (++peds>48 || p->GetModelIndex()==0)) return;
            if(kind==vc::EntityKind::Object) {
                auto* o=static_cast<CObject*>(p);
                if(++objects>32 || o->ObjectCreatedBy!=GAME_OBJECT || o->bIsPickup || o->bIsWeapon || (!o->m_nCollisionDamageEffect && o->m_nSpecialCollisionResponseCases==COLLRESPONSE_NONE)) return;
            }
            int h=Handle(kind,p);uint32_t key=Key(kind,h);if(now-pending[key]<1200) return;
            Record temporary;temporary.state.kind=kind;temporary.state.model=p->GetModelIndex();temporary.pointer=p;temporary.handle=h;
            vc::Entity e;e.kind=kind;e.key=key;e.origin=kind==vc::EntityKind::Object?Wire(static_cast<CObject*>(p)->m_objectMatrix.GetPosition()):Wire(p->GetPosition());
            if(!Capture(temporary,e)) return;
            vc::Message m;m.kind=vc::Kind::EntityCreate;m.entity=e;
            if(network->SendEntity(m)) {pending[key]=now;++sent;}
        };
        auto* vp=CPools::GetVehiclePool();for(int i=0;i<vp->GetSize();++i) candidate(vc::EntityKind::Vehicle,vp->GetSlot(i));
        auto* pp=CPools::GetPedPool();for(int i=0;i<pp->GetSize();++i) candidate(vc::EntityKind::Ped,pp->GetSlot(i));
        auto* op=CPools::GetObjectPool();for(int i=0;i<op->GetSize();++i) candidate(vc::EntityKind::Object,op->GetSlot(i));
    }
};
NativeWorld::NativeWorld():impl_(new Impl) {impl_->outer=this;}
NativeWorld::~NativeWorld()=default;
uint32_t NativeWorld::Id(CEntity* p) const {return impl_->Id(p);}
bool NativeWorld::Owns(CEntity* p) const {return impl_->Owns(p);}
uint32_t NativeWorld::Generation(uint32_t id) const {auto it=impl_->records.find(id);return it==impl_->records.end()?0:it->second.state.generation;}
CEntity* NativeWorld::Pointer(uint32_t id) const {auto it=impl_->records.find(id);return it!=impl_->records.end() && it->second.Valid()?it->second.pointer:nullptr;}
bool NativeWorld::PopulationOwner() const {auto& a=*impl_;return a.net.self==a.net.host || (a.net.self<vc::kMaxPlayers && (a.net.populationMask&(1u<<a.net.self)));}
bool NativeWorld::Hold(CPhysical* p) const {auto id=Id(p);return id && impl_->records.at(id).state.owner!=impl_->net.self;}
bool NativeWorld::ReplicaPed(CPed* p) const {return p && p!=LocalPlayer() && (impl_->IsPlayer(p)||Hold(p));}
void NativeWorld::Clear() {
    auto& a=*impl_;for(auto& [id,r]:a.records) if(r.state.kind==vc::EntityKind::Ped) a.Destroy(r);
    for(auto& [id,r]:a.records) a.Destroy(r);
    a.records.clear();a.pending.clear();a.requests.clear();a.contacts.Clear();a.net={};a.network=nullptr;a.pendingDriver=0;a.scanned=0;
}
void NativeWorld::Begin(const vc::BackgroundClient::Snapshot& net,vc::BackgroundClient& network,const std::array<CPed*,vc::kMaxPlayers>& players,CapturePedFn capture,AnimatePedFn animate) {
    auto& a=*impl_;a.net=net;a.network=&network;a.players=players;a.capture=capture;a.animate=animate;
    for(auto it=a.records.begin();it!=a.records.end();) {
        if(!net.entities.contains(it->first)) {a.Destroy(it->second);it=a.records.erase(it);} else ++it;
    }
    auto now=vc::NowMs();
    // Allocate vehicles before attaching their NPC occupants.
    for(auto kind:{vc::EntityKind::Vehicle,vc::EntityKind::Object,vc::EntityKind::Ped}) for(const auto& [id,entry]:net.entities) {
        const auto& e=entry.state;if(e.kind!=kind) continue;
        auto& r=a.records[id];bool fresh=!r.state.id;bool changed=r.state.generation!=e.generation;
        if(!fresh && (r.state.kind!=e.kind || r.state.model!=e.model)) {a.Destroy(r);r=Impl::Record{};fresh=true;}
        r.state=e;bool owns=e.owner==net.self;
        bool existed=r.Valid();
        auto* localPed=LocalPlayer();
        bool localRide=localPed && localPed->bInVehicle && ((e.kind==vc::EntityKind::Vehicle && r.pointer==localPed->m_pMyVehicle) || (e.kind==vc::EntityKind::Ped && e.ped.vehicle==Id(localPed->m_pMyVehicle)));
        if(!owns && !localRide && localPed && (e.area!=localPed->m_area || Distance(vc::EntityPosition(e),Wire(localPed->GetPosition()))>300*300)) {
            if(r.allocated) a.Destroy(r);continue;
        }
        if(!existed) {
            bool missingOriginal=owns && e.creator==net.self && e.creatorEpoch==net.players[net.self].epoch && (!Resolve(e.kind,int(e.key&0xFFFFFF)) || Resolve(e.kind,int(e.key&0xFFFFFF))->GetModelIndex()!=e.model);
            if(owns && e.creator==net.self && e.creatorEpoch==net.players[net.self].epoch && (r.initialised || missingOriginal)) {
                // A destroyed map prop is persistent state, not an unspawn request:
                // removing its record would restore an intact original on the guest.
                if(e.kind==vc::EntityKind::Object && !(e.object.flags&64)) {r.tombstone=true;r.state.object.flags=0;continue;}
                if(now-r.removedAt>1200) {vc::Message remove;remove.kind=vc::Kind::EntityRemove;remove.entity=e;network.SendEntity(remove);r.removedAt=now;}continue;
            }
            if(!a.Create(r)) continue;
            r.initialised=true;
            if(log) log("world bind id="+std::to_string(id)+" kind="+std::to_string(int(kind))+" owner="+std::to_string(e.owner));
        }
        if(changed) {r.track.Clear();r.motion.Clear();r.received=0;r.sequence=0;}
        if(!owns) {
            if(!r.received || r.received!=entry.received || r.sequence!=entry.sequence) {
                if(e.kind==vc::EntityKind::Vehicle) r.motion.Push(double(entry.received)*.001,e,double(now)*.001);
                r.track.Push(double(entry.received)*.001,e,double(now)*.001);
                r.received=entry.received;r.sequence=entry.sequence;
            }
            a.Pose(r,e.kind==vc::EntityKind::Vehicle?a.VehiclePose(r,ReplicaTime()):r.track.At(ReplicaTime()-.1));
        } else if(changed || !existed) {
            if(r.allocated || (!fresh && !r.owned)) a.Pose(r,e);
            if(r.pointer->IsPed()) {
                // A newly authoritative former mirror must regain native AI
                // eligibility and physical movement, not its replica mission immunity.
                auto* ped=static_cast<CPed*>(r.pointer);ped->CharCreatedBy=e.mission?MISSION_CHAR:RANDOM_CHAR;
                ped->bAffectedByGravity=true;ped->bUsesCollision=!ped->bInVehicle && !ped->DyingOrDead();
            }
            if((r.allocated || (!fresh && !r.owned)) && r.pointer->IsVehicle()) {
                auto* v=static_cast<CVehicle*>(r.pointer);
                v->SetStatus(v->pDriver==LocalPlayer()?STATUS_PLAYER:v->pDriver?STATUS_PHYSICS:STATUS_ABANDONED);
                if(v->pDriver && v->pDriver!=LocalPlayer()) {CCarCtrl::JoinCarWithRoadSystem(v);v->AutoPilot.m_nCarMission=MISSION_CRUISE;v->AutoPilot.m_nCruiseSpeed=20;}
            }
        }
        r.owned=owns;r.pointer->bStreamingDontDelete=true;
    }
    // A driver starts the native entry sequence only after server authority arrives.
    if(a.pendingDriver) {
        auto* e=Pointer(a.pendingDriver);if(e && Owns(e)) {
            auto* local=LocalPlayer();a.pendingDriver=0;
            if(local && !local->bInVehicle) {local->m_vehDoor=CAR_DOOR_LF;local->SetObjective(OBJECTIVE_ENTER_CAR_AS_DRIVER,e);local->SetEnterCar(static_cast<CVehicle*>(e),CAR_DOOR_LF);}
        }
    }
    bool g=(GetForegroundWindow()==GetActiveWindow()) && (GetAsyncKeyState('G')&0x8000);
    if(g && !a.gHeld && !CapturingInput()) Passenger();a.gHeld=g;
}
void NativeWorld::Reconcile() {
    auto& a=*impl_;const double now=ReplicaTime();
    // The native contact solver may temporarily move the other car while
    // solving our owner's impulse. Never let that local replica displacement
    // become its final world/render pose or accumulate across simulation ticks.
    for(auto kind:{vc::EntityKind::Vehicle,vc::EntityKind::Object,vc::EntityKind::Ped})
        for(auto& [id,r]:a.records) if(r.Valid() && !r.owned && r.state.kind==kind) {
            auto pose=kind==vc::EntityKind::Vehicle?a.VehiclePose(r,now):r.track.At(now-.1);if(pose.id) a.Pose(r,pose);
        }
    auto* local=LocalPlayer();
    if(local && local->bInVehicle && local->m_pMyVehicle && Hold(local->m_pMyVehicle) && local->GetPedState()==PED_DRIVING) {
        local->SetPedPositionInCar();local->GetMatrix().UpdateRW();local->UpdateRwFrame();
    }
}
void NativeWorld::After(vc::PlayerState& local) {
    auto& a=*impl_;if(!a.network || a.net.self>=vc::kMaxPlayers) return;
    a.Scan();DescribeVehicle(LocalPlayer(),local);
    const auto now=vc::NowMs();
    std::set<uint32_t> occupied;if(local.vehicle) occupied.insert(local.vehicle);
    for(auto& player:a.net.players) if(player.active && player.hasState && player.state.vehicle) occupied.insert(player.state.vehicle);
    for(auto& [id,r]:a.records) if(r.state.owner==a.net.self && !r.state.mission && r.Valid()) {
        if(r.pointer->IsVehicle() && static_cast<CVehicle*>(r.pointer)->IsHeli()) continue; // Native heli manager owns its flight-away/removal lifecycle.
        auto nativePosition=Wire(r.pointer->GetPosition());
        bool nearPlayer=Distance(nativePosition,local.position)<300*300;
        for(auto& player:a.net.players) if(player.active && player.hasState && Distance(nativePosition,player.state.position)<300*300) nearPlayer=true;
        bool privateProjectile=r.state.kind==vc::EntityKind::Object && (r.state.object.flags&64);
        bool merged=!privateProjectile && !PopulationOwner() && r.state.creator==a.net.self && r.state.creatorEpoch==a.net.players[a.net.self].epoch;
        if((nearPlayer && !merged) || occupied.contains(id)) continue;
        if(r.state.kind==vc::EntityKind::Ped && occupied.contains(Id(static_cast<CPed*>(r.pointer)->m_pMyVehicle))) continue;
        if(now-r.removedAt<1200) continue;
        vc::Message remove;remove.kind=vc::Kind::EntityRemove;remove.entity=r.state;
        if(a.network->SendEntity(remove)) {
            r.removedAt=now;
            if(r.state.kind!=vc::EntityKind::Object) r.allocated=true;
            a.Destroy(r);
        }
    }
    std::vector<vc::Entity> outgoing;
    for(auto& [id,r]:a.records) if(r.state.owner==a.net.self) {
        auto state=r.state;if(r.tombstone) {state.object.flags=0;outgoing.push_back(state);} else if(a.Capture(r,state)) outgoing.push_back(state);
    }
    a.network->PublishEntities(std::move(outgoing));a.contacts.Prune(vc::NowMs());
}
void NativeWorld::DescribeVehicle(CPed* p,vc::PlayerState& s) const {
    s.vehicle=0;s.seat=255;s.door=uint8_t(p->m_vehDoor);s.flags&=uint8_t(~vc::kInVehicle);
    if(!p->bInVehicle && p->GetPedState()!=PED_ENTER_CAR && p->GetPedState()!=PED_EXIT_CAR && p->GetPedState()!=PED_CARJACK) return;
    auto* v=p->m_pMyVehicle;if(!v || !IsVehiclePointerValid(v)) return;
    s.vehicle=Id(v);if(!s.vehicle) return;
    if(p->bInVehicle) s.flags|=vc::kInVehicle;
    if(v->pDriver==p || p->m_objective==OBJECTIVE_ENTER_CAR_AS_DRIVER) s.seat=0;
    else for(unsigned i=0;i<std::min<unsigned>(v->m_nNumMaxPassengers,8);++i) if(v->pPassengers[i]==p) s.seat=uint8_t(i+1);
    if(s.seat==255 && p->m_objective==OBJECTIVE_ENTER_CAR_AS_PASSENGER) s.seat=uint8_t(SeatForDoor(p->m_vehDoor));
    if(p->m_pVehicleAnim) s.vehicleAnimation=uint16_t(p->m_pVehicleAnim->animId);
}
void NativeWorld::Attach(CPed* p,const vc::PlayerState& s) {
    if(!p || p==LocalPlayer()) return;
    auto* e=Pointer(s.vehicle);auto* v=e && e->IsVehicle()?static_cast<CVehicle*>(e):nullptr;
    if(p->m_pMyVehicle && p->m_pMyVehicle!=v) Detach(p);
    if(!v || !(s.flags&vc::kInVehicle)) {
        if(p->bInVehicle) Detach(p);
        p->m_pMyVehicle=v;p->bUsesCollision=!(s.flags&vc::kDead);p->bAffectedByGravity=false;
        p->SetPedState(PedState(s.pedState));return;
    }
    CPed** slot=s.seat==0?&v->pDriver:s.seat>=1 && s.seat<=std::min<unsigned>(v->m_nNumMaxPassengers,8)?&v->pPassengers[s.seat-1]:nullptr;
    if(!slot) return;
    if(*slot && *slot!=p) {
        if(impl_->IsPlayer(*slot)) return;
        Detach(*slot);
    }
    // Direct named fields avoid applying AddPassenger's seating force on every snapshot.
    if(*slot!=p) {*slot=p;p->RegisterReference(reinterpret_cast<CEntity**>(slot));}
    if(p->m_pMyVehicle!=v) {p->m_pMyVehicle=v;v->RegisterReference(reinterpret_cast<CEntity**>(&p->m_pMyVehicle));}
    v->m_nNumPassengers=0;for(auto* occupant:v->pPassengers) if(occupant) ++v->m_nNumPassengers;
    p->bInVehicle=true;p->bUsesCollision=false;p->bAffectedByGravity=false;p->bRenderPedInCar=true;p->m_vehDoor=s.door;
    p->SetPedState(PedState(s.pedState));
    if(p->GetPedState()==PED_DRIVING) p->SetPedPositionInCar();
    p->GetMatrix().UpdateRW();p->UpdateRwFrame();
}
bool NativeWorld::CanEnter(CPed* p,CVehicle* v) {
    auto& a=*impl_;if(p!=LocalPlayer() || !a.network) return true;
    uint32_t id=Id(v);if(!id) return false;
    if(p->m_objective==OBJECTIVE_ENTER_CAR_AS_PASSENGER) {
        unsigned seat=SeatForDoor(p->m_vehDoor);
        if(v->IsBike()) seat=1;
        if(seat>8 || (v->pPassengers[seat-1] && v->pPassengers[seat-1]!=p)) return false;
        return a.lastSeatVehicle==id && a.lastSeatIndex==seat && vc::NowMs()<a.lastSeatUntil;
    }
    // A player already occupying the driver seat cannot be evicted by a guest.
    for(unsigned i=0;i<vc::kMaxPlayers;++i) if(a.net.players[i].active && i!=a.net.self) {
        const auto& s=a.net.players[i].state;if((s.flags&vc::kInVehicle) && s.vehicle==id && s.seat==0) return false;
    }
    if(Owns(v)) return true;
    a.pendingDriver=id;
    if(vc::NowMs()-a.requests[id]>=500) {vc::Message claim;claim.kind=vc::Kind::EntityClaim;claim.entity.id=id;claim.entity.generation=Generation(id);a.network->SendEntity(claim);a.requests[id]=vc::NowMs();}
    return false;
}
bool NativeWorld::Passenger() {
    auto& a=*impl_;auto* p=LocalPlayer();if(!p || p->bInVehicle || !a.network) return false;
    CVehicle* best=nullptr;unsigned selected=0;float distance=36.f;
    for(auto& [id,r]:a.records) if(r.state.kind==vc::EntityKind::Vehicle && r.Valid() && r.state.area==p->m_area) {
        auto* v=static_cast<CVehicle*>(r.pointer);
        for(unsigned seat=1;seat<=std::min<unsigned>(v->m_nNumMaxPassengers,8);++seat) {
            if(v->pPassengers[seat-1]) continue;
            bool taken=false;for(auto& player:a.net.players) if(player.active && (player.state.flags&vc::kInVehicle) && player.state.vehicle==id && player.state.seat==seat) taken=true;
            if(taken) continue;
            CVector door=v->GetMatrix()*CVector(1.2f,-.5f,0);
            if(v->IsBike()) door=v->GetMatrix()*CVector(0,-.7f,0);
            else if(v->IsCar()) {
                unsigned node=DoorForSeat(seat);auto* car=static_cast<CAutomobile*>(v);
                if(!car->m_aCarNodes[node]) {if(!v->bIsBus) continue;node=CAR_DOOR_RF;}
                if(!car->m_aCarNodes[node]) continue;
                v->GetComponentWorldPosition(node,door);
            }
            float d=(door-p->GetPosition()).MagnitudeSqr();if(d<distance) {distance=d;best=v;selected=seat;}
        }
    }
    if(!best) return false;
    vc::Message request;request.kind=vc::Kind::SeatRequest;request.entity.id=Id(best);request.entity.generation=Generation(request.entity.id);request.id=uint8_t(selected);
    a.seatDoor=DoorForSeat(selected);return a.network->SendEntity(request);
}
bool NativeWorld::Event(const vc::Message& m) {
    auto& a=*impl_;
    if(m.kind==vc::Kind::SeatGrant) {
        if(m.id==255 || m.host!=a.net.self || Generation(m.entity.id)!=m.entity.generation) return true;
        auto* p=LocalPlayer();auto* e=Pointer(m.entity.id);if(!p || !e || !e->IsVehicle() || p->bInVehicle) return true;
        auto* v=static_cast<CVehicle*>(e);if(m.id<1 || m.id>8 || v->pPassengers[m.id-1]) return true;
        a.lastSeatVehicle=m.entity.id;a.lastSeatIndex=m.id;a.lastSeatUntil=vc::NowMs()+8000;
        unsigned door=v->IsBike()?(DotProduct(p->GetPosition()-v->GetPosition(),v->GetRight())>=0?CAR_DOOR_RR:CAR_DOOR_LR):DoorForSeat(m.id);
        p->SetObjective(OBJECTIVE_ENTER_CAR_AS_PASSENGER,v);p->m_vehDoor=door;p->SetEnterCar(v,door);return true;
    }
    if(m.kind==vc::Kind::EntityHit) {
        auto* e=Pointer(m.entity.id);if(!e || !Owns(e) || Generation(m.entity.id)!=m.entity.generation) return true;
        auto* attacker=m.hit.source?Pointer(m.hit.source):m.sender<vc::kMaxPlayers?a.players[m.sender]:nullptr;
        a.applying=true;
        NpcPlayerScope context(static_cast<CPhysical*>(e),attacker);
        if(e->IsPed()) {
            auto* ped=static_cast<CPed*>(e);
            if(attacker && !ped->DyingOrDead()) {
                if(ped->m_nPedType==PEDTYPE_COP && attacker->IsPed() && static_cast<CPed*>(attacker)->IsPlayer()) {
                    static_cast<CPlayerPed*>(attacker)->SetWantedLevelNoDrop(m.hit.weapon>=WEAPONTYPE_COLT45 && m.hit.weapon<=WEAPONTYPE_MINIGUN?2:1);
                    if(ped->GetPedState()==PED_FLEE_ENTITY || ped->GetPedState()==PED_FLEE_POS) ped->ClearFlee();
                    auto* cop=static_cast<CCopPed*>(ped);cop->SetPursuit(true);
                } else ped->ReactToAttack(attacker);
                if(m.hit.weapon>=WEAPONTYPE_COLT45 && m.hit.weapon<=WEAPONTYPE_MINIGUN && ped->IsPedInControl() && !ped->bIsDucking) {
                    ped->ClearAttackByRemovingAnim();
                    auto* hit=CAnimManager::AddAnimation(ped->GetClump(),ASSOCGRP_STD,AnimationId(ANIM_STD_HITBYGUN_FRONT+m.hit.direction));
                    if(hit) {hit->blendAmount=0;hit->blendDelta=8;}
                }
            }
            ped->InflictDamage(attacker,eWeaponType(m.hit.weapon),m.hit.damage,ePedPieceTypes(m.hit.piece),m.hit.direction);
            if(attacker && attacker->IsPed()) {
                bool cop=ped->m_nPedType==PEDTYPE_COP;bool gun=m.hit.weapon>=WEAPONTYPE_COLT45 && m.hit.weapon<=WEAPONTYPE_MINIGUN;
                auto type=gun?(cop?EVENT_SHOOT_COP:EVENT_SHOOT_PED):(cop?EVENT_ASSAULT_POLICE:EVENT_ASSAULT);
                CEventList::RegisterEvent(type,EVENT_ENTITY_PED,ped,static_cast<CPed*>(attacker),1000);
            }
        }
        else if(e->IsVehicle()) {
            auto* v=static_cast<CVehicle*>(e);
            if(attacker) {
                if(v->pDriver && OwnerPed(v->pDriver)) v->pDriver->ReactToAttack(attacker);
                for(auto* ped:v->pPassengers) if(ped && OwnerPed(ped)) ped->ReactToAttack(attacker);
            }
            v->InflictDamage(attacker,eWeaponType(m.hit.weapon),m.hit.damage,e->GetPosition());
        }
        else static_cast<CObject*>(e)->ObjectDamage(m.hit.damage);
        a.applying=false;return true;
    }
    if(m.kind==vc::Kind::VehicleContact) {
        auto* source=Pointer(m.contact.source);auto* target=Pointer(m.contact.target);
        if(!source || !target || !Owns(target) || !target->IsVehicle() || Generation(m.contact.source)!=m.contact.sourceGeneration || Generation(m.contact.target)!=m.contact.targetGeneration) return true;
        if(a.contacts.Resolved(m.contact.target,m.contact.source,vc::NowMs())) return true;
        auto* p=static_cast<CPhysical*>(target);p->m_vecMoveSpeed+=Native(vc::Limited(m.contact.move,30.f))*.02f;p->m_vecTurnSpeed+=Native(vc::Limited(m.contact.turn,12.f))*.02f;return true;
    }
    return false;
}
bool NativeWorld::RouteDamage(CEntity* victim,CEntity* attacker,int weapon,float damage,int piece,uint8_t direction) {
    auto& a=*impl_;if(a.applying || !a.network) return false;
    auto id=Id(victim);if(!id || Owns(victim)) return false;
    if(attacker==LocalPlayer() || (LocalPlayer() && attacker==LocalPlayer()->m_pMyVehicle) || Owns(attacker)) {
        vc::Message hit;hit.kind=vc::Kind::EntityHit;hit.entity.id=id;hit.entity.generation=Generation(id);
        hit.hit.weapon=uint8_t(weapon);hit.hit.damage=std::clamp(damage,0.01f,1000.f);hit.hit.piece=uint8_t(std::clamp(piece,0,6));hit.hit.direction=direction;
        hit.hit.source=Id(attacker);if(attacker==LocalPlayer()) hit.hit.source=0;
        hit.hit.sourceGeneration=Generation(hit.hit.source);hit.hit.sourceLife=a.capture?a.capture(LocalPlayer()).life:1;a.network->SendEvent(hit);
    }
    return true;
}
void NativeWorld::Contact(CPhysical* self,CPhysical* other,const CVector& beforeMove,const CVector& beforeTurn) {
    auto& a=*impl_;if(!self || !other || !self->IsVehicle() || !other->IsVehicle()) return;
    auto source=Id(self),target=Id(other);if(!source || !target || !Owns(self)) return;
    auto now=vc::NowMs();a.contacts.Native(source,target,now);
    if(Owns(other) || now-a.contactSent[{source,target}]<60) return;
    auto move=(other->m_vecMoveSpeed-beforeMove)*50.f,turn=(other->m_vecTurnSpeed-beforeTurn)*50.f;
    if(move.MagnitudeSqr()<.0001f && turn.MagnitudeSqr()<.0001f) return;
    vc::Message m;m.kind=vc::Kind::VehicleContact;m.contact={source,Generation(source),target,Generation(target),vc::Limited(Wire(move),30),vc::Limited(Wire(turn),12)};
    a.network->SendEvent(m);a.contactSent[{source,target}]=now;
}
void NativeWorld::TraceVehicles(std::ostream& out,uint64_t now) const {
    const auto& a=*impl_;std::set<uint32_t> occupied;
    if(auto* local=LocalPlayer();local && local->bInVehicle) occupied.insert(Id(local->m_pMyVehicle));
    for(const auto& p:a.net.players) if(p.active && p.hasState && p.state.vehicle) occupied.insert(p.state.vehicle);
    for(uint32_t id:occupied) {
        auto it=a.records.find(id);if(it==a.records.end() || !it->second.Valid() || it->second.state.kind!=vc::EntityKind::Vehicle) continue;
        const auto& r=it->second;auto* v=static_cast<CVehicle*>(r.pointer);auto p=v->GetPosition(),velocity=v->m_vecMoveSpeed*50.f;
        auto* frame=v->GetClump()?RpClumpGetFrame(v->GetClump()):nullptr;auto rendered=frame?RwFrameGetLTM(frame)->pos:p;
        auto known=a.net.entities.find(id);if(known==a.net.entities.end()) continue;
        out<<now<<','<<unsigned(a.net.self)<<','<<id<<','<<unsigned(r.state.owner)<<','<<r.state.generation<<','<<known->second.sequence<<','<<(now-known->second.received)
           <<','<<p.x<<','<<p.y<<','<<p.z<<','<<rendered.x<<','<<rendered.y<<','<<rendered.z<<','<<velocity.x<<','<<velocity.y<<','<<velocity.z<<','<<v->m_fHealth<<'\n';
    }
}
void NativeWorld::Diagnostics(std::ostream& out) const {
    unsigned temporary=0;auto* objects=CPools::GetObjectPool();for(int i=0;i<objects->GetSize();++i) if(auto* o=objects->GetSlot(i)) if(o->ObjectCreatedBy==TEMP_OBJECT) ++temporary;
    auto& a=*impl_;out<<",\"temporaryObjects\":"<<temporary<<",\"populationOwner\":"<<PopulationOwner()<<",\"entities\":[";bool comma=false;
    for(auto& [id,r]:a.records) if(r.Valid()) {
        if(comma) out<<",";comma=true;auto pos=r.pointer->GetPosition();
        out<<"{\"id\":"<<id<<",\"kind\":"<<int(r.state.kind)<<",\"owner\":"<<unsigned(r.state.owner)<<",\"generation\":"<<r.state.generation<<",\"ageMs\":"<<(vc::NowMs()-a.net.entities.at(id).received)<<",\"model\":"<<r.state.model<<",\"position\":["<<pos.x<<","<<pos.y<<","<<pos.z<<"]";
        out<<",\"playoutDelayMs\":"<<r.track.Delay()*1000;
        if(r.state.kind==vc::EntityKind::Vehicle) {
            auto* v=static_cast<CVehicle*>(r.pointer);auto velocity=v->m_vecMoveSpeed*50.f;
            auto* frame=v->GetClump()?RpClumpGetFrame(v->GetClump()):nullptr;auto rendered=frame?RwFrameGetLTM(frame)->pos:pos;auto forward=v->GetForward();auto received=vc::EntityPosition(r.state);
            out<<",\"renderPosition\":["<<rendered.x<<","<<rendered.y<<","<<rendered.z<<"],\"velocity\":["<<velocity.x<<","<<velocity.y<<","<<velocity.z<<"],\"forward\":["<<forward.x<<","<<forward.y<<","<<forward.z<<"],\"wirePosition\":["<<received.x<<","<<received.y<<","<<received.z<<"],\"sequence\":"<<a.net.entities.at(id).sequence;
            LightDiagnostics(v,out);
            out<<",\"health\":"<<v->m_fHealth<<",\"driver\":"<<(v->pDriver!=nullptr)<<",\"passengers\":"<<int(v->m_nNumPassengers)<<",\"status\":"<<int(v->GetStatus());if(v->IsCar()) {auto* c=static_cast<CAutomobile*>(v);out<<",\"wheel\":"<<c->m_aWheelRotation[0]<<",\"door\":"<<int(c->Damage.m_doorStatus[0])<<",\"panels\":"<<c->Damage.m_panelStatus;out<<",\"steer\":"<<v->m_fSteerAngle<<",\"gas\":"<<v->m_fGasPedal<<",\"lights\":"<<c->Damage.m_lightStatus;}}
        if(r.state.kind==vc::EntityKind::Ped) {
            auto* ped=static_cast<CPed*>(r.pointer);auto sampled=a.capture(ped);out<<" ,\"objective\":"<<unsigned(ped->m_objective)<<",\"waitState\":"<<unsigned(ped->m_nWaitState)<<",\"threatFlags\":"<<ped->m_threatFlags<<",\"threatPlayer\":"<<PlayerId(ped->m_threatEntity)<<",\"fleePlayer\":"<<PlayerId(ped->m_fleeFrom)<<",\"objectivePlayer\":"<<PlayerId(ped->m_pedInObjective)<<",\"lookPlayer\":"<<PlayerId(ped->m_pLookTarget)<<",\"type\":"<<unsigned(ped->m_nPedType);out<<",\"health\":"<<ped->m_fHealth<<",\"armour\":"<<ped->m_fArmour;out<<",\"lastRenderAgoMs\":"<<(r.renderedAt?vc::NowMs()-r.renderedAt:999999);
            auto rendered=RwFrameGetMatrix(RpClumpGetFrame(ped->GetClump()))->pos;
            out<<",\"vehicle\":"<<Id(ped->m_pMyVehicle)<<",\"inVehicle\":"<<ped->bInVehicle<<",\"pedState\":"<<unsigned(ped->GetPedState())<<",\"visible\":"<<ped->bIsVisible<<",\"alpha\":"<<CVisibilityPlugins::GetClumpAlpha(ped->GetClump())<<",\"offscreen\":"<<ped->bOffscreen<<",\"moving\":"<<(ped->m_movingListNode!=nullptr)<<",\"netAnimations\":"<<unsigned(r.state.ped.animationCount)<<",\"animations\":[";
            for(unsigned i=0;i<sampled.animationCount;++i) {if(i) out<<",";auto& anim=sampled.animations[i];out<<"["<<anim.group<<","<<anim.id<<","<<anim.blend<<","<<anim.time<<"]";}
            out<<"],\"renderPosition\":["<<rendered.x<<","<<rendered.y<<","<<rendered.z<<"],\"bones\":[";
            bool boneComma=false;
            for(unsigned tag:{0u,1u,2u,3u,22u,32u,42u,52u}) if(auto* frame=RpAnimBlendClumpFindBone(ped->GetClump(),tag);frame && frame->hanimFrame) {
                if(boneComma) out<<",";boneComma=true;auto q=frame->hanimFrame->q;auto t=frame->hanimFrame->t;
                out<<"["<<tag<<","<<q.imag.x<<","<<q.imag.y<<","<<q.imag.z<<","<<q.real<<","<<t.x<<","<<t.y<<","<<t.z<<"]";
            }
            out<<"],\"hierarchies\":[";bool assocComma=false;
            for(auto* n=RpAnimBlendClumpGetFirstAssociation(ped->GetClump());n;n=RpAnimBlendGetNextAssociation(n)) if(n->blendAmount>.001f) {
                if(assocComma) out<<",";assocComma=true;int nodes=0;for(int i=0;i<n->numNodes;++i) if(n->nodes[i].sequence) ++nodes;
                out<<"["<<n->groupId<<","<<n->animId<<","<<n->numNodes<<","<<nodes<<","<<(n->hierarchy->sequences!=nullptr)<<","<<n->flags<<"]";
            }
            out<<"]";
        }
        if(r.state.kind==vc::EntityKind::Object) {
            auto* object=static_cast<CObject*>(r.pointer);unsigned copies=0,dummies=0;
            auto* op=CPools::GetObjectPool();for(int i=0;i<op->GetSize();++i) if(auto* o=op->GetSlot(i)) if(o->GetModelIndex()==r.state.model && Distance(Wire(o->m_objectMatrix.GetPosition()),r.state.origin)<.01f) ++copies;
            auto* dp=CPools::GetDummyPool();for(int i=0;i<dp->GetSize();++i) if(auto* d=dp->GetSlot(i)) if(d->GetModelIndex()==r.state.model && Distance(Wire(d->GetPosition()),r.state.origin)<.01f) ++dummies;
            auto origin=r.state.origin;
            out<<",\"origin\":["<<origin.x<<","<<origin.y<<","<<origin.z<<"],\"mapCopies\":"<<copies<<",\"dummyCopies\":"<<dummies<<",\"visible\":"<<object->bIsVisible<<",\"collision\":"<<object->bUsesCollision<<",\"damaged\":"<<object->bRenderDamaged<<",\"damageEffect\":"<<unsigned(object->m_nCollisionDamageEffect)<<",\"flags\":"<<unsigned(r.state.object.flags);
        }
        out<<"}";
    }
    out<<"]";
}
CVehicle* NativeWorld::SpawnTestVehicle(int model,float x,float y,float z) {
    if(!PopulationOwner() || !InWorld({x,y,z}) || model<130 || model>236) return nullptr;
    CStreaming::RequestModel(model,STREAMFLAGS_DONT_REMOVE);CStreaming::LoadAllRequestedModels(false);
    if(!CStreaming::HasModelLoaded(model)) return nullptr;
    CStreaming::LoadSceneCollision(CVector(x,y,z));bool found=false;
    float ground=CWorld::FindGroundZFor3DCoord(x,y,z+4,&found);
    if(!found || std::abs(ground-z)>5) return nullptr;
    auto* v=NewVehicle(model);if(!v) return nullptr;
    v->SetPosition(CVector(x,y,ground+.8f));
    if(v->IsCar()) static_cast<CAutomobile*>(v)->PlaceOnRoadProperly();
    const auto& box=v->GetColModel()->boundingBox;
    const int nx=std::clamp(int(std::ceil((box.max.x-box.min.x)/.5f)),1,16);
    const int ny=std::clamp(int(std::ceil((box.max.y-box.min.y)/.5f)),1,24);
    // Test a grid through the actual model's cabin/footprint before adding it to
    // the world. A test car must never begin intersecting a building or vehicle.
    for(int ix=0;ix<=nx;++ix) for(int iy=0;iy<=ny;++iy) for(float height:{.45f,.8f}) {
        CVector local(std::lerp(box.min.x,box.max.x,float(ix)/nx),std::lerp(box.min.y,box.max.y,float(iy)/ny),std::lerp(box.min.z,box.max.z,height));
        if(CWorld::TestSphereAgainstWorld(v->GetMatrix()*local,.25f,v,true,true,false,true,false,false)) {delete v;return nullptr;}
    }
    v->SetStatus(STATUS_ABANDONED);v->bIsLocked=false;CWorld::Add(v);return v;
}
bool NativeWorld::TestVehiclePose(uint32_t id,float x,float y,float z,float heading) {
    auto* e=Pointer(id);if(!e || !e->IsVehicle() || !Owns(e) || !InWorld({x,y,z}) || !std::isfinite(heading)) return false;
    auto* v=static_cast<CVehicle*>(e);v->Remove();v->SetHeading(heading);v->SetPosition(CVector(x,y,z));
    v->m_vecMoveSpeed=CVector(0,0,0);v->m_vecTurnSpeed=CVector(0,0,0);
    v->GetMatrix().UpdateRW();v->UpdateRwFrame();v->Add();return true;
}
bool NativeWorld::TestEnter(uint32_t id,unsigned seat,bool warp) {
    auto* e=Pointer(id);auto* p=LocalPlayer();if(!e || !e->IsVehicle() || !p || seat>8) return false;
    auto* v=static_cast<CVehicle*>(e);
    if(warp) {if(seat==0 && !Owns(v)) return false;vc::PlayerState s;s.vehicle=id;s.flags=vc::kReady|vc::kInVehicle;s.seat=uint8_t(seat);s.door=uint8_t(DoorForSeat(seat));s.pedState=PED_DRIVING;
        if(p->bInVehicle) Detach(p);
        p->m_pMyVehicle=v;v->RegisterReference(reinterpret_cast<CEntity**>(&p->m_pMyVehicle));p->bInVehicle=true;p->bUsesCollision=false;p->SetPedState(PED_DRIVING);
        if(seat==0) {v->SetDriver(p);v->SetStatus(STATUS_PLAYER);} else if(!v->pPassengers[seat-1]) {v->pPassengers[seat-1]=p;++v->m_nNumPassengers;}
        p->m_vehDoor=DoorForSeat(seat);p->SetPedPositionInCar();return true;
    }
    if(seat) return Passenger();
    p->m_vehDoor=CAR_DOOR_LF;p->SetObjective(OBJECTIVE_ENTER_CAR_AS_DRIVER,v);p->SetEnterCar(v,CAR_DOOR_LF);return true;
}
}

namespace vccoop {
bool NativeWorld::BoneState(CPed* p,vc::PlayerState& state) const {
    auto id=Id(p);if(!id || Owns(p)) return false;
    const auto& r=impl_->records.at(id);if(r.state.kind!=vc::EntityKind::Ped) return false;
    state=r.track.At(ReplicaTime()-.1).ped;return true;
}
void NativeWorld::Projectile(CObject* p) {
    auto& a=*impl_;if(!p || !a.network || Id(p)) return;
    Impl::Record temporary;temporary.state.kind=vc::EntityKind::Object;temporary.state.model=p->GetModelIndex();temporary.pointer=p;temporary.handle=Handle(vc::EntityKind::Object,p);
    vc::Entity e;e.kind=vc::EntityKind::Object;e.key=Key(e.kind,temporary.handle);e.origin=Wire(p->GetPosition());e.object.flags=64;
    if(!a.Capture(temporary,e)) return;
    vc::Message message;message.kind=vc::Kind::EntityCreate;message.entity=e;
    if(a.network->SendEntity(message)) a.pending[e.key]=vc::NowMs();
}
}

namespace vccoop { void NativeWorld::Rendered(CPed* ped) {auto id=Id(ped);if(id) impl_->records.at(id).renderedAt=vc::NowMs();} }
