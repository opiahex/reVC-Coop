#pragma once
#include "Protocol.h"
namespace vc {
inline float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 MuzzleLocal(const Transform& t,Vec3 p) {
    Vec3 d{p.x-t.position.x,p.y-t.position.y,p.z-t.position.z};
    return {Dot(d,t.right),Dot(d,t.forward),Dot(d,t.up)};
}
inline Vec3 MuzzleWorld(const Transform& t,Vec3 p) {
    return {t.position.x+t.right.x*p.x+t.forward.x*p.y+t.up.x*p.z,
        t.position.y+t.right.y*p.x+t.forward.y*p.y+t.up.y*p.z,
        t.position.z+t.right.z*p.x+t.forward.z*p.y+t.up.z*p.z};
}
struct Flash { float offset{},size{}; };
struct MuzzlePlan {
    std::array<Flash,7> flashes{}; unsigned count{}; int particle{16};
    bool large{}; float shell{}; unsigned smokeCount{}; float smokeSize{};
};
inline MuzzlePlan WeaponMuzzle(uint8_t weapon) {
    MuzzlePlan p;
    if(weapon==17 || weapon==18 || weapon==28 || weapon==29) {
        p.flashes={Flash{0,.06f},Flash{.06f,.04f},Flash{.10f,.02f}};
        p.count=3; p.shell=.015f; p.smokeCount=3;
    } else if(weapon>=22 && weapon<=25) {
        p.flashes={Flash{0,.07f},Flash{.06f,.05f},Flash{.10f,.04f},Flash{.14f,.03f},Flash{.17f,.03f},Flash{.20f,.02f},Flash{.22f,.01f}};
        p.count=7; p.shell=.015f; p.smokeCount=1;
    } else if(weapon>=19 && weapon<=21) {
        p.flashes={Flash{.1f,0},Flash{.2f,.15f},Flash{.3f,.2f},Flash{0,0}};
        p.count=4; p.particle=15; p.smokeCount=4; p.smokeSize=.1f;
    } else if(weapon==26 || weapon==27 || weapon==32 || weapon==33 || weapon==35) p.large=true;
    return p;
}
}
