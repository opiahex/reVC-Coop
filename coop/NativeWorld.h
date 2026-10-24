#pragma once
#include "net/BackgroundClient.h"
#include "common/WorldInterpolation.h"
#include "common/VehicleMotion.h"
#include "common/VehicleContact.h"
#include <memory>
#include <functional>
#include <ostream>
class CEntity; class CPhysical; class CPed; class CVehicle; class CVector; class CObject;
namespace vccoop {
using CapturePedFn=vc::PlayerState(*)(CPed*);
using AnimatePedFn=void(*)(CPed*,const vc::PlayerState&);
class NativeWorld {
    struct Impl; std::unique_ptr<Impl> impl_;
public:
    NativeWorld(); ~NativeWorld();
    std::function<void(const std::string&)> log;
    void Begin(const vc::BackgroundClient::Snapshot&,vc::BackgroundClient&,const std::array<CPed*,vc::kMaxPlayers>&,CapturePedFn,AnimatePedFn);
    void After(vc::PlayerState&);
    void Reconcile();
    void Clear();
    bool PopulationOwner() const;
    bool Hold(CPhysical*) const;
    bool ReplicaPed(CPed*) const;
    bool Owns(CEntity*) const;
    uint32_t Id(CEntity*) const;
    uint32_t Generation(uint32_t) const;
    CEntity* Pointer(uint32_t) const;
    void Attach(CPed*,const vc::PlayerState&);
    void DescribeVehicle(CPed*,vc::PlayerState&) const;
    bool CanEnter(CPed*,CVehicle*);
    void CancelLocalEntry();
    bool Passenger();
    bool Event(const vc::Message&);
    bool RouteDamage(CEntity*,CEntity*,int,float,int,uint8_t);
    void Contact(CPhysical*,CPhysical*,const CVector&,const CVector&);
    void Diagnostics(std::ostream&) const;
    void TraceVehicles(std::ostream&,uint64_t) const;
    void Rendered(CPed*);
    bool BoneState(CPed*,vc::PlayerState&) const;
    void Projectile(CObject*);
    CVehicle* SpawnTestVehicle(int,float,float,float);
    bool TestEnter(uint32_t,unsigned,bool);
    bool TestVehiclePose(uint32_t,float,float,float,float);
};
}
