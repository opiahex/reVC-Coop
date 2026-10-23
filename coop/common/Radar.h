#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace vc {
inline constexpr size_t kRadarSlots=75;
struct RadarTrace {
    uint8_t slot{},active{},type{4},display{2},flags{};
    uint16_t generation{},sprite{},scale{1}; uint32_t colour{},entity{};
    std::array<float,3> position{};
    bool operator==(const RadarTrace&) const=default;
};
inline bool ValidRadar(const RadarTrace& r) {
    if(r.slot>=kRadarSlots || r.active>1 || r.type<1 || r.type>5 || r.display>3 || r.flags>3 || r.sprite>=40 || !r.scale || r.scale>16) return false;
    for(float f:r.position) if(!std::isfinite(f) || std::abs(f)>20000) return false;
    return !r.active || r.generation;
}
struct RemoteRadar { RadarTrace trace; uint32_t sequence{}; bool known{}; };
inline bool RadarPlayerVisible(bool active,bool ready,float health,uint8_t ownArea,uint8_t remoteArea,uint64_t age) {
    return active && ready && health>0 && ownArea==remoteArea && age<3000;
}
}
