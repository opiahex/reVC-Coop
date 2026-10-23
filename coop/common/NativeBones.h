#pragma once
#include <cstdint>
#include <cstddef>
namespace vc {
// VC x86 AnimBlendFrameData. The owning clump supplies this contiguous array;
// CPed's node lookup has an uninitialized slot 0 and must not be scanned.
struct NativeBoneFrame {
    uint32_t flags{}; float reset[3]{}; void* interpolation{}; uint32_t tag{};
};
static_assert(sizeof(NativeBoneFrame)==0x18 && offsetof(NativeBoneFrame,tag)==0x14);
inline void* FindNativeBone(const NativeBoneFrame* frames,int count,uint32_t tag) {
    if(!frames || count<=0 || count>128) return nullptr;
    for(int i=0;i<count;++i) if(frames[i].tag==tag) return frames[i].interpolation;
    return nullptr;
}
}
