#pragma once
#include <stdint.h>
class CPickup;
class CPlayerPed; class CPlayerInfo; class CWanted;
class CEntity; class CPhysical; class CPed; class CVehicle; class CVector; class CObject;
namespace vccoop {
CPlayerPed* LocalPlayer();
CPlayerPed* FocusPlayer();
CPlayerInfo* RemotePlayerInfo(CPlayerPed*);
unsigned PlayerId(CEntity*);
bool OwnerPed(CPed*);
// Only native NPC AI observes this context. Local input/camera and world slots remain unchanged.
class NpcPlayerScope {
    CPlayerPed* previous_;CPhysical* witness_;
public: explicit NpcPlayerScope(CPhysical*,CEntity* aggressor=nullptr);~NpcPlayerScope();
};
bool RemoteWanted(CWanted*);
uint32_t CrimeIdentity(CEntity*);
bool RouteCrime(CWanted*,int,const CVector&,uint32_t,bool,bool);
bool RouteWanted(CWanted*,int);
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
bool PoliceOwner();
bool EnsureWeapon(int);
bool ManagedPickup(CPickup*);
bool CollectPickup(CPickup*,bool ammo=false);
void PickupRemoving(CPickup*);
void PickupCreated(CPickup*);
uint32_t PickupSource(uint32_t& generation);
CPlayerPed* PoliceTarget();
CPlayerPed* PopulationTarget();
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
bool ApplyEnvironment();
bool TrackedObject(CObject*);
bool FixedSimulation();
unsigned SimulationSteps();
void BeforeSimulationStep();
void AfterSimulationStep();
void RefreshReplicas();
double ReplicaTime();
class InterpolatedFrame {
    bool enabled_{};
public: InterpolatedFrame();~InterpolatedFrame();
};
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
