#pragma once
#include "Protocol.h"
#include <map>
namespace vc {
inline Vec3 Limited(Vec3 v,float maximum) {
    const float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return length>maximum ? Vec3{v.x*maximum/length,v.y*maximum/length,v.z*maximum/length} : v;
}
class ContactHistory {
    std::map<std::pair<uint32_t,uint32_t>,uint64_t> native_;
public:
    void Native(uint32_t source,uint32_t target,uint64_t now) { native_[{source,target}]=now; }
    bool Resolved(uint32_t source,uint32_t target,uint64_t now) const {
        auto it=native_.find({source,target}); return it!=native_.end() && now-it->second<120;
    }
    void Prune(uint64_t now) { for(auto it=native_.begin();it!=native_.end();) it=now-it->second>10000 ? native_.erase(it) : std::next(it); }
    void Clear() { native_.clear(); }
};
}
