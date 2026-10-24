#include "BackgroundClient.h"
#include <windows.h>
#include <set>

namespace vc {
bool BackgroundClient::Start() {
    if (worker_.joinable()) return true;
    stopping_=false;
    try { worker_=std::thread(&BackgroundClient::Run,this); return true; }
    catch (...) { return false; }
}
void BackgroundClient::Stop() {
    stopping_=true;
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_); snapshot_={}; command_.connect=false; sampled_=0;
}
bool BackgroundClient::Connect(const std::string& address,uint16_t port,const std::string& name) {
    if (address.empty() || !port || !ValidName(name) || !Start()) return false;
    std::lock_guard lock(mutex_);
    ++command_.revision; command_.connect=true; command_.address=address; command_.port=port; command_.name=name;
    sampled_=entitySampled_=0; localEntities_.clear(); outgoing_.clear(); outgoingEvents_.clear(); incomingEvents_.clear(); snapshot_={}; snapshot_.status=Connection::Connecting;
    return true;
}
void BackgroundClient::Disconnect() {
    std::lock_guard lock(mutex_);
    ++command_.revision; command_.connect=false; sampled_=entitySampled_=0; localEntities_.clear(); outgoing_.clear(); outgoingEvents_.clear(); incomingEvents_.clear(); snapshot_={};
}
void BackgroundClient::Publish(const PlayerState& state) {
    if (!ValidState(state)) return;
    std::lock_guard lock(mutex_); local_=state; sampled_=NowMs();
}
BackgroundClient::Snapshot BackgroundClient::Read() { std::lock_guard lock(mutex_); return snapshot_; }
bool BackgroundClient::SendChat(const std::string& text) {
    if (!ValidText(text)) return false;
    std::lock_guard lock(mutex_);
    if (snapshot_.status!=Connection::Online || outgoing_.size()>=8) return false;
    outgoing_.push_back(text); return true;
}
bool BackgroundClient::SendEvent(const Message& event) {
    std::lock_guard lock(mutex_);
    if (snapshot_.status!=Connection::Online || outgoingEvents_.size()>=4096) return false;
    outgoingEvents_.push_back(event); return true;
}
std::deque<GameEvent> BackgroundClient::TakeEvents() {
    std::lock_guard lock(mutex_); std::deque<GameEvent> result; result.swap(incomingEvents_); return result;
}
bool BackgroundClient::SendEntity(const Message& event) {
    std::lock_guard lock(mutex_);
    if(snapshot_.status!=Connection::Online || outgoingEvents_.size()>=4096) return false;
    outgoingEvents_.push_back(event); return true;
}
void BackgroundClient::PublishEntities(std::vector<Entity> entities) {
    if(entities.size()>kMaxEntities) return;
    std::lock_guard lock(mutex_); localEntities_=std::move(entities); entitySampled_=NowMs();
}
void BackgroundClient::Run() {
    Client client;
    uint64_t revision=0,lastSend=0,lastEntitySend=0,entityRound=0;
    std::map<uint32_t,uint64_t> entitySent;std::map<uint32_t,uint8_t> objectFlags;
    while (!stopping_) {
        Command command; PlayerState local; uint64_t sampled,entitySampled; std::deque<std::string> outgoing; std::deque<Message> outgoingEvents; std::vector<Entity> entities;
        { std::lock_guard lock(mutex_); command=command_; local=local_; sampled=sampled_; entitySampled=entitySampled_; entities=localEntities_; outgoing.swap(outgoing_); outgoingEvents.swap(outgoingEvents_); }
        if (command.revision!=revision) {
            client.Disconnect(); revision=command.revision; lastSend=lastEntitySend=0;entitySent.clear();objectFlags.clear();
            if (command.connect) client.Connect(command.address,command.port,command.name);
        }
        client.Poll();
        for (const auto& text:outgoing) client.SendChat(text);
        auto now=NowMs();
        if (client.Status()==Connection::Online && sampled && (now-lastSend>=33 || client.NeedsTransition(local))) {
            // Paused/minimized game: keep transport alive and hold the last pose.
            if (now-sampled>250) {
                local.velocity={}; local.move=1;
                for(size_t i=0;i<local.animationCount;++i) local.animations[i].flags&=uint16_t(~1u);
            }
            client.SendState(local,uint16_t(std::min<uint64_t>(now-sampled,250))); lastSend=now;
        }
        for (const auto& event:outgoingEvents) {
            if((event.kind>=Kind::EntityCreate && event.kind<=Kind::EntityClaim) || event.kind==Kind::SeatRequest) client.SendEntity(event);
            else client.SendEvent(event);
        }
        if(client.Status()==Connection::Online && entitySampled && now-lastEntitySend>=15) {
            std::set<uint32_t> occupied;
            if(local.vehicle) occupied.insert(local.vehicle);
            for(auto& player:client.Players()) if(player.active && player.hasState && player.state.vehicle) occupied.insert(player.state.vehicle);
            // Vary packet order so deterministic transport loss never selects
            // the same NPCs from an otherwise identical batch on every tick.
            if(!entities.empty()) std::rotate(entities.begin(),entities.begin()+size_t(entityRound++%entities.size()),entities.end());
            for(auto& e:entities) {
                uint64_t period=50; // moving population at 20 Hz; human-driven vehicles at 30 Hz
                if(e.kind==EntityKind::Vehicle && occupied.contains(e.id)) period=33;
                if(e.kind==EntityKind::Object && !(e.object.flags&64)) {
                    auto v=e.object.transform.velocity,w=e.object.turn;period=v.x*v.x+v.y*v.y+v.z*v.z+w.x*w.x+w.y*w.y+w.z*w.z<.01f?500:50;
                }
                if(e.kind==EntityKind::Object && (!objectFlags.contains(e.id) || objectFlags[e.id]!=e.object.flags)) period=0;
                if(now-entitySent[e.id]<period) continue;
                auto known=client.Entities().find(e.id);
                if(known==client.Entities().end() || known->second.state.generation!=e.generation || known->second.state.owner!=client.Self()) continue;
                if(now-entitySampled>250) {
                    e.vehicle.transform.velocity={}; e.vehicle.turn={}; e.vehicle.wheelOmega={}; e.object.transform.velocity={};e.object.turn={}; e.ped.velocity={};
                    for(size_t i=0;i<e.ped.animationCount;++i) e.ped.animations[i].flags&=uint16_t(~1u);
                }
                Message update; update.kind=Kind::EntityUpdate; update.entity=e;update.sampleAgeMs=uint16_t(std::min<uint64_t>(now-entitySampled,250));if(client.SendEntity(update)) {entitySent[e.id]=now;if(e.kind==EntityKind::Object) objectFlags[e.id]=e.object.flags;}
            }
            lastEntitySend=now;
        }
        {
            std::lock_guard lock(mutex_);
            // A queued disconnect/reconnect must not publish the old session again.
            if (command_.revision==revision) {
                snapshot_.status=client.Status(); snapshot_.self=client.Self(); snapshot_.host=client.Host(); snapshot_.populationMask=client.PopulationMask();
                snapshot_.transport=client.Transport();snapshot_.environment=client.EnvironmentState();snapshot_.hasEnvironment=client.HasEnvironment();
                snapshot_.clockReady=client.Clock().Ready();snapshot_.clockSamples=client.Clock().Samples();snapshot_.clockRtt=client.Clock().Rtt();snapshot_.clockOffset=client.Clock().Offset();
                snapshot_.players=client.Players(); snapshot_.error=client.Error();
                snapshot_.chats=client.Chats();
                snapshot_.entities=client.Entities();snapshot_.pickups=client.Pickups();
                snapshot_.radar=client.Radar(); snapshot_.mission=client.Mission(); snapshot_.vote=client.Vote(); snapshot_.hasMission=client.HasMission();
                auto events=client.TakeEvents();
                if (events.size()+incomingEvents_.size()>8192) {
                    client.Disconnect(); snapshot_.status=Connection::Offline; snapshot_.error="Game event queue overflow";
                } else for(auto& event:events) incomingEvents_.push_back(std::move(event));
            }
        }
        Sleep(5);
    }
    client.Disconnect();
}
}
