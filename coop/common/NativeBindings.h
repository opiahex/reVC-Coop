#pragma once
#include <unordered_map>
namespace vc {
// A native pool object may never point at two independently owned shadow tables.
class NativeBindings {
    std::unordered_map<void*,const void*> owners_;
public:
    bool Claim(void* native,const void* owner) {
        if(!native || !owner) return false;
        auto [it,inserted]=owners_.try_emplace(native,owner);
        return inserted || it->second==owner;
    }
    void Release(void* native,const void* owner) {
        auto it=owners_.find(native); if(it!=owners_.end() && it->second==owner) owners_.erase(it);
    }
};
}
