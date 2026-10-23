#pragma once
#include "Interpolation.h"
namespace vc {
inline Vec3 Mix(Vec3 a,Vec3 b,float t) { return {std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)}; }
inline Vec3 Cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline Vec3 Unit(Vec3 a,Vec3 fallback={1,0,0}) {
    float length=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
    return length>.00001f ? Vec3{a.x/length,a.y/length,a.z/length} : fallback;
}

inline Quaternion Rotation(const Transform& t) {
    float m[3][3]={{t.right.x,t.forward.x,t.up.x},{t.right.y,t.forward.y,t.up.y},{t.right.z,t.forward.z,t.up.z}};
    Quaternion q; float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) { float s=std::sqrt(trace+1)*2; q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,.25f*s }; }
    else {
        int i=m[1][1]>m[0][0] ? 1 : 0; if(m[2][2]>m[i][i]) i=2;
        const int j=(i+1)%3,k=(i+2)%3; float s=std::sqrt(std::max(.000001f,1+m[i][i]-m[j][j]-m[k][k]))*2;
        float values[3]{}; values[i]=.25f*s; values[j]=(m[j][i]+m[i][j])/s; values[k]=(m[k][i]+m[i][k])/s;
        q={values[0],values[1],values[2],(m[k][j]-m[j][k])/s};
    }
    return q;
}
inline Transform BlendTransform(const Transform& a,const Transform& b,float t) {
    auto q=Rotation(a),r=Rotation(b);
    float dot=q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w;
    if(dot<0) { r={-r.x,-r.y,-r.z,-r.w}; dot=-dot; }
    float x=1-t,y=t;
    if(dot<.9995f) { float angle=std::acos(std::clamp(dot,-1.f,1.f)),denom=std::sin(angle); x=std::sin((1-t)*angle)/denom; y=std::sin(t*angle)/denom; }
    q={q.x*x+r.x*y,q.y*x+r.y*y,q.z*x+r.z*y,q.w*x+r.w*y};
    float norm=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    q={q.x/norm,q.y/norm,q.z/norm,q.w/norm};
    Transform result; result.position=Mix(a.position,b.position,t); result.velocity=Mix(a.velocity,b.velocity,t);
    result.right={1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.z*q.w),2*(q.x*q.z-q.y*q.w)};
    result.forward={2*(q.x*q.y-q.z*q.w),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.x*q.w)};
    result.up={2*(q.x*q.z+q.y*q.w),2*(q.y*q.z-q.x*q.w),1-2*(q.x*q.x+q.y*q.y)};
    return result;
}
// Resolve phase wrapping using measured angular velocity, including multiple
// turns between 10 Hz snapshots. Shortest-angle interpolation would reverse fast wheels.
inline float WheelPhase(float a,float b,float omega,float span,float t) {
    constexpr float tau=2*std::numbers::pi_v<float>;
    const float delta=b-a;
    return std::remainder(a+(delta+std::round((omega*span-delta)/tau)*tau)*t,tau);
}
class WorldInterpolator {
    struct Sample { double time; Entity state; };
    std::deque<Sample> samples_;
    Interpolator ped_;
public:
    void Clear() { samples_.clear(); ped_.Clear(); }
    void Push(double time,const Entity& e) {
        if(!samples_.empty()) {
            const auto& last=samples_.back(); const auto a=EntityPosition(last.state),b=EntityPosition(e);
            const auto dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
            if(time<last.time || last.state.generation!=e.generation || last.state.area!=e.area || dx*dx+dy*dy+dz*dz>625) Clear();
        }
        samples_.push_back({time,e}); while(samples_.size()>32) samples_.pop_front();
        if(e.kind==EntityKind::Ped) ped_.Push(time,e.ped);
    }
    Entity At(double time) const {
        if(samples_.empty()) return {};
        auto result=samples_.back().state;
        if(result.kind==EntityKind::Ped) { result.ped=ped_.At(time); return result; }
        auto matrix=[](Entity& e)->Transform&{return e.kind==EntityKind::Vehicle ? e.vehicle.transform : e.object.transform;};
        if(time<=samples_.front().time) return samples_.front().state;
        for(size_t i=1;i<samples_.size();++i) if(time<=samples_[i].time) {
            auto a=samples_[i-1].state,b=samples_[i].state;
            float t=float((time-samples_[i-1].time)/(samples_[i].time-samples_[i-1].time));
            result=t<1 ? a : b; matrix(result)=BlendTransform(matrix(a),matrix(b),t);
            if(result.kind==EntityKind::Vehicle) {
                auto& v=result.vehicle; const auto& x=a.vehicle; const auto& y=b.vehicle;
                v.steer=std::lerp(x.steer,y.steer,t); v.wheelSteer=std::lerp(x.wheelSteer,y.wheelSteer,t);v.bikeLean=std::lerp(x.bikeLean,y.bikeLean,t);
                for(size_t w=0;w<4;++w) {
                    v.wheelOmega[w]=std::lerp(x.wheelOmega[w],y.wheelOmega[w],t);
                    v.wheelAngle[w]=WheelPhase(x.wheelAngle[w],y.wheelAngle[w],(x.wheelOmega[w]+y.wheelOmega[w])*.5f,float(samples_[i].time-samples_[i-1].time),t);
                    v.wheelPosition[w]=std::lerp(x.wheelPosition[w],y.wheelPosition[w],t);
                    v.suspension[w]=std::lerp(x.suspension[w],y.suspension[w],t);
                }
            }
            return result;
        }
        auto& transform=matrix(result); float dt=float(std::clamp(time-samples_.back().time,0.,.1));
        transform.position.x+=transform.velocity.x*dt; transform.position.y+=transform.velocity.y*dt; transform.position.z+=transform.velocity.z*dt;
        if(result.kind==EntityKind::Vehicle) for(size_t w=0;w<4;++w)
            result.vehicle.wheelAngle[w]=std::remainder(result.vehicle.wheelAngle[w]+result.vehicle.wheelOmega[w]*dt,2*std::numbers::pi_v<float>);
        return result;
    }
};
}
