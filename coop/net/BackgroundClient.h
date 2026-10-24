#pragma once
#include "Network.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace vc {
// Only the worker owns ENet objects. The game thread exchanges copied snapshots.
class BackgroundClient {
public:
    struct Snapshot {
        Connection status{Connection::Offline};
        uint8_t self{kNoPlayer},host{kNoPlayer},populationMask{};
        std::array<RemotePlayer,kMaxPlayers> players{};
        std::string error;
        std::deque<ChatLine> chats;
        std::map<uint32_t,RemoteEntity> entities;
        std::map<uint32_t,Pickup> pickups;
        std::array<RemoteRadar,kRadarSlots> radar{};
        MissionState mission; CutsceneVote vote; bool hasMission{};
        bool clockReady{};uint64_t clockSamples{},clockRtt{};double clockOffset{};
        Environment environment;bool hasEnvironment{};
        TransportStats transport;
    };
    ~BackgroundClient() { Stop(); }
    bool Start();
    void Stop();
    bool Connect(const std::string& address,uint16_t port,const std::string& name);
    void Disconnect();
    void Publish(const PlayerState& state);
    bool SendChat(const std::string& text);
    bool SendEvent(const Message&);
    std::deque<GameEvent> TakeEvents();
    bool SendEntity(const Message&);
    void PublishEntities(std::vector<Entity>);
    Snapshot Read();
private:
    struct Command { uint64_t revision{}; bool connect{}; std::string address,name; uint16_t port{}; };
    std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> stopping_{};
    Command command_;
    Snapshot snapshot_;
    PlayerState local_{};
    uint64_t sampled_{};
    std::deque<std::string> outgoing_;
    std::deque<Message> outgoingEvents_;
    std::deque<GameEvent> incomingEvents_;
    std::vector<Entity> localEntities_;
    uint64_t entitySampled_{};
    void Run();
};
}
