#include "common.h"
#undef lerp
#include "Coop.h"
#include "common/FixedStep.h"
#include "common/WorldInterpolation.h"
#include "Pools.h"
#include "World.h"
#include "main.h"
#include "Camera.h"
#include "PlayerPed.h"
#include "Object.h"
#include "Timer.h"
#include "CutsceneMgr.h"
#include "Frontend.h"
#include "ModelInfo.h"
#include "Automobile.h"
#include "Coronas.h"
#include "common/Protocol.h"
#include <chrono>
#include <map>
#include <vector>
namespace vccoop {
namespace {
vc::FixedStep clock;
bool fixed{},cameraKnown{};
CMatrix oldCamera,currentCamera;
struct Pose {CPhysical* pointer{};CMatrix previous,current;};
std::map<uint32_t,Pose> poses;
struct Saved {uint32_t key;CPhysical* pointer;CMatrix matrix;};
std::vector<Saved> saved;
CMatrix savedCamera;CVector savedGameCamera;
double renderTime{};
double RealTime() {return double(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())*.001;}
uint32_t Key(unsigned kind,int handle) {return (kind<<28)|uint32_t(handle);}
CPhysical* Resolve(uint32_t key) {
    auto handle=int(key&0x0FFFFFFF);
    switch(key>>28) {case 1:return CPools::GetPed(handle);case 2:return CPools::GetVehicle(handle);case 3:return CPools::GetObject(handle);}
    return nullptr;
}
template<class Fn> void Each(Fn fn) {
    auto* pp=CPools::GetPedPool();if(pp) for(int i=0;i<pp->GetSize();++i) if(auto* p=pp->GetSlot(i)) {
        if(ReplicaPed(p) || (p->bInVehicle && p->m_pMyVehicle && RemoteVehicle(p->m_pMyVehicle))) continue;
        fn(Key(1,CPools::GetPedRef(p)),p);
    }
    auto* vp=CPools::GetVehiclePool();if(vp) for(int i=0;i<vp->GetSize();++i) if(auto* p=vp->GetSlot(i)) if(!HoldReplica(p)) fn(Key(2,CPools::GetVehicleRef(p)),p);
    auto* op=CPools::GetObjectPool();if(op) for(int i=0;i<op->GetSize();++i) if(auto* p=op->GetSlot(i)) if(!HoldReplica(p)) fn(Key(3,CPools::GetObjectRef(p)),p);
}
vc::Transform Sample(const CMatrix& m) {
    auto v=[](const CVector& p){return vc::Vec3{p.x,p.y,p.z};};
    vc::Transform t;t.position=v(m.GetPosition());t.right=v(m.GetRight());t.forward=v(m.GetForward());t.up=v(m.GetUp());return t;
}
CMatrix Blend(const CMatrix& a,const CMatrix& b,float alpha) {
    if((a.GetPosition()-b.GetPosition()).MagnitudeSqr()>625) return b;
    if(!vc::ValidTransform(Sample(a)) || !vc::ValidTransform(Sample(b))) return b;
    auto t=vc::BlendTransform(Sample(a),Sample(b),alpha);
    CMatrix m;m.SetUnity();m.GetPosition()=CVector(t.position.x,t.position.y,t.position.z);m.GetRight()=CVector(t.right.x,t.right.y,t.right.z);m.GetForward()=CVector(t.forward.x,t.forward.y,t.forward.z);m.GetUp()=CVector(t.up.x,t.up.y,t.up.z);return m;
}
CMatrix BlendCamera(const CMatrix& a,const CMatrix& b,float alpha) {
    // CCamera stores Left/Forward/Up, whereas physical entities store Right.
    // Convert the reflected camera basis before quaternion interpolation.
    CMatrix x=a,y=b;x.GetRight()*=-1.f;y.GetRight()*=-1.f;
    auto m=Blend(x,y,alpha);m.GetRight()*=-1.f;return m;
}
void CommitCamera() {
    TheCamera.CalculateDerivedValues();TheCamera.GetGameCamPosition()=TheCamera.GetPosition();
    auto* frame=RwCameraGetFrame(Scene.camera);auto* matrix=RwFrameGetMatrix(frame);
    matrix->pos=TheCamera.GetPosition();matrix->right=TheCamera.GetRight();matrix->up=TheCamera.GetUp();matrix->at=TheCamera.GetForward();RwMatrixUpdate(matrix);RwFrameUpdateObjects(frame);
}
void MoveCarLights() {
    auto* pool=CPools::GetVehiclePool();if(!pool) return;
    for(int i=0;i<pool->GetSize();++i) if(auto* car=pool->GetSlot(i);car && car->IsCar()) {
        auto* info=static_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(car->GetModelIndex()));
        auto head=info->m_positions[CAR_POS_HEADLIGHTS],tail=info->m_positions[CAR_POS_TAILLIGHTS];
        const auto hr=car->GetMatrix()*head,hl=hr-car->GetRight()*(2.f*head.x);
        const auto tr=car->GetMatrix()*tail,tl=tr-car->GetRight()*(2.f*tail.x);
        // The near-front flares (+6/+7) can still be fading after the car
        // passes the camera. Keep them attached even when not registered anew.
        for(unsigned n:{0u,6u}) CCoronas::Move(uintptr(car)+n,hl);
        for(unsigned n:{1u,7u}) CCoronas::Move(uintptr(car)+n,hr);
        CCoronas::Move(uintptr(car)+14,tl);CCoronas::Move(uintptr(car)+15,tr);
    }
}
void AlignOccupants() {
    auto* pool=CPools::GetPedPool();if(!pool) return;
    for(int i=0;i<pool->GetSize();++i) if(auto* ped=pool->GetSlot(i);ped && ped->bInVehicle && ped->m_pMyVehicle && ped->GetPedState()==PED_DRIVING) {
        const auto vehicleKey=Key(2,CPools::GetVehicleRef(ped->m_pMyVehicle));
        const auto vehicle=std::find_if(saved.begin(),saved.end(),[&](const Saved& s){return s.key==vehicleKey;});
        if(vehicle==saved.end()) continue;
        const CMatrix parent=vehicle->matrix;
        const auto key=Key(1,CPools::GetPedRef(ped));
        auto original=std::find_if(saved.begin(),saved.end(),[&](const Saved& s){return s.key==key;});
        const CMatrix originalPose=original==saved.end()?ped->GetMatrix():original->matrix;
        if(original==saved.end()) saved.push_back({key,ped,originalPose});
        // Vehicle and occupants must use the same interpolated parent pose.
        // A separately interpolated world position cuts across turns, while an
        // unmodified passenger remains one physics tick ahead of the body.
        // Apply the parent's render delta without running the stateful seat
        // routine again (it changes heading, entry flags and bike lean caches).
        ped->GetMatrix()=ped->m_pMyVehicle->GetMatrix()*(Invert(parent)*originalPose);
        ped->UpdateRwFrame();ped->UpdateRpHAnim();
    }
}
}
double ReplicaTime() {return renderTime?renderTime:RealTime();}
bool FixedSimulation() {return fixed;}
unsigned SimulationSteps() {
    auto now=std::chrono::steady_clock::now();static auto last=now;
    double elapsed=std::chrono::duration<double>(now-last).count();last=now;
    fixed=Active() && !FrontEndMenuManager.m_bGameNotLoaded;
    auto steps=clock.Advance(elapsed,fixed);
    if(!fixed) {poses.clear();cameraKnown=false;}
    return steps;
}
void BeforeSimulationStep() {
    if(!fixed) return;
    poses.clear();
    Each([](auto key,auto* p){auto& pose=poses[key];pose.pointer=p;pose.previous=p->GetMatrix();pose.current=pose.previous;});
    oldCamera=TheCamera.GetMatrix();
}
void AfterSimulationStep() {
    if(!fixed) return;
    Each([](auto key,auto* p){auto it=poses.find(key);if(it!=poses.end() && it->second.pointer==p) it->second.current=p->GetMatrix();});
    currentCamera=TheCamera.GetMatrix();cameraKnown=true;
}
InterpolatedFrame::InterpolatedFrame() {
    renderTime=RealTime();
    RefreshReplicas();
    enabled_=fixed && cameraKnown && !FrontEndMenuManager.m_bMenuActive && !CCutsceneMgr::IsCutsceneProcessing();
    if(!enabled_) {if(Active()) MoveCarLights();return;}
    auto alpha=clock.Alpha();saved.clear();
    for(auto& [key,pose]:poses) if(Resolve(key)==pose.pointer && !HoldReplica(pose.pointer)) {
        auto* p=pose.pointer;saved.push_back({key,p,p->GetMatrix()});p->GetMatrix()=Blend(pose.previous,pose.current,alpha);p->UpdateRwFrame();
    }
    savedCamera=TheCamera.GetMatrix();savedGameCamera=TheCamera.GetGameCamPosition();TheCamera.GetMatrix()=BlendCamera(oldCamera,currentCamera,alpha);CommitCamera();
    AlignOccupants();
    MoveCarLights();
}
InterpolatedFrame::~InterpolatedFrame() {
    renderTime=0;
    if(!enabled_) return;
    for(auto& s:saved) if(Resolve(s.key)==s.pointer) {s.pointer->GetMatrix()=s.matrix;s.pointer->UpdateRwFrame();if(s.pointer->IsPed()) static_cast<CPed*>(s.pointer)->UpdateRpHAnim();}
    saved.clear();TheCamera.GetMatrix()=savedCamera;CommitCamera();TheCamera.GetGameCamPosition()=savedGameCamera;
}
}
