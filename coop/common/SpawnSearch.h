#pragma once
#include "Protocol.h"
#include <optional>
#include <numbers>
namespace vc {
// Query must project onto loaded ground and reject obstructed body/head space.
// No untested fallback coordinate is ever returned.
template<class Query>
std::optional<Vec3> FindNearbySpawn(Vec3 anchor,float heading,const std::vector<Vec3>& occupied,Query query) {
    for(float radius:{1.6f,2.4f,3.6f,5.2f,8.f}) for(unsigned i=0;i<16;++i) {
        float angle=heading+std::numbers::pi_v<float>+float(i)*std::numbers::pi_v<float>/8;
        Vec3 candidate{anchor.x+std::cos(angle)*radius,anchor.y+std::sin(angle)*radius,anchor.z},grounded;
        if(!query(candidate,grounded) || !ValidPoint(grounded) || std::abs(grounded.z-anchor.z)>1.75f) continue;
        bool free=true;
        for(auto p:occupied) { float dx=p.x-grounded.x,dy=p.y-grounded.y; if(dx*dx+dy*dy<1.2f*1.2f && std::abs(p.z-grounded.z)<2) free=false; }
        if(free) return grounded;
    }
    return std::nullopt;
}
}
