#pragma once
#include <cstdint>
namespace vc {
// Native VC eEntityStatus: player=0, physics=2, abandoned=4.
inline uint8_t ControlledVehicleFlags(uint8_t flags,bool owner,bool localDriver) {
    if(owner && localDriver && ((flags>>3)==2 || (flags>>3)==3 || (flags>>3)==4)) return flags&7u;
    if(!owner) return uint8_t((flags&7u)|(2u<<3));
    return flags;
}
}
