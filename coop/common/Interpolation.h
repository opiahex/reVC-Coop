#pragma once
#include "Protocol.h"
#include <algorithm>
#include <deque>
#include <numbers>

namespace vc {
inline float AngleLerp(float a, float b, float t) {
    float delta=std::remainder(b-a,2*std::numbers::pi_v<float>); return a+delta*t;
}
inline Quaternion BlendQuaternion(Quaternion a,Quaternion b,float t) {
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0) { b={-b.x,-b.y,-b.z,-b.w}; dot=-dot; }
    float x=1-t,y=t;
    if(dot<.9995f) { const float angle=std::acos(std::clamp(dot,-1.f,1.f)),denom=std::sin(angle); x=std::sin((1-t)*angle)/denom; y=std::sin(t*angle)/denom; }
    Quaternion q{a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y,a.w*x+b.w*y};
    const float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return length>.00001f ? Quaternion{q.x/length,q.y/length,q.z/length,q.w/length} : Quaternion{};
}
class Interpolator {
    struct Sample { double time; PlayerState state; };
    std::deque<Sample> samples_;
public:
    void Clear() { samples_.clear(); }
    bool Empty() const { return samples_.empty(); }
    void Push(double time, const PlayerState& s) {
        if (!samples_.empty()) {
            const auto& p=samples_.back().state.position;
            float dx=p.x-s.position.x,dy=p.y-s.position.y,dz=p.z-s.position.z;
            const auto& last=samples_.back().state;
            if (time<samples_.back().time || last.area!=s.area || last.life!=s.life || last.vehicle!=s.vehicle
                || ((last.flags^s.flags)&kInVehicle) || dx*dx+dy*dy+dz*dz>625) Clear();
        }
        samples_.push_back({time,s}); while (samples_.size()>32) samples_.pop_front();
    }
    PlayerState At(double time) const {
        if (samples_.empty()) return {};
        if (time<=samples_.front().time) return samples_.front().state;
        for (size_t i=1;i<samples_.size();++i) {
            if (time>samples_[i].time) continue;
            const auto& a=samples_[i-1]; const auto& b=samples_[i];
            float t=b.time>a.time ? float((time-a.time)/(b.time-a.time)) : 1;
            auto s=t<1 ? a.state : b.state;
            s.position={std::lerp(a.state.position.x,b.state.position.x,t),std::lerp(a.state.position.y,b.state.position.y,t),std::lerp(a.state.position.z,b.state.position.z,t)};
            s.heading=AngleLerp(a.state.heading,b.state.heading,t);
            for(size_t j=0;j<s.bones.size();++j) if((a.state.boneMask&b.state.boneMask)&(1u<<j))
                s.bones[j]=BlendQuaternion(a.state.bones[j],b.state.bones[j],t);
            for (size_t j=0;j<s.animationCount && t<1;++j) {
                auto& anim=s.animations[j]; bool matched=false;
                for (size_t k=0;k<b.state.animationCount;++k) {
                    const auto& next=b.state.animations[k];
                    if (anim.group!=next.group || anim.id!=next.id) continue;
                    // Loop/restart boundaries must not run the animation backwards.
                    anim.time=next.time>=anim.time && next.time-anim.time<.5f
                        ? std::lerp(anim.time,next.time,t) : anim.time+float(time-a.time)*anim.speed;
                    anim.blend=std::lerp(anim.blend,next.blend,t); matched=true; break;
                }
                if (!matched && (anim.flags&1)) anim.time+=float(time-a.time)*anim.speed;
            }
            return s;
        }
        auto s=samples_.back().state;
        float dt=float(std::clamp(time-samples_.back().time,0.0,0.1));
        s.position.x+=s.velocity.x*dt; s.position.y+=s.velocity.y*dt; s.position.z+=s.velocity.z*dt;
        for(size_t i=0;i<s.animationCount;++i) if(s.animations[i].flags&1) s.animations[i].time+=dt*s.animations[i].speed;
        return s;
    }
};
}
