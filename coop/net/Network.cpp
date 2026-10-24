#include "Network.h"
#include "common/PopulationPolicy.h"
#include <algorithm>
#include <memory>
#include <set>

namespace vc {
namespace {
void ConfigurePeer(ENetPeer* peer) {
    enet_peer_timeout(peer,32,3000,10000);
    // Native asset loading can briefly increase RTT without saturating the
    // link. The stock 5 s minimum/slow recovery discarded entire streams of
    // unreliable NPC updates on localhost. Re-evaluate that minimum sooner and
    // recover on frequent acknowledgements while retaining ENet's throttling.
    enet_peer_ping_interval(peer,100);
    enet_peer_throttle_configure(peer,1000,8,1);
}
}
bool SendPacket(ENetPeer* peer,const Message& m,uint8_t channel,bool reliable) {
    auto bytes=Encode(m);
    auto* packet=enet_packet_create(bytes.data(),bytes.size(),reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet) return false;
    if (enet_peer_send(peer,channel,packet)!=0) { enet_packet_destroy(packet); return false; }
    return true;
}
size_t Server::PlayerCount() const {
    return size_t(std::count_if(peers_.begin(),peers_.end(),[](auto& p){ return p.second.id!=kNoPlayer; }));
}
bool Server::Start(uint16_t port,const std::string& bind) {
    Stop(); ENetAddress addr{}; addr.port=port;
    if (bind!="0.0.0.0" && enet_address_set_host_ip(&addr,bind.c_str())!=0) return false;
    socket_=enet_host_create(&addr,16,kChannels,0,0);
    if (!socket_) return false;
    socket_->maximumPacketSize=kMaxPacket; socket_->maximumWaitingData=2*1024*1024;
    if (log) log("listening on "+bind+":"+std::to_string(socket_->address.port));
    return true;
}
void Server::Stop() {
    if (socket_) enet_host_destroy(socket_);
    hasEnvironment_=false;environment_={};environmentAt_=0;
    socket_=nullptr; peers_.clear(); entities_.clear();pickups_.clear();nextPickup_=0; host_=kNoPlayer; nextEpoch_=nextEntity_=0; population_=0; hasMission_=false; missionSequence_=0; missionBarrier_={}; vote_={}; radar_={}; hasRadar_={};
}
void Server::Broadcast(const Message& m,ENetPeer* except,uint8_t channel,bool reliable) {
    for (auto& [peer,p] : peers_) if (peer!=except && p.id!=kNoPlayer && !p.rejected) SendPacket(peer,m,channel,reliable);
}
void Server::RefreshPopulation() {
    std::array<PopulationViewer,kMaxPlayers> viewers{};
    for(const auto& [peer,p]:peers_) if(p.id<kMaxPlayers && p.hasState) {
        (void)peer; viewers[p.id]={bool(p.lastState.state.flags&kReady),p.lastState.state.area,p.lastState.state.position};
    }
    auto next=ChoosePopulation(viewers,host_,population_);
    if(next==population_) return;
    population_=next; Message policy; policy.kind=Kind::Population; policy.population=next; Broadcast(policy);
    if(log) log("population sources mask="+std::to_string(next));
}
void Server::Remove(ENetPeer* peer) {
    auto it=peers_.find(peer); if (it==peers_.end()) return;
    auto id=it->second.id; const bool storyHost=id==host_ && hasMission_ && missionState_.mission.active; peers_.erase(it);
    if (id==kNoPlayer) return;
    if(id==host_) { hasEnvironment_=false;environment_={};environmentAt_=0;hasMission_=false; missionSequence_=0; missionBarrier_={}; vote_={}; radar_={}; hasRadar_={}; SendVote(); }
    if (id==host_) {
        host_=kNoPlayer;
        for (const auto& [key,p] : peers_) if (p.id!=kNoPlayer && (host_==kNoPlayer || p.id<host_)) host_=p.id;
    }
    Message m; m.kind=Kind::Left; m.id=id; m.host=host_; Broadcast(m);
    if(host_==kNoPlayer) {entities_.clear();pickups_.clear();}
    else {
        std::set<uint32_t> occupied;
        for(const auto& [target,p]:peers_) { (void)target; if(p.hasState && p.lastState.state.health>0 && p.lastState.state.vehicle) occupied.insert(p.lastState.state.vehicle); }
        for(auto entity=entities_.begin();entity!=entities_.end();) {
            auto& e=entity->second;
            if(e.state.owner!=id) { ++entity; continue; }
            const bool protectedVehicle=occupied.contains(e.state.id);
            const bool protectedOccupant=e.state.kind==EntityKind::Ped && occupied.contains(e.state.ped.vehicle);
            if(e.state.kind!=EntityKind::Object && !protectedVehicle && !protectedOccupant) {
                Message remove; remove.kind=Kind::EntityRemove; remove.entity=e.state; Broadcast(remove); entity=entities_.erase(entity); continue;
            }
            for(const auto& [target,p]:peers_) if(p.id==host_) {
                (void)target; e.state.owner=host_; e.state.ownerEpoch=p.epoch; ++e.state.generation; e.sequence=0;
                Message authority; authority.kind=Kind::EntityAuthority; authority.entity=e.state; Broadcast(authority); break;
            }
            ++entity;
        }
    }
    missionBarrier_.members&=uint8_t(~(1u<<id)); ReleaseMissionBarrier();
    vote_.members&=uint8_t(~(1u<<id)); vote_.yes&=vote_.members; vote_.Recount(); SendVote();
    if(storyHost) {
        // No peer has the host's VM stack/locals. Never pretend that choosing a
        // new transport leader can resume the old mission script.
        Message stop; stop.kind=Kind::Rejected; stop.reason=Reason::MissionHostLeft;
        for(auto& [target,other]:peers_) if(other.id<kMaxPlayers && !other.rejected) { SendPacket(target,stop); other.rejected=true; enet_peer_disconnect_later(target,uint32_t(stop.reason)); }
    }
    RefreshPopulation();
    if (log) log("player "+std::to_string(id)+" left; host="+std::to_string(host_));
}
void Server::Reject(ENetPeer* peer,Reason reason) {
    Remove(peer);
    Peer p; p.rejected=true; p.connected=NowMs(); peers_.emplace(peer,std::move(p));
    Message m; m.kind=Kind::Rejected; m.reason=reason; SendPacket(peer,m);
    enet_peer_disconnect_later(peer,uint32_t(reason));
    if (log) log("rejected connection, reason="+std::to_string(unsigned(reason)));
}
void Server::Receive(ENetPeer* peer,const ENetEvent& event) {
    auto it=peers_.find(peer); if (it==peers_.end() || it->second.rejected) return;
    auto& p=it->second; Message m;
    if (!Decode({event.packet->data,event.packet->dataLength},m) || m.sender!=kNoPlayer || m.epoch!=0) { Reject(peer,Reason::Protocol); return; }
    auto now=NowMs(); if (now-p.window>=1000) { p.window=now; p.states=p.controls=p.hits=p.shots=p.entityUpdates=p.entityControls=p.missionEvents=p.clocks=p.crimes=p.pickups=0; }
    if (m.kind==Kind::Hello && p.id==kNoPlayer && event.channelID==0) {
        if (++p.controls>8) { Reject(peer,Reason::RateLimit); return; }
        if(missionBarrier_.token || (hasMission_ && missionState_.mission.cinematic)) { Reject(peer,Reason::MissionBusy); return; }
        std::array<bool,kMaxPlayers> used{};
        for (const auto& [key,other] : peers_) if (other.id<kMaxPlayers) used[other.id]=true;
        uint8_t id=0; while (id<kMaxPlayers && used[id]) ++id;
        if (id==kMaxPlayers) { Reject(peer,Reason::Full); return; }
        p.id=id; p.name=m.name; if (!++nextEpoch_) ++nextEpoch_; p.epoch=nextEpoch_; if (host_==kNoPlayer) host_=id;
        Message welcome; welcome.kind=Kind::Welcome; welcome.id=id; welcome.host=host_; SendPacket(peer,welcome);
        for (const auto& [key,other] : peers_) if (other.id!=kNoPlayer) {
            Message joined; joined.kind=Kind::Joined; joined.id=other.id; joined.name=other.name; joined.epoch=other.epoch; SendPacket(peer,joined);
            if (other.hasState) { auto baseline=other.lastState; baseline.kind=Kind::Transition; SendPacket(peer,baseline); }
        }
        Message joined; joined.kind=Kind::Joined; joined.id=id; joined.name=p.name; joined.epoch=p.epoch; Broadcast(joined,peer);
        for(const auto& [key,e]:entities_) { Message spawn; spawn.kind=Kind::EntityCreate; spawn.entity=e.state; SendPacket(peer,spawn); }
        for(const auto& [key,pickup]:pickups_) {Message baseline;baseline.kind=Kind::PickupState;baseline.pickup=pickup.state;SendPacket(peer,baseline);}
        Message policy; policy.kind=Kind::Population; policy.population=population_; SendPacket(peer,policy);
        if(hasEnvironment_) SendPacket(peer,environment_); // baseline follows the reliable roster
        if(hasMission_) SendPacket(peer,missionState_);
        for(size_t i=0;i<kRadarSlots;++i) if(hasRadar_[i]) SendPacket(peer,radar_[i]);
        if (log) log("player "+std::to_string(id)+" joined: "+p.name);
        return;
    }
    if(m.kind==Kind::Environment && p.id<kMaxPlayers) {
        if(p.id!=host_ || event.channelID!=1) {Reject(peer,Reason::Protocol);return;}
        if(hasEnvironment_ && !Newer(m.sequence,environment_.sequence)) return;
        // Weather snapshots are replaceable; never backlog the reliable event queue.
        if(now-environmentAt_<15) return;
        m.sender=p.id;m.epoch=p.epoch;environment_=m;hasEnvironment_=true;environmentAt_=now;
        Broadcast(m,peer,1+p.id,false);return;
    }
    if(m.kind==Kind::ClockRequest && p.id<kMaxPlayers && event.channelID==kClockChannel) {
        if(++p.clocks>16) {Reject(peer,Reason::RateLimit);return;}
        Message reply;reply.kind=Kind::ClockReply;reply.sequence=m.sequence;reply.clockClientMs=m.clockClientMs;
        reply.clockServerInMs=now;reply.clockServerOutMs=NowMs();SendPacket(peer,reply,kClockChannel,false);return;
    }
    // Discard impossible source clocks; an unsynchronized peer uses the staging-age fallback.
    if(m.sampleTimeMs && (m.sampleTimeMs>now+100 || m.sampleTimeMs+2000<now)) m.sampleTimeMs=0;
    if(m.kind==Kind::Radar && p.id!=kNoPlayer) {
        if(p.id!=host_ || event.channelID || ++p.missionEvents>1500) { Reject(peer,Reason::Protocol); return; }
        const auto slot=m.radar.slot; if(hasRadar_[slot] && !Newer(m.sequence,radar_[slot].sequence)) return;
        m.sender=p.id; m.epoch=p.epoch; radar_[slot]=m; hasRadar_[slot]=true; Broadcast(m,peer); return;
    }
    if(m.kind>=Kind::MissionState && m.kind<=Kind::MissionVoteState && p.id!=kNoPlayer) { ReceiveMission(peer,p,m,event.channelID); return; }
    if(m.kind>=Kind::EntityCreate && m.kind<=Kind::VehicleContact && p.id!=kNoPlayer) { ReceiveEntity(peer,p,m,event.channelID); return; }
    if(m.kind==Kind::Explosion && p.id!=kNoPlayer && event.channelID==0) {
        if(++p.shots>600 || !p.hasState) return;
        const auto& blast=m.explosion; Vec3 origin=p.lastState.state.position;
        if(blast.source) {
            auto it=entities_.find(blast.source);if(it==entities_.end()) return;
            const auto& e=it->second.state;if(e.owner!=p.id || e.ownerEpoch!=p.epoch || e.generation!=blast.generation) return;
            origin=EntityPosition(e);
        } else if(blast.life!=p.lastState.state.life) return;
        const float dx=origin.x-blast.position.x,dy=origin.y-blast.position.y,dz=origin.z-blast.position.z;
        if(dx*dx+dy*dy+dz*dz>2000*2000) return;
        m.sender=p.id;m.epoch=p.epoch;Broadcast(m,peer);return;
    }
    if(m.kind>=Kind::PickupCreate && m.kind<=Kind::PickupGrant && p.id<kMaxPlayers) {
        if(event.channelID || ++p.pickups>1024 || m.kind==Kind::PickupGrant || m.kind==Kind::PickupState) {Reject(peer,Reason::Protocol);return;}
        ReceivePickup(peer,p,m);return;
    }
    if(m.kind==Kind::HeliHit && p.id<kMaxPlayers && event.channelID==0) {
        if(++p.shots>600) {Reject(peer,Reason::RateLimit);return;}
        auto& h=m.heli;auto it=entities_.find(h.target);if(it==entities_.end()) return;auto& target=it->second;const auto& e=target.state;
        Vec3 origin;uint8_t area;
        if(e.generation!=h.generation || e.kind!=EntityKind::Vehicle || e.model!=165 || target.heliRewarded || target.heliCommittedDown || e.vehicle.heliStatus==3
            || !AttackOrigin(p,h.source,h.sourceGeneration,origin,area) || e.area!=area || (!h.source && h.sourceLife!=p.lastState.state.life)) return;
        auto point=EntityPosition(e);float dx=origin.x-point.x,dy=origin.y-point.y,dz=origin.z-point.z;
        float x=h.point.x-point.x,y=h.point.y-point.y,z=h.point.z-point.z;
        if(dx*dx+dy*dy+dz*dz>1500*1500 || x*x+y*y+z*z>12*12) return;
        if(!h.source) {target.heliAttacker=p.id;target.heliEpoch=p.epoch;target.heliLife=h.sourceLife;}
        else for(const auto& [peer,driver]:peers_) if(driver.hasState && driver.lastState.state.vehicle==h.source && driver.lastState.state.seat==0) {
            target.heliAttacker=driver.id;target.heliEpoch=driver.epoch;target.heliLife=driver.lastState.state.life;break;
        }
        if(h.mode || (target.heliDamage+=h.damage)>700) target.heliCommittedDown=true;
        m.sender=p.id;m.epoch=p.epoch;Broadcast(m);return;
    }
    if(m.kind==Kind::HeliDown && p.id<kMaxPlayers && event.channelID==0) {
        auto it=entities_.find(m.heli.target);if(it==entities_.end())return;auto& entry=it->second;const auto& e=entry.state;
        if(e.owner!=p.id || e.ownerEpoch!=p.epoch || e.generation!=m.heli.generation || e.model!=165 || e.kind!=EntityKind::Vehicle || entry.heliRewarded || !entry.heliCommittedDown || e.vehicle.heliStatus!=3) return;
        entry.heliRewarded=true;
        for(const auto& [peer,target]:peers_) if(target.id==entry.heliAttacker && target.epoch==entry.heliEpoch && target.hasState && target.lastState.state.life==entry.heliLife) {
            Message reward;reward.kind=Kind::HeliReward;reward.heli=m.heli;reward.hit.targetEpoch=target.epoch;reward.hit.life=entry.heliLife;SendPacket(peer,reward);break;
        }
        return;
    }
    if(m.kind==Kind::Arrest && p.id<kMaxPlayers && event.channelID==0) {
        if(++p.crimes>120) {Reject(peer,Reason::RateLimit);return;}
        const auto& a=m.arrest;auto it=entities_.find(a.cop);if(it==entities_.end()) return;
        const auto& e=it->second.state;
        if(e.kind!=EntityKind::Ped || e.ped.pedType!=6 || e.ped.health<=0 || e.owner!=p.id || e.ownerEpoch!=p.epoch || e.generation!=a.generation) return;
        for(auto& [target,t]:peers_) if(t.id==a.target && t.epoch==a.epoch && t.hasState && (t.lastState.state.flags&kReady)
            && t.lastState.state.life==a.life && t.lastState.state.health>0 && t.lastState.state.wanted && t.lastState.state.area==e.area) {
            auto x=t.lastState.state.position,y=EntityPosition(e);float dx=x.x-y.x,dy=x.y-y.y,dz=x.z-y.z;
            if(dx*dx+dy*dy+dz*dz>36 || t.arrestedLife==a.life) return;
            t.arrestedLife=a.life;m.sender=p.id;m.epoch=p.epoch;SendPacket(target,m);return;
        }
        return;
    }
    if(m.kind==Kind::Crime && p.id<kMaxPlayers && event.channelID==0) {
        if(++p.crimes>120) {Reject(peer,Reason::RateLimit);return;}
        const auto& c=m.crime;
        for(const auto& [target,t]:peers_) if(t.id==c.target && t.epoch==c.epoch && t.hasState
            && t.lastState.state.life==c.life && t.lastState.state.health>0) {
            auto origin=t.lastState.state.position;auto area=t.lastState.state.area;
            if(c.witness) {
                auto it=entities_.find(c.witness);if(it==entities_.end()) return;
                const auto& e=it->second.state;
                if(e.owner!=p.id || e.ownerEpoch!=p.epoch || e.generation!=c.generation || e.area!=area) return;
                auto v=EntityPosition(e);float dx=v.x-origin.x,dy=v.y-origin.y,dz=v.z-origin.z;
                if(dx*dx+dy*dy+dz*dz>120*120) return;
            } else if(!(population_&(1u<<p.id)) || !p.hasState || p.lastState.state.area!=area) return;
            float dx=c.position.x-origin.x,dy=c.position.y-origin.y,dz=c.position.z-origin.z;
            if(dx*dx+dy*dy+dz*dz>40*40) return;
            m.sender=p.id;m.epoch=p.epoch;SendPacket(target,m);return;
        }
        return;
    }
    if (m.kind==Kind::Chat && p.id!=kNoPlayer && event.channelID==0) {
        if (now-p.chatWindow>=1000) { p.chatWindow=now; p.chats=0; }
        if (++p.chats>4) { Reject(peer,Reason::RateLimit); return; }
        m.sender=p.id; m.epoch=p.epoch; Broadcast(m); return;
    }
    if ((m.kind==Kind::Hit || m.kind==Kind::Shot) && p.id!=kNoPlayer && event.channelID==0) {
        if ((m.kind==Kind::Hit && ++p.hits>120) || (m.kind==Kind::Shot && ++p.shots>600)) { Reject(peer,Reason::RateLimit); return; }
        Vec3 source; uint8_t area{};
        if(!AttackOrigin(p,m.kind==Kind::Hit ? m.hit.source:m.shot.source,m.kind==Kind::Hit ? m.hit.sourceGeneration:m.shot.sourceGeneration,source,area)) return;
        const auto sourceId=m.kind==Kind::Hit ? m.hit.source:m.shot.source;
        const auto sourceLife=m.kind==Kind::Hit ? m.hit.sourceLife:m.shot.sourceLife;
        if(!sourceId && sourceLife!=p.lastState.state.life) return;
        if(m.kind==Kind::Shot) {
            const float dx=m.shot.start.x-source.x,dy=m.shot.start.y-source.y,dz=m.shot.start.z-source.z;
            if(dx*dx+dy*dy+dz*dz>32*32) return;
        }
        m.sender=p.id; m.epoch=p.epoch;
        if (m.kind==Kind::Hit) {
            for (const auto& [target,t]:peers_) if (t.id==m.hit.target && t.epoch==m.hit.targetEpoch
                && t.hasState && t.lastState.state.life==m.hit.life && t.lastState.state.health>0) {
                const auto a=source,b=t.lastState.state.position;
                const auto dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
                const float range=m.hit.weapon<=11 ? 6.f : 1500.f;
                if (area==t.lastState.state.area && dx*dx+dy*dy+dz*dz<=range*range)
                    SendPacket(target,m);
                break;
            }
        } else Broadcast(m,peer);
        return;
    }
    if ((m.kind!=Kind::State && m.kind!=Kind::Transition) || p.id==kNoPlayer || event.channelID!=(m.kind==Kind::Transition ? 0:1)) { Reject(peer,Reason::Protocol); return; }
    if (++p.states>90) { Reject(peer,Reason::RateLimit); return; }
    if (p.hasState && !Newer(m.sequence,p.sequence)) return;
    p.hasState=true; p.sequence=m.sequence; m.sender=p.id; m.epoch=p.epoch; p.lastState=m; RefreshPopulation();
    Broadcast(m,peer,m.kind==Kind::Transition ? 0:uint8_t(1+p.id),m.kind==Kind::Transition);
}
void Server::ReceivePickup(ENetPeer* peer,Peer& owner,Message& m) {
    auto now=NowMs();
    if(!owner.hasState || !(owner.lastState.state.flags&kReady)) return;
    auto broadcast=[&](const Pickup& p){Message state;state.kind=Kind::PickupState;state.pickup=p;Broadcast(state);};
    if(m.kind==Kind::PickupCreate) {
        auto p=m.pickup;p.creator=owner.id;p.creatorEpoch=owner.epoch;p.id=0;p.revision=1;p.status=0;p.takenBy=kNoPlayer;
        auto d=p.position,a=owner.lastState.state.position;float dx=d.x-a.x,dy=d.y-a.y,dz=d.z-a.z;
        bool ownedDrop=false;
        if(p.source) {
            auto source=entities_.find(p.source);if(source==entities_.end()) return;const auto& e=source->second.state;
            auto pos=EntityPosition(e);float x=d.x-pos.x,y=d.y-pos.y,z=d.z-pos.z;
            ownedDrop=e.owner==owner.id && e.ownerEpoch==owner.epoch && e.generation==p.sourceGeneration && e.kind==EntityKind::Ped && x*x+y*y+z*z<16*16;
            if(!ownedDrop) return;
        }
        bool nearbyPersonal=(p.type==4 || p.type==5 || p.type==8) && dx*dx+dy*dy+dz*dz<64;
        if(owner.id!=host_ && !ownedDrop && !nearbyPersonal && (!(population_&(1u<<owner.id)) || (p.area!=13 && p.area!=owner.lastState.state.area) || dx*dx+dy*dy+dz*dz>220*220)) return;
        for(const auto& [id,existing]:pickups_) {
            const auto& q=existing.state;auto b=q.position;
            bool ephemeral=p.type==4 || p.type==5 || p.type==8;
            bool same=ephemeral?(q.creator==owner.id && q.creatorEpoch==owner.epoch && q.key==p.key):
                (q.type==p.type && q.model==p.model && q.area==p.area && std::abs(d.x-b.x)<.12f && std::abs(d.y-b.y)<.12f && std::abs(d.z-b.z)<.12f);
            if(same) {Message state;state.kind=Kind::PickupState;state.pickup=q;SendPacket(peer,state);return;}
        }
        if(pickups_.size()>=kMaxPickups) return;
        p.id=++nextPickup_;pickups_[p.id]={p,p.remaining?now+p.remaining:0};broadcast(p);return;
    }
    auto it=pickups_.find(m.pickup.id);if(it==pickups_.end()) return;auto& entry=it->second;auto& p=entry.state;
    if(p.revision!=m.pickup.revision || p.status) return;
    if(m.kind==Kind::PickupRemove) {
        if(p.creator!=owner.id || p.creatorEpoch!=owner.epoch) return;
        p.status=2;++p.revision;entry.deadline=0;broadcast(p);return;
    }
    if(owner.lastState.state.life!=m.hit.life || owner.lastState.state.health<=0 || (p.area!=13 && owner.lastState.state.area!=p.area)) return;
    auto a=owner.lastState.state.position,b=p.position;float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    if(dx*dx+dy*dy+dz*dz>6*6 || (m.id && ((p.flags&1) && !p.quantity))) return;
    // Commit first, then award exactly one participant. A second concurrent
    // request for this revision cannot grant ammo/money again.
    Message grant;grant.kind=Kind::PickupGrant;grant.pickup=p;grant.pickup.takenBy=owner.id;grant.id=m.id;grant.hit.targetEpoch=owner.epoch;grant.hit.life=m.hit.life;
    p.takenBy=owner.id;++p.revision;
    if(m.id) {p.flags|=1;p.quantity=0;}
    else {
        p.flags|=2;
        if(p.type==2 || p.type==15) {p.status=1;p.remaining=p.type==2?30000:p.model==375?300000:720000;entry.deadline=now+p.remaining;}
        else {p.status=2;entry.deadline=(p.type==4 || p.type==5 || p.type==8)?now+15000:0;}
    }
    SendPacket(peer,grant);broadcast(p);
}
void Server::TickPickups() {
    auto now=NowMs();std::vector<uint32_t> retired;
    for(auto& [id,entry]:pickups_) {
        auto& p=entry.state;if(!entry.deadline || now<entry.deadline) continue;
        if(p.status==2) {p.status=3;++p.revision;Message forget;forget.kind=Kind::PickupState;forget.pickup=p;Broadcast(forget);retired.push_back(id);continue;}
        if(p.status==1) {
            bool nearbyPlayer=false;
            for(const auto& [peer,player]:peers_) if(player.hasState && (player.lastState.state.flags&kReady)
                && PickupRespawnBlocked(p,player.lastState.state.position,player.lastState.state.area)) nearbyPlayer=true;
            if(nearbyPlayer) continue;
            p.status=0;p.flags&=uint8_t(~2u);p.takenBy=kNoPlayer;
        } else if(!p.status) {p.status=2;p.takenBy=kNoPlayer;}
        else continue;
        entry.deadline=p.status==2 && (p.type==4 || p.type==5 || p.type==8)?now+15000:0;p.remaining=0;++p.revision;Message state;state.kind=Kind::PickupState;state.pickup=p;Broadcast(state);
    }
    for(auto id:retired) pickups_.erase(id);
}
bool Server::AttackOrigin(const Peer& p,uint32_t source,uint32_t generation,Vec3& position,uint8_t& area) const {
    if(!source) {
        if(!p.hasState || !(p.lastState.state.flags&kReady) || p.lastState.state.health<=0) return false;
        position=p.lastState.state.position; area=p.lastState.state.area; return true;
    }
    auto it=entities_.find(source); if(it==entities_.end()) return false;
    const auto& e=it->second.state;
    if(e.owner!=p.id || e.ownerEpoch!=p.epoch || e.generation!=generation || e.kind==EntityKind::Object
        || (e.kind==EntityKind::Ped && e.ped.health<=0) || (e.kind==EntityKind::Vehicle && e.vehicle.health<=0)) return false;
    position=EntityPosition(e); area=e.area; return true;
}
void Server::ReceiveEntity(ENetPeer* peer,Peer& p,Message& m,uint8_t channel) {
    const bool update=m.kind==Kind::EntityUpdate;
    if(channel!=(update ? 1 : 0) || m.kind==Kind::EntityAuthority || m.kind==Kind::SeatGrant) { Reject(peer,Reason::Protocol); return; }
    if((update && ++p.entityUpdates>kMaxEntities*35) || (!update && ++p.entityControls>240)) { Reject(peer,Reason::RateLimit); return; }
    auto& state=m.entity;
    if(m.kind==Kind::VehicleContact) {
        const auto& c=m.contact;
        auto source=entities_.find(c.source),target=entities_.find(c.target);
        if(source==entities_.end() || target==entities_.end()) return;
        const auto& a=source->second.state; const auto& b=target->second.state;
        if(a.kind!=EntityKind::Vehicle || b.kind!=EntityKind::Vehicle || a.owner!=p.id || a.ownerEpoch!=p.epoch
            || a.generation!=c.sourceGeneration || b.generation!=c.targetGeneration || b.owner==p.id || a.area!=b.area) return;
        auto x=EntityPosition(a),y=EntityPosition(b); const auto dx=x.x-y.x,dy=x.y-y.y,dz=x.z-y.z;
        if(dx*dx+dy*dy+dz*dz>625) return;
        m.sender=p.id; m.epoch=p.epoch;
        for(const auto& [targetPeer,owner]:peers_) if(owner.id==b.owner && owner.epoch==b.ownerEpoch) { SendPacket(targetPeer,m); break; }
        return;
    }
    if(m.kind==Kind::EntityCreate) {
        if(state.kind==EntityKind::Object && !state.mission && !(state.object.flags&64)) {
            for(const auto& [id,entry]:entities_) {
                const auto& known=entry.state;auto a=known.origin,b=state.origin;
                if(known.kind!=EntityKind::Object || known.mission || (known.object.flags&64) || known.model!=state.model || known.area!=state.area) continue;
                const float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
                if(dx*dx+dy*dy+dz*dz<.01f) {
                    Message original;original.kind=Kind::EntityCreate;original.entity=known;SendPacket(peer,original);return;
                }
            }
        }
        if(state.id || !p.hasState || !(p.lastState.state.flags&kReady)) return;
        for(const auto& [key,existing]:entities_) if(existing.state.creator==p.id && existing.state.creatorEpoch==p.epoch && existing.state.key==state.key) {
            Message reply; reply.kind=Kind::EntityCreate; reply.entity=existing.state; SendPacket(peer,reply); return;
        }
        if(entities_.size()>=(state.mission ? kMaxEntities:kMaxEntities-64) || nextEntity_==UINT32_MAX) return;
        if(state.kind==EntityKind::Object && std::count_if(entities_.begin(),entities_.end(),[](const auto& item){return item.second.state.kind==EntityKind::Object;})>=int(kMaxWorldObjects)) return;
        const auto position=EntityPosition(state),player=p.lastState.state.position;
        const auto dx=position.x-player.x,dy=position.y-player.y,dz=position.z-player.z;
        const bool privateCar=state.kind==EntityKind::Vehicle && (p.lastState.state.flags&kInVehicle) && dx*dx+dy*dy+dz*dz<=100;
        const bool privateProjectile=state.kind==EntityKind::Object && (state.object.flags&64) && dx*dx+dy*dy+dz*dz<=100;
        const bool populationSource=(population_&(1u<<p.id)) && state.area==p.lastState.state.area && dx*dx+dy*dy+dz*dz<=220*220;
        if(p.id!=host_ && !privateCar && !privateProjectile && !populationSource) return;
        if(state.mission && p.id!=host_) return;
        state.id=++nextEntity_; state.generation=1; state.owner=state.creator=p.id; state.ownerEpoch=state.creatorEpoch=p.epoch;
        entities_[state.id]={state,0,NowMs()}; m.sender=kNoPlayer; m.epoch=0; Broadcast(m); return;
    }
    auto it=entities_.find(state.id); if(it==entities_.end()) return;
    auto& entry=it->second; auto& e=entry.state;
    if(state.generation!=e.generation) return; // a stale owner cannot alter a transferred entity
    if(m.kind==Kind::SeatRequest) {
        bool available=e.kind==EntityKind::Vehicle && m.id>=1 && m.id<=e.vehicle.passengers && p.hasState && p.lastState.state.health>0
            && p.lastState.state.area==e.area && !(p.lastState.state.flags&kInVehicle);
        if(available) {
            auto a=p.lastState.state.position,b=EntityPosition(e); auto dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
            available=dx*dx+dy*dy+dz*dz<=64;
            const auto& lease=entry.seats[m.id];
            if(lease.player!=p.id && lease.player!=kNoPlayer && lease.until>NowMs()) available=false;
            for(const auto& [target,other]:peers_) if(other.id!=p.id && other.hasState && (other.lastState.state.flags&kInVehicle)
                && other.lastState.state.vehicle==e.id && other.lastState.state.seat==m.id) available=false;
            for(const auto& [key,npc]:entities_) if(npc.state.kind==EntityKind::Ped && (npc.state.ped.flags&kInVehicle)
                && npc.state.ped.vehicle==e.id && npc.state.ped.seat==m.id) available=false;
        }
        if(available) entry.seats[m.id]={p.id,p.epoch,NowMs()+8000};
        m.kind=Kind::SeatGrant; m.host=p.id; if(!available) m.id=255; m.sender=kNoPlayer; m.epoch=0; SendPacket(peer,m); return;
    }
    if(m.kind==Kind::EntityClaim) {
        if(e.kind!=EntityKind::Vehicle || e.owner==p.id || !p.hasState || p.lastState.state.health<=0 || e.area!=p.lastState.state.area) return;
        const auto a=EntityPosition(e),b=p.lastState.state.position; const auto dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
        if(dx*dx+dy*dy+dz*dz>144) return;
        if(NowMs()<entry.claimUntil) return;
        for(const auto& [target,owner]:peers_) if(owner.id==e.owner && owner.hasState && owner.lastState.state.vehicle==e.id && owner.lastState.state.seat==0 && owner.lastState.state.health>0) return;
        e.owner=p.id; e.ownerEpoch=p.epoch; ++e.generation; entry.sequence=0; entry.claimUntil=NowMs()+3000;
        m.kind=Kind::EntityAuthority; m.entity=e; m.sender=kNoPlayer; m.epoch=0; Broadcast(m);
        for(auto& [occupantId,occupant]:entities_) if(occupant.state.kind==EntityKind::Ped && occupant.state.ped.vehicle==e.id) {
            occupant.state.owner=p.id; occupant.state.ownerEpoch=p.epoch; ++occupant.state.generation; occupant.sequence=0;
            Message authority; authority.kind=Kind::EntityAuthority; authority.entity=occupant.state; Broadcast(authority);
        }
        return;
    }
    if(m.kind==Kind::EntityHit) {
        Vec3 a; uint8_t area{}; if(!AttackOrigin(p,m.hit.source,m.hit.sourceGeneration,a,area) || (area!=e.area && !(e.kind==EntityKind::Object && e.area==13)) || (!m.hit.source && m.hit.sourceLife!=p.lastState.state.life)) return;
        auto b=EntityPosition(e); auto dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
        float range=m.hit.weapon<=11 ? 6.f : 1500.f;
        if(dx*dx+dy*dy+dz*dz>range*range) return;
        m.sender=p.id; m.epoch=p.epoch;
        for(const auto& [target,owner]:peers_) if(owner.id==e.owner && owner.epoch==e.ownerEpoch) { SendPacket(target,m); break; }
        return;
    }
    if(e.owner!=p.id || e.ownerEpoch!=p.epoch) return;
    if(m.kind==Kind::EntityRemove) { m.sender=kNoPlayer; m.epoch=0; Broadcast(m); entities_.erase(it); return; }
    if(!update || state.kind!=e.kind || state.model!=e.model || (state.mission!=e.mission && (p.id!=host_ || !state.mission)) || state.key!=e.key || !Newer(m.sequence,entry.sequence)) return;
    const auto creator=e.creator; const auto creatorEpoch=e.creatorEpoch; const auto origin=e.origin;
    e=state; e.owner=p.id; e.ownerEpoch=p.epoch; e.creator=creator; e.creatorEpoch=creatorEpoch; e.origin=origin;
    entry.sequence=m.sequence; entry.updated=NowMs(); m.entity=e; m.sender=p.id; m.epoch=p.epoch;
    Broadcast(m,peer,uint8_t(1+p.id),false);
}
void Server::SendVote() { Message m; m.kind=Kind::MissionVoteState; m.vote=vote_; Broadcast(m); }
void Server::ReleaseMissionBarrier() {
    if(!missionBarrier_.Complete()) return;
    Message release; release.kind=Kind::MissionRelease; release.sender=host_; release.epoch=missionBarrier_.epoch; release.sequence=missionBarrier_.token;
    Broadcast(release); missionBarrier_={}; barrierAt_=0;
    if(log) log("mission barrier released serial="+std::to_string(release.sequence));
}
void Server::ReceiveMission(ENetPeer* peer,Peer& p,Message& m,uint8_t channel) {
    if(channel || ++p.missionEvents>1500) { Reject(peer,channel ? Reason::Protocol:Reason::RateLimit); return; }
    if(m.kind==Kind::MissionVote) {
        if(m.sequence==vote_.scene && p.hasState && (p.lastState.state.flags&kReady) && vote_.Submit(p.id,m.id!=0)) { voteAt_=NowMs(); SendVote(); }
        return;
    }
    if(m.kind==Kind::MissionReady) {
        if(!p.hasState || !(p.lastState.state.flags&kReady) || p.id==host_) return;
        missionBarrier_.Ack(p.id,m.sequence); ReleaseMissionBarrier(); return;
    }
    if(p.id!=host_ || m.kind==Kind::MissionRelease || m.kind==Kind::MissionVoteState) { Reject(peer,Reason::Protocol); return; }
    if(!m.sequence || !Newer(m.sequence,missionSequence_)) return;
    if(m.kind==Kind::MissionState) {
        if(hasMission_ && m.mission.run!=missionState_.mission.run && !Newer(m.mission.run,missionState_.mission.run)) return;
        missionState_=m; missionState_.sender=p.id; missionState_.epoch=p.epoch; hasMission_=true;
    } else {
        if(!hasMission_ || m.command.run!=missionState_.mission.run) return;
        if(m.kind==Kind::MissionBarrier) {
            if(missionBarrier_.token) return;
            missionBarrier_={m.sequence,m.command.run,p.epoch,0,uint8_t(1u<<host_)};
            for(const auto& [target,other]:peers_) { (void)target; if(other.id<kMaxPlayers && !other.rejected) missionBarrier_.members|=uint8_t(1u<<other.id); }
            barrierAt_=NowMs();
        }
    }
    missionSequence_=m.sequence; m.sender=p.id; m.epoch=p.epoch; Broadcast(m,peer);
    if(m.kind==Kind::MissionBarrier) ReleaseMissionBarrier();
    if(m.kind==Kind::MissionCommand && m.command.opcode==0x02E7) {
        vote_={}; vote_.scene=m.sequence;
        for(const auto& [target,other]:peers_) { (void)target; if(other.id<kMaxPlayers && other.hasState && (other.lastState.state.flags&kReady)) vote_.members|=uint8_t(1u<<other.id); }
        SendVote();
    }
    if(m.kind==Kind::MissionCommand && m.command.opcode==0x02EA) { vote_={}; SendVote(); }
}
void Server::Poll(uint32_t wait) {
    if (!socket_) return;
    ENetEvent event{}; int count=0;
    while (count++<128 && enet_host_service(socket_,&event,count==1 ? wait : 0)>0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                ConfigurePeer(event.peer);
                Peer p; p.connected=p.window=NowMs(); peers_.emplace(event.peer,std::move(p));
                if (event.data!=kProtocol) Reject(event.peer,Reason::Protocol);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
                Receive(event.peer,event); enet_packet_destroy(event.packet); break;
            case ENET_EVENT_TYPE_DISCONNECT: Remove(event.peer); break;
            default: break;
        }
    }
    TickPickups();
    if(vote_.open && NowMs()-voteAt_>15000) { vote_.open=vote_.yes=0; vote_.declined=255; SendVote(); }
    std::vector<ENetPeer*> expired;
    for (const auto& [peer,p] : peers_) if (p.id==kNoPlayer && NowMs()-p.connected>5000) expired.push_back(peer);
    for (auto* peer : expired) { enet_peer_disconnect_now(peer,uint32_t(Reason::Timeout)); Remove(peer); }
    if(missionBarrier_.token && NowMs()-barrierAt_>15000) {
        std::vector<ENetPeer*> slow;
        for(const auto& [target,p]:peers_) if(p.id<kMaxPlayers && !(missionBarrier_.ready&(1u<<p.id))) slow.push_back(target);
        for(auto* target:slow) Reject(target,Reason::MissionTimeout);
        ReleaseMissionBarrier();
    }
    if(log && NowMs()-transportLogAt_>=2000) {
        transportLogAt_=NowMs();
        for(const auto& [peer,p]:peers_) if(p.id<kMaxPlayers)
            log("transport id="+std::to_string(p.id)+" throttle="+std::to_string(peer->packetThrottle)+"/32 rtt="+std::to_string(peer->roundTripTime)+" variance="+std::to_string(peer->roundTripTimeVariance)+" queued="+std::to_string(enet_list_size(&peer->outgoingCommands)));
    }
    enet_host_flush(socket_);
}
bool Client::Connect(const std::string& address,uint16_t port,const std::string& name) {
    Disconnect(); error_.clear();
    if (!ValidName(name) || !port) { error_="Invalid name or port"; return false; }
    ENetAddress addr{}; addr.port=port;
    if (enet_address_set_host(&addr,address.c_str())!=0) { error_="Cannot resolve server"; return false; }
    socket_=enet_host_create(nullptr,1,kChannels,0,0);
    if (!socket_) { error_="Cannot create network socket"; return false; }
    socket_->maximumPacketSize=kMaxPacket; socket_->maximumWaitingData=2*1024*1024;
    server_=enet_host_connect(socket_,&addr,kChannels,kProtocol);
    if (!server_) { Fail("Cannot allocate connection"); return false; }
    enet_peer_timeout(server_,32,3000,10000); name_=name; deadline_=NowMs()+8000; status_=Connection::Connecting;
    return true;
}
void Client::Disconnect() {
    if (server_) { enet_peer_disconnect_now(server_,0); if (socket_) enet_host_flush(socket_); }
    if (socket_) enet_host_destroy(socket_);
    environment_={};hasEnvironment_=false;environmentSequence_=0;
    clock_.Clear();clockAt_=0;clockSequence_=0;clockPending_.clear();
    socket_=nullptr; server_=nullptr; self_=host_=kNoPlayer; population_=0; sequence_=0; hasPublished_=false; published_={}; players_={}; chats_.clear(); events_.clear(); entities_.clear();pickups_.clear(); mission_={}; vote_={}; radar_={}; hasMission_=false; missionSequence_=0; status_=Connection::Offline;
}
void Client::Fail(const std::string& error) { Disconnect(); error_=error; }
void Client::Receive(std::span<const uint8_t> bytes,uint8_t channel) {
    Message m;
    if (!Decode(bytes,m)) { Fail("Invalid server packet"); return; }
    if(m.kind==Kind::Environment) {
        if(status_!=Connection::Online || m.sender!=host_ || host_>=kMaxPlayers || (channel!=0 && channel!=1+host_) || !players_[host_].active || m.epoch!=players_[host_].epoch) return;
        if(!hasEnvironment_ || Newer(m.sequence,environmentSequence_)) {environment_=m.environment;hasEnvironment_=true;environmentSequence_=m.sequence;}
        return;
    }
    if(m.kind==Kind::ClockReply) {
        if(status_!=Connection::Online || channel!=kClockChannel || m.sender!=kNoPlayer || m.epoch) return;
        auto it=clockPending_.find(m.sequence);if(it==clockPending_.end() || it->second!=m.clockClientMs) return;
        clock_.Observe(it->second,m.clockServerInMs,m.clockServerOutMs,NowMs());clockPending_.erase(it);return;
    }
    if(m.kind==Kind::Radar) {
        if(status_!=Connection::Online || channel || m.sender!=host_ || host_>=kMaxPlayers || !players_[host_].active || m.epoch!=players_[host_].epoch) { Fail("Invalid radar authority"); return; }
        auto& entry=radar_[m.radar.slot]; if(!entry.known || Newer(m.sequence,entry.sequence)) entry={m.radar,m.sequence,true}; return;
    }
    if(m.kind==Kind::MissionVoteState) {
        if(status_!=Connection::Online || channel || m.sender!=kNoPlayer || m.epoch) { Fail("Invalid shared cutscene vote"); return; }
        vote_=m.vote; return;
    }
    if(m.kind>=Kind::MissionState && m.kind<=Kind::MissionRelease) {
        if(status_!=Connection::Online || channel || m.sender!=host_ || host_>=kMaxPlayers || !players_[host_].active || m.epoch!=players_[host_].epoch || m.kind==Kind::MissionReady) { Fail("Invalid story authority"); return; }
        if(m.kind!=Kind::MissionRelease) {
            if(!m.sequence || !Newer(m.sequence,missionSequence_)) return;
            missionSequence_=m.sequence;
        }
        if(m.kind==Kind::MissionState) { mission_=m.mission; hasMission_=true; return; }
        if(events_.size()>=8192) { Fail("Story event queue overflow"); return; }
        events_.push_back({NowMs(),m}); return;
    }
    if(m.kind==Kind::Population) {
        if(status_!=Connection::Online || channel || m.sender!=kNoPlayer || m.epoch) { Fail("Invalid population authority"); return; }
        population_=m.population; return;
    }
    if(m.kind==Kind::SeatGrant) {
        if(status_!=Connection::Online || channel || m.sender!=kNoPlayer || m.epoch || m.host!=self_) { Fail("Invalid seat grant"); return; }
        if(events_.size()>=8192) { Fail("Game event queue overflow"); return; }
        events_.push_back({NowMs(),m}); return;
    }
    if(m.kind==Kind::EntityCreate || m.kind==Kind::EntityAuthority || m.kind==Kind::EntityRemove || m.kind==Kind::EntityUpdate) {
        if(status_!=Connection::Online) return;
        const auto& e=m.entity;
        if(m.kind==Kind::EntityUpdate) {
            if(m.sender>=kMaxPlayers || channel!=1+m.sender || !players_[m.sender].active || players_[m.sender].epoch!=m.epoch) return;
            auto it=entities_.find(e.id); if(it==entities_.end()) return;
            if(e.generation!=it->second.state.generation || e.owner!=m.sender || e.ownerEpoch!=m.epoch || !Newer(m.sequence,it->second.sequence)) return;
            it->second={e,m.sequence,clock_.LocalTime(m.sampleTimeMs,NowMs(),m.sampleAgeMs)}; return;
        }
        if(channel || m.sender!=kNoPlayer || m.epoch || !e.id) { Fail("Invalid entity control"); return; }
        if(m.kind==Kind::EntityRemove) { auto it=entities_.find(e.id); if(it!=entities_.end() && it->second.state.generation==e.generation) entities_.erase(it); return; }
        if(e.owner>=kMaxPlayers || !players_[e.owner].active || e.ownerEpoch!=players_[e.owner].epoch) return;
        auto it=entities_.find(e.id);
        if(it!=entities_.end() && e.generation<=it->second.state.generation) return;
        if(it==entities_.end() && entities_.size()>=kMaxEntities) { Fail("Entity limit exceeded"); return; }
        entities_[e.id]={e,0,NowMs()}; return;
    }
    if(m.kind==Kind::HeliReward) {
        if(status_==Connection::Online && !channel && m.sender==kNoPlayer && !m.epoch && m.hit.targetEpoch==players_[self_].epoch) events_.push_back({NowMs(),m});return;
    }
    if(m.kind==Kind::PickupState || m.kind==Kind::PickupGrant) {
        if(status_!=Connection::Online || channel || m.sender!=kNoPlayer || m.epoch) return;
        if(m.kind==Kind::PickupGrant) {
            if(m.pickup.takenBy==self_ && m.hit.targetEpoch==players_[self_].epoch) events_.push_back({NowMs(),m});return;
        }
        if(m.pickup.status==3) {pickups_.erase(m.pickup.id);return;}
        auto it=pickups_.find(m.pickup.id);
        if(it==pickups_.end() && pickups_.size()>=kMaxPickups) {Fail("Pickup limit exceeded");return;}
        if(it==pickups_.end() || Newer(m.pickup.revision,it->second.revision)) pickups_[m.pickup.id]=m.pickup;
        return;
    }
    if (m.kind==Kind::Hit || m.kind==Kind::Shot || m.kind==Kind::Explosion || m.kind==Kind::EntityHit || m.kind==Kind::VehicleContact || m.kind==Kind::Crime || m.kind==Kind::Arrest || m.kind==Kind::HeliHit) {
        if (status_!=Connection::Online || channel!=0 || m.sender>=kMaxPlayers) { Fail("Invalid event sender"); return; }
        const auto& p=players_[m.sender];
        if (!p.active || p.epoch!=m.epoch) return;
        if (m.kind==Kind::Hit && (m.hit.target!=self_ || m.hit.targetEpoch!=players_[self_].epoch)) return;
        if(m.kind==Kind::Arrest && (m.arrest.target!=self_ || m.arrest.epoch!=players_[self_].epoch)) return;
        if(m.kind==Kind::Crime && (m.crime.target!=self_ || m.crime.epoch!=players_[self_].epoch)) return;
        if (events_.size()>=8192) { Fail("Game event queue overflow"); return; }
        events_.push_back({NowMs(),m}); return;
    }
    if (m.kind==Kind::Chat) {
        if (status_!=Connection::Online || channel!=0 || m.sender>=kMaxPlayers || !players_[m.sender].active || m.epoch!=players_[m.sender].epoch) { Fail("Invalid chat sender"); return; }
        chats_.push_back({++chatSerial_,NowMs(),m.sender,players_[m.sender].name,m.text});
        if (chats_.size()>100) chats_.pop_front();
        return;
    }
    if (m.kind==Kind::State || m.kind==Kind::Transition) {
        if (status_!=Connection::Online || m.sender>=kMaxPlayers || m.sender==self_ || channel!=(m.kind==Kind::Transition ? 0:1+m.sender)) return;
        auto& p=players_[m.sender];
        // State channels can arrive before the reliable roster. Discard until the roster is known.
        if (!p.active || m.epoch!=p.epoch || (p.hasState && !Newer(m.sequence,p.sequence))) return;
        p.hasState=true; p.sequence=m.sequence; p.state=m.state; p.received=clock_.LocalTime(m.sampleTimeMs,NowMs(),m.sampleAgeMs); return;
    }
    if (channel!=0 || m.sender!=kNoPlayer || (m.kind!=Kind::Joined && m.epoch!=0)) { Fail("Invalid control packet"); return; }
    if (m.kind==Kind::Rejected) {
        const char* reasons[]={"", "Protocol mismatch", "Server full", "Rate limit", "Handshake timeout", "Cutscene in progress; connect after it finishes", "Timed out preparing shared cutscene/audio", "Story host left; mission cannot migrate"};
        Fail(reasons[unsigned(m.reason)]); return;
    }
    if (m.kind==Kind::Welcome && status_==Connection::Handshake) {
        self_=m.id; host_=m.host; status_=Connection::Online; return;
    }
    if (status_!=Connection::Online) { Fail("Unexpected server message"); return; }
    if (m.kind==Kind::Joined) { auto& p=players_[m.id]; if (!p.active || p.epoch!=m.epoch) { p={}; p.generation=++generation_; } p.active=true; p.name=m.name; p.epoch=m.epoch; }
    else if (m.kind==Kind::Left) { players_[m.id]={}; if(host_!=m.host) { environment_={};hasEnvironment_=false;environmentSequence_=0;radar_={}; hasMission_=false; mission_={}; missionSequence_=0; } host_=m.host; }
    else Fail("Unexpected server message");
}
void Client::Poll() {
    if (!socket_) return;
    ENetEvent event{}; unsigned count=0;
    while (socket_ && count++<128 && enet_host_service(socket_,&event,0)>0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                ConfigurePeer(server_);
                Message m; m.kind=Kind::Hello; m.name=name_; SendPacket(server_,m); status_=Connection::Handshake; break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                // A rejected packet can destroy the host; release its detached event packet first.
                auto bytes=std::vector<uint8_t>(event.packet->data,event.packet->data+event.packet->dataLength);
                enet_packet_destroy(event.packet);
                Receive(bytes,event.channelID); break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: Fail("Disconnected from server"); break;
            default: break;
        }
    }
    if (socket_ && status_!=Connection::Online && NowMs()>deadline_) Fail("Connection timed out");
    if(status_==Connection::Online && NowMs()-clockAt_>=(clock_.Samples()<4?250:2000)) {
        Message request;request.kind=Kind::ClockRequest;request.sequence=++clockSequence_;request.clockClientMs=NowMs();
        if(SendPacket(server_,request,kClockChannel,false)) {
            clockPending_[request.sequence]=request.clockClientMs;while(clockPending_.size()>8) clockPending_.erase(clockPending_.begin());clockAt_=request.clockClientMs;
        }
    }
    if (socket_) enet_host_flush(socket_);
}
bool Client::SendState(const PlayerState& state,uint16_t sampleAgeMs) {
    if (status_!=Connection::Online || !ValidState(state) || sampleAgeMs>250) return false;
    const bool reliable=NeedsTransition(state);
    Message m; m.kind=reliable ? Kind::Transition:Kind::State; m.sequence=++sequence_; m.state=state;m.sampleAgeMs=sampleAgeMs;m.sampleTimeMs=clock_.ServerTime(NowMs()-sampleAgeMs);
    if(!SendPacket(server_,m,reliable ? 0:1,reliable)) return false;
    published_=state; hasPublished_=true;
    // The server broadcasts to other peers. Keep our own published state in
    // the same snapshot table used by population/occupancy decisions.
    if(self_<kMaxPlayers) {auto& own=players_[self_];own.state=state;own.sequence=m.sequence;own.received=NowMs();own.hasState=true;}
    return true;
}
bool Client::SendChat(const std::string& text) {
    if (status_!=Connection::Online || !ValidText(text)) return false;
    Message m; m.kind=Kind::Chat; m.text=text; return SendPacket(server_,m);
}
bool Client::SendEvent(const Message& event) {
    if (status_!=Connection::Online || (event.kind!=Kind::PickupCreate && event.kind!=Kind::PickupRequest && event.kind!=Kind::PickupRemove && event.kind!=Kind::Crime && event.kind!=Kind::Arrest && event.kind!=Kind::HeliHit && event.kind!=Kind::HeliDown && event.kind!=Kind::Environment && event.kind!=Kind::Hit && event.kind!=Kind::Shot && event.kind!=Kind::Explosion && event.kind!=Kind::EntityHit && event.kind!=Kind::VehicleContact && event.kind!=Kind::MissionState && event.kind!=Kind::MissionCommand && event.kind!=Kind::MissionBarrier && event.kind!=Kind::MissionReady && event.kind!=Kind::MissionVote && event.kind!=Kind::Radar)
        || (event.kind==Kind::Hit && !ValidHit(event.hit)) || (event.kind==Kind::Shot && !ValidShot(event.shot))) return false;
    auto m=event; m.sender=kNoPlayer; m.epoch=0;if(m.kind==Kind::Environment) {if(self_!=host_) return false;m.sequence=++sequence_;} Message parsed;
    if(!Decode(Encode(m),parsed)) return false;
    return SendPacket(server_,m,m.kind==Kind::Environment?1:0,m.kind!=Kind::Environment);
}
bool Client::SendEntity(const Message& event) {
    if(status_!=Connection::Online || ((event.kind<Kind::EntityCreate || event.kind>Kind::EntityClaim) && event.kind!=Kind::SeatRequest)) return false;
    if((event.kind==Kind::EntityCreate || event.kind==Kind::EntityUpdate) && !ValidEntity(event.entity)) return false;
    if(event.sampleAgeMs>250) return false;
    auto m=event; m.sender=kNoPlayer; m.epoch=0;
    const bool update=m.kind==Kind::EntityUpdate; if(update) {m.sequence=++sequence_;m.sampleTimeMs=clock_.ServerTime(NowMs()-m.sampleAgeMs);}
    if(!SendPacket(server_,m,update ? 1 : 0,!update)) return false;
    if(update) {
        auto it=entities_.find(m.entity.id);
        if(it!=entities_.end() && it->second.state.owner==self_ && it->second.state.generation==m.entity.generation)
            it->second={m.entity,m.sequence,NowMs()-m.sampleAgeMs};
    }
    return true;
}
}
