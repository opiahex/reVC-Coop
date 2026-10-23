#pragma once
#include <stdint.h>
class CEntity; class CPhysical; class CPed; class CVehicle; class CVector; class CObject;
namespace vccoop {
void FrontendTick();
void BeforeProcess();
void AfterProcess();
void AfterWorld();
void Render();
void WorldReset();
void Shutdown();
void InstallDiagnostics();
bool CaptureNativeFrame(void*,const char* path="vc-coop-frame.bmp");
bool Active();
bool CapturingInput();
bool PopulationOwner();
bool HoldReplica(CPhysical*);
bool ReplicaPed(CPed*);
bool CanEnterVehicle(CPed*,CVehicle*);
bool VehicleOccupants(CEntity*,CEntity*);
void ApplyRemoteBones(CPed*);
void BeforePedRender(CPed*);
void RecordExplosion(CEntity*,CEntity*,int,const CVector&,uint32_t,bool);
void ShareProjectile(CObject*);
bool ExplosionTarget(CPhysical*);
bool RemoteVehicle(CVehicle*);
class ExplosionDamageScope {
    bool previous_;
public: ExplosionDamageScope(); ~ExplosionDamageScope();
};
bool RoutePedDamage(CPed*,CEntity*,int,float,int,uint8_t);
bool RouteVehicleDamage(CVehicle*,CEntity*,int,float);
bool RouteObjectDamage(CEntity*,float);
void RecordShot(CEntity*,CEntity*,int,const CVector&,const CVector&,const CVector&,int,int,uint8_t options=0);
class CombatScope {
    CEntity* previous_;
public:
    explicit CombatScope(CEntity*);
    ~CombatScope();
};
void CollisionContact(CPhysical*,CPhysical*,const CVector&,const CVector&);
bool Sandbox();
bool WindowMessage(void*,unsigned,uintptr_t,intptr_t);
}
