#pragma once
#include "Protocol.h"
namespace vc {
// Critical state and dependent combat events share the ordered reliable channel.
inline bool CriticalTransition(const PlayerState& a,const PlayerState& b) {
    if(a.life!=b.life || a.model!=b.model || a.area!=b.area || a.flags!=b.flags
        || (a.health>0)!=(b.health>0) || a.vehicle!=b.vehicle || a.seat!=b.seat || a.weapon!=b.weapon || a.actions!=b.actions || a.wanted!=b.wanted
        || a.aimKind!=b.aimKind || a.aimTarget!=b.aimTarget || a.aimGeneration!=b.aimGeneration || a.aimLife!=b.aimLife) return true;
    auto vehicleStage=[](uint16_t state){return state==50 || (state>=56 && state<=61);};
    if(a.pedState!=b.pedState && (vehicleStage(a.pedState) || vehicleStage(b.pedState))) return true;
    const float dx=a.position.x-b.position.x,dy=a.position.y-b.position.y,dz=a.position.z-b.position.z;
    return dx*dx+dy*dy+dz*dz>625;
}
}
