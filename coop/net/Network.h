#pragma once
#include "common/Protocol.h"
#include "common/PlayerTransitions.h"
#include <enet/enet.h>
#include <chrono>
#include <functional>
#include <map>
#include <deque>

namespace vc {
inline uint64_t NowMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
class NetworkRuntime {
    bool ok_{enet_initialize()==0};
public:
    ~NetworkRuntime() { if (ok_) enet_deinitialize(); }
    bool Ok() const { return ok_; }
};
bool SendPacket(ENetPeer*,const Message&,uint8_t channel=0,bool reliable=true);
class Server {
    struct Peer {
        uint8_t id{kNoPlayer}; bool rejected{},hasState{};
        std::string name; uint64_t connected{},window{}; unsigned states{},controls{};
        uint32_t sequence{},epoch{},arrestedLife{}; Message lastState{};
        uint64_t chatWindow{}; unsigned chats{};
        unsigned hits{},shots{},entityUpdates{},entityControls{},missionEvents{},clocks{},crimes{},pickups{};
    };
    ENetHost* socket_{};
    std::map<ENetPeer*,Peer> peers_;
    uint8_t host_{kNoPlayer}; uint32_t nextEpoch_{};
    struct SeatLease { uint8_t player{kNoPlayer}; uint32_t epoch{}; uint64_t until{}; };
    struct SharedEntity { Entity state; uint32_t sequence{}; uint64_t updated{},claimUntil{}; std::array<SeatLease,9> seats{};uint8_t heliAttacker{kNoPlayer};uint32_t heliEpoch{},heliLife{};bool heliRewarded{},heliCommittedDown{};uint32_t heliDamage{}; };
    std::map<uint32_t,SharedEntity> entities_;
    uint32_t nextEntity_{},nextPickup_{};
    struct SharedPickup {Pickup state;uint64_t deadline{};};
    std::map<uint32_t,SharedPickup> pickups_;
    void ReceivePickup(ENetPeer*,Peer&,Message&);
    void TickPickups();
    uint8_t population_{};
    uint64_t transportLogAt_{};
    Message environment_;bool hasEnvironment_{};uint64_t environmentAt_{};
    Message missionState_{}; bool hasMission_{}; uint32_t missionSequence_{};
    MissionBarrier missionBarrier_; uint64_t barrierAt_{};
    CutsceneVote vote_; uint64_t voteAt_{};
    std::array<Message,kRadarSlots> radar_{}; std::array<bool,kRadarSlots> hasRadar_{};
    void SendVote();
    void ReceiveMission(ENetPeer*,Peer&,Message&,uint8_t channel);
    void ReleaseMissionBarrier();
    void RefreshPopulation();
    void ReceiveEntity(ENetPeer*,Peer&,Message&,uint8_t channel);
    bool AttackOrigin(const Peer&,uint32_t source,uint32_t generation,Vec3&,uint8_t& area) const;
    void Broadcast(const Message&,ENetPeer* except=nullptr,uint8_t channel=0,bool reliable=true);
    void Remove(ENetPeer*);
    void Reject(ENetPeer*,Reason);
    void Receive(ENetPeer*,const ENetEvent&);
public:
    std::function<void(const std::string&)> log;
    ~Server() { Stop(); }
    bool Start(uint16_t port,const std::string& bind="0.0.0.0");
    void Stop();
    void Poll(uint32_t wait=0);
    uint16_t Port() const { return socket_ ? socket_->address.port : 0; }
    size_t PlayerCount() const;
};
enum class Connection { Offline, Connecting, Handshake, Online };
struct RemotePlayer {
    bool active{},hasState{};
    std::string name; PlayerState state{}; uint32_t sequence{},epoch{}; uint64_t received{},generation{};
};
struct ChatLine { uint64_t serial{},time{}; uint8_t sender{}; std::string name,text; };
struct GameEvent { uint64_t received{}; Message message; };
struct RemoteEntity { Entity state; uint32_t sequence{}; uint64_t received{}; };
struct TransportStats {uint32_t rtt{},variance{},throttle{},loss{},reliableBytes{},queued{};};
class Client {
    NetworkClock clock_;
    uint64_t clockAt_{};uint32_t clockSequence_{};
    std::map<uint32_t,uint64_t> clockPending_;
    Environment environment_;bool hasEnvironment_{};uint32_t environmentSequence_{};
    ENetHost* socket_{}; ENetPeer* server_{};
    Connection status_{Connection::Offline};
    uint8_t self_{kNoPlayer},host_{kNoPlayer},population_{};
    uint32_t sequence_{}; uint64_t deadline_{},generation_{};
    PlayerState published_{}; bool hasPublished_{};
    std::string name_,error_;
    std::array<RemotePlayer,kMaxPlayers> players_{};
    std::deque<ChatLine> chats_;
    uint64_t chatSerial_{};
    std::deque<GameEvent> events_;
    std::map<uint32_t,RemoteEntity> entities_;
    std::map<uint32_t,Pickup> pickups_;
    MissionState mission_{}; CutsceneVote vote_; bool hasMission_{}; uint32_t missionSequence_{};
    std::array<RemoteRadar,kRadarSlots> radar_{};
    void Receive(std::span<const uint8_t>,uint8_t channel);
    void Fail(const std::string&);
public:
    ~Client() { Disconnect(); }
    bool Connect(const std::string& address,uint16_t port,const std::string& name);
    void Disconnect();
    void Poll();
    bool NeedsTransition(const PlayerState& s) const { return !hasPublished_ || CriticalTransition(published_,s); }
    bool SendState(const PlayerState&,uint16_t sampleAgeMs=0);
    const auto& Clock() const {return clock_;}
    TransportStats Transport() const {
        return server_?TransportStats{server_->roundTripTime,server_->roundTripTimeVariance,server_->packetThrottle,server_->packetLoss,server_->reliableDataInTransit,uint32_t(enet_list_size(&server_->outgoingCommands))}:TransportStats{};
    }
    const auto& EnvironmentState() const {return environment_;}
    bool HasEnvironment() const {return hasEnvironment_;}
    bool SendChat(const std::string&);
    bool SendEvent(const Message&);
    bool SendEntity(const Message&);
    const auto& Entities() const { return entities_; }
    const auto& Pickups() const { return pickups_; }
    const auto& Radar() const { return radar_; }
    const auto& Mission() const { return mission_; }
    const auto& Vote() const { return vote_; }
    bool HasMission() const { return hasMission_; }
    std::deque<GameEvent> TakeEvents() { std::deque<GameEvent> result; result.swap(events_); return result; }
    const std::deque<ChatLine>& Chats() const { return chats_; }
    Connection Status() const { return status_; }
    uint8_t Self() const { return self_; }
    uint8_t Host() const { return host_; }
    uint8_t PopulationMask() const { return population_; }
    const auto& Players() const { return players_; }
    const std::string& Error() const { return error_; }
};
}
