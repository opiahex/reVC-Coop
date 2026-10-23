#pragma once
#include "WorldInterpolation.h"
namespace vc {
// Vehicles take part in local contacts: their collision pose must not be the
// 100 ms historical pose used for distant pedestrian animation playback.
inline Entity PredictVehicle(Entity e,double seconds) {
    const float dt=float(std::clamp(seconds,0.,.1));
    auto& t=e.vehicle.transform;
    t.position.x+=t.velocity.x*dt;t.position.y+=t.velocity.y*dt;t.position.z+=t.velocity.z*dt;
    const auto w=e.vehicle.turn;
    const float speed=std::sqrt(w.x*w.x+w.y*w.y+w.z*w.z),angle=speed*dt;
    if(angle>.000001f) {
        const Vec3 axis{w.x/speed,w.y/speed,w.z/speed};
        auto rotate=[&](Vec3 v) {
            auto cross=Cross(axis,v);float dot=axis.x*v.x+axis.y*v.y+axis.z*v.z,c=std::cos(angle),s=std::sin(angle);
            return Vec3{v.x*c+cross.x*s+axis.x*dot*(1-c),v.y*c+cross.y*s+axis.y*dot*(1-c),v.z*c+cross.z*s+axis.z*dot*(1-c)};
        };
        t.right=rotate(t.right);t.forward=rotate(t.forward);t.up=rotate(t.up);
    }
    for(unsigned i=0;i<4;++i) e.vehicle.wheelAngle[i]=std::remainder(e.vehicle.wheelAngle[i]+e.vehicle.wheelOmega[i]*dt,2*std::numbers::pi_v<float>);
    return e;
}
class VehicleMotion {
    Entity latest_;double received_{},corrected_{};Vec3 correction_{};bool known_{};
public:
    void Clear() {known_=false;correction_={};}
    void Push(double received,const Entity& next,double now) {
        Vec3 error{};
        if(known_ && latest_.generation==next.generation && latest_.area==next.area && now-received_<.25) {
            auto before=At(now).vehicle.transform.position,after=PredictVehicle(next,now-received).vehicle.transform.position;
            error={before.x-after.x,before.y-after.y,before.z-after.z};
            if(error.x*error.x+error.y*error.y+error.z*error.z>4) error={};
        }
        latest_=next;received_=received;corrected_=now;correction_=error;known_=true;
    }
    Entity At(double now) const {
        if(!known_) return {};
        auto e=PredictVehicle(latest_,now-received_);
        const float decay=float(std::exp(-std::max(0.,now-corrected_)/.04));
        auto& p=e.vehicle.transform.position;p.x+=correction_.x*decay;p.y+=correction_.y*decay;p.z+=correction_.z*decay;
        return e;
    }
};
}
