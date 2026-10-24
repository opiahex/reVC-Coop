#pragma once
#include <algorithm>
#include <cmath>
namespace vc {
class FixedStep {
    double pending_{};bool active_{};
public:
    static constexpr double Seconds=1./30.;
    unsigned Advance(double elapsed,bool active) {
        if(!active) {pending_=0;active_=false;return 1;}
        if(!active_) {active_=true;pending_=0;return 1;}
        if(!std::isfinite(elapsed) || elapsed<0) elapsed=0;
        pending_=std::min(pending_+elapsed,.25);
        unsigned steps=std::min(4u,unsigned((pending_+1.e-9)/Seconds));
        pending_=std::max(0.,pending_-steps*Seconds);return steps;
    }
    float Alpha() const {return float(std::clamp(pending_/Seconds,0.,1.));}
};
}
