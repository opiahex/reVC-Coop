#pragma once
#include "net/BackgroundClient.h"
#include <map>
#include <deque>
#include <iosfwd>
class CPickup;
namespace vccoop {
class NativePickups {
    struct Binding {int32_t handle{-1};bool allocated{};uint32_t revision{};};
    std::map<uint32_t,Binding> bindings_;
    std::map<uint32_t,uint64_t> announced_,requested_;
    std::map<uint32_t,std::pair<uint32_t,uint32_t>> origins_;
    std::deque<vc::Message> grants_;
    std::map<uint32_t,vc::Pickup> states_;
    vc::BackgroundClient* network_{};vc::BackgroundClient::Snapshot net_;
    uint64_t scanAt_{};bool applying_{};uint64_t awarded_{},requests_{};
public:
    void Tick(const vc::BackgroundClient::Snapshot&,vc::BackgroundClient&);
    void Clear();bool Managed(CPickup*) const;bool Request(CPickup*,bool ammo);
    void Created(CPickup*);void Removing(CPickup*);void Event(const vc::Message&);
    void Diagnostics(std::ostream&) const;
};
}
