#include "NativePickups.h"
#include "common.h"
#undef lerp
#include "Coop.h"
#include "Pickups.h"
#include "PlayerPed.h"
#include "PlayerInfo.h"
#include "World.h"
#include "Streaming.h"
#include "ModelInfo.h"
#include "WeaponInfo.h"
#include "Object.h"
#include <set>
#include <ostream>
namespace vccoop {
namespace {
bool Shared(const CPickup* p) {return p && vc::SharedPickupType(p->m_eType) && vc::PickupModel(uint16_t(p->m_eModelIndex));}
int Slot(CPickup* p) {for(int i=0;i<NUMPICKUPS;++i) if(&CPickups::aPickUps[i]==p) return i;return -1;}
int Handle(int i) {return int(uint32_t(i)|(uint32_t(CPickups::aPickUps[i].m_nIndex)<<16));}
CPickup* Resolve(int h) {int i=CPickups::GetActualPickupIndex(h);return i<0?nullptr:&CPickups::aPickUps[i];}
bool Match(const CPickup& p,const vc::Pickup& q) {return p.m_eType==q.type && p.m_eModelIndex==q.model && (p.m_vecPos-CVector(q.position.x,q.position.y,q.position.z)).MagnitudeSqr()<.015f;}
}
bool NativePickups::Managed(CPickup* p) const {return !applying_ && Active() && Shared(p) && Slot(p)>=0;}
bool NativePickups::Request(CPickup* p,bool ammo) {
    if(!Managed(p)) return true;
    for(auto& [id,b]:bindings_) if(Resolve(b.handle)==p) {
        auto it=states_.find(id);if(it==states_.end() || it->second.status || !network_) return false;
        if(ammo && p->m_bWasAmmoCollected && !p->m_nQuantity) return false;
        auto now=vc::NowMs();if(now-requested_[id]<500) return false;
        vc::Message request;request.kind=vc::Kind::PickupRequest;request.pickup=it->second;request.id=ammo?1:0;
        request.hit.life=net_.players[net_.self].state.life;
        if(network_->SendEvent(request)) {requested_[id]=now;++requests_;}return false;
    }
    return false;
}
void NativePickups::Created(CPickup* p) {
    if(applying_ || !Active() || !Shared(p)) return;int slot=Slot(p);if(slot<0) return;
    uint32_t generation=0,id=PickupSource(generation);origins_[uint32_t(Handle(slot))]={id,generation};
}
void NativePickups::Removing(CPickup* p) {
    if(!Managed(p) || !network_) return;
    for(auto& [id,b]:bindings_) if(Resolve(b.handle)==p) {
        auto it=states_.find(id);if(it==states_.end() || it->second.status || it->second.creator!=net_.self || it->second.creatorEpoch!=net_.players[net_.self].epoch) return;
        vc::Message remove;remove.kind=vc::Kind::PickupRemove;remove.pickup=it->second;remove.id=0;remove.hit.life=net_.players[net_.self].state.life;network_->SendEvent(remove);return;
    }
}
void NativePickups::Event(const vc::Message& m) {if(m.kind==vc::Kind::PickupGrant && grants_.size()<64) grants_.push_back(m);}
void NativePickups::Clear() {
    applying_=true;
    for(auto& [id,b]:bindings_) if(b.allocated && Resolve(b.handle)) CPickups::RemovePickUp(b.handle);
    bindings_.clear();origins_.clear();announced_.clear();requested_.clear();grants_.clear();states_.clear();network_=nullptr;scanAt_=0;applying_=false;
}
void NativePickups::Tick(const vc::BackgroundClient::Snapshot& net,vc::BackgroundClient& network) {
    if(net.self>=vc::kMaxPlayers || !LocalPlayer()) return;
    net_=net;network_=&network;states_=net.pickups;auto now=vc::NowMs();applying_=true;
    for(auto it=bindings_.begin();it!=bindings_.end();) if(!states_.contains(it->first)) {
        if(it->second.allocated && Resolve(it->second.handle)) CPickups::RemovePickUp(it->second.handle);
        it=bindings_.erase(it);
    } else ++it;
    // Bind matching native originals; only missing supplies are instantiated.
    for(const auto& [id,q]:states_) {
        auto& b=bindings_[id];auto* p=Resolve(b.handle);
        if(p && (!p->m_eType || p->m_eModelIndex!=q.model)) p=nullptr;
        if(!p) {
            for(int i=0;i<NUMPICKUPS;++i) if(Match(CPickups::aPickUps[i],q)) {
                bool used=false;for(const auto& [other,old]:bindings_) if(other!=id && old.handle==Handle(i)) used=true;
                if(!used) {b.handle=Handle(i);p=&CPickups::aPickUps[i];break;}
            }
        }
        if(!p && !q.status && (q.area==13 || q.area==LocalPlayer()->m_area) && (LocalPlayer()->GetPosition()-CVector(q.position.x,q.position.y,q.position.z)).MagnitudeSqr()<220*220) {
            CStreaming::RequestModel(q.model,STREAMFLAGS_DEPENDENCY);
            if(CStreaming::HasModelLoaded(q.model)) {
                b.handle=CPickups::GenerateNewOne(CVector(q.position.x,q.position.y,q.position.z),q.model,q.type,q.quantity,q.rate,(q.flags&1)!=0);
                b.allocated=true;p=Resolve(b.handle);
            }
        }
        if(!p) continue;
        for(int i=0;i<NUMPICKUPS;++i) if(&CPickups::aPickUps[i]!=p && Match(CPickups::aPickUps[i],q)) {
            bool used=false;for(const auto& [other,binding]:bindings_) if(other!=id && binding.handle==Handle(i)) used=true;
            if(!used) CPickups::RemovePickUp(Handle(i));
        }
        if(b.revision!=q.revision) {
            // Notify a script about collection without giving everyone the reward.
            if((q.flags&2) && b.revision && q.takenBy!=vc::kNoPlayer) {
                CPickups::AddToCollectedPickupsArray(b.handle&0xffff);
                if(q.type==PICKUP_COLLECTABLE1 && q.takenBy!=net.self) ++CWorld::Players[CWorld::PlayerInFocus].m_nCollectedPackages;
            }
            b.revision=q.revision;
        }
        p->m_nQuantity=q.quantity;p->m_bWasAmmoCollected=(q.flags&1)!=0;
        if(p->m_pObject) p->m_pObject->bAmmoCollected=p->m_bWasAmmoCollected;
        if(q.status) {p->GetRidOfObjects();p->m_bRemoved=true;if(q.status==2) p->m_eType=PICKUP_NONE;}
        else {p->m_bRemoved=false;if(p->m_pObject) p->m_pObject->m_area=q.area;}
    }
    applying_=false;
    // Award only server-committed claims, once. Native effects/inventory run in
    // the recipient's player slot; the temporary pickup never enters replication.
    for(auto it=grants_.begin();it!=grants_.end();) {
        auto& m=*it;const auto& q=m.pickup;
        if(LocalPlayer()->DyingOrDead() || m.hit.targetEpoch!=net.players[net.self].epoch || m.hit.life!=net.players[net.self].state.life) {it=grants_.erase(it);continue;}
        CStreaming::RequestModel(q.model,STREAMFLAGS_DEPENDENCY);
        if(!CStreaming::HasModelLoaded(q.model)) {++it;continue;}
        auto* model=CModelInfo::GetModelInfo(q.model);
        if(model && model->GetModelType()==MITYPE_WEAPON && !EnsureWeapon(CPickups::WeaponForModel(q.model))) {++it;continue;}
        applying_=true;CPickup gift{};gift.m_eType=q.type;gift.m_eModelIndex=q.model;gift.m_nQuantity=q.quantity;gift.m_bWasAmmoCollected=(q.flags&1)!=0;
        gift.m_vecPos=CVector(q.position.x,q.position.y,q.position.z);gift.m_nTimer=CTimer::GetTimeInMilliseconds()+120000;
        gift.GiveUsAPickUpObject(&gift.m_pObject,&gift.m_pExtraObject,-1,-1);
        if(gift.m_pObject) {
            if(m.id==1) gift.ExtractAmmoFromPickup(LocalPlayer());
            else gift.Update(LocalPlayer(),FindPlayerVehicle(),CWorld::PlayerInFocus,true);
            gift.GetRidOfObjects();++awarded_;it=grants_.erase(it);
        } else ++it;
        applying_=false;
    }
    if(now-scanAt_<100) return;scanAt_=now;
    unsigned sent=0;std::set<int> bound;
    for(const auto& [id,b]:bindings_) if(Resolve(b.handle)) bound.insert(b.handle);
    for(int i=0;i<NUMPICKUPS && sent<24;++i) {
        auto& p=CPickups::aPickUps[i];int key=Handle(i);
        if(!Shared(&p) || p.m_bRemoved || bound.contains(key) || now-announced_[uint32_t(key)]<1000) continue;
        // Native random loot is generated only by its AI owner. Static scripts
        // in both games match a canonical model/position instead of cloning it.
        auto origin=origins_.find(uint32_t(key));bool ownedDrop=origin!=origins_.end() && origin->second.first;
        bool personalDrop=(p.m_eType==4 || p.m_eType==5 || p.m_eType==8) && (p.m_vecPos-LocalPlayer()->GetPosition()).MagnitudeSqr()<64;
        if(!PopulationOwner() && net.self!=net.host && !ownedDrop && !personalDrop) continue;
        vc::Message create;create.kind=vc::Kind::PickupCreate;auto& q=create.pickup;
        if(ownedDrop) {q.source=origin->second.first;q.sourceGeneration=origin->second.second;}
        q.key=uint32_t(key);q.type=p.m_eType;q.model=p.m_eModelIndex;q.quantity=p.m_nQuantity;q.rate=p.m_nMoneySpeed;q.flags=p.m_bWasAmmoCollected?1:0;
        q.position={p.m_vecPos.x,p.m_vecPos.y,p.m_vecPos.z};q.area=p.m_pObject?p.m_pObject->m_area:LocalPlayer()->m_area;
        if(q.type==4 || q.type==5 || q.type==8) q.remaining=std::min(p.m_nTimer>CTimer::GetTimeInMilliseconds()?p.m_nTimer-CTimer::GetTimeInMilliseconds():1u,120000u);
        if(vc::ValidPickup(q) && network.SendEvent(create)) {announced_[uint32_t(key)]=now;++sent;}
    }
}
void NativePickups::Diagnostics(std::ostream& out) const {
    out<<",\"pickupAwards\":"<<awarded_<<",\"pickupRequests\":"<<requests_<<",\"pickups\":[";bool comma=false;
    for(const auto& [id,p]:states_) {if(comma) out<<",";comma=true;auto it=bindings_.find(id);auto* native=it==bindings_.end()?nullptr:Resolve(it->second.handle);
        out<<"{\"id\":"<<id<<",\"revision\":"<<p.revision<<",\"model\":"<<p.model<<",\"type\":"<<unsigned(p.type)<<",\"status\":"<<unsigned(p.status)<<",\"visible\":"<<(native && native->m_pObject && native->m_pObject->bIsVisible)<<",\"quantity\":"<<p.quantity<<",\"position\":["<<p.position.x<<","<<p.position.y<<","<<p.position.z<<"]}";
    }out<<"]";
}
}
