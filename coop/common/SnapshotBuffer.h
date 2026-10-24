#pragma once
#include <algorithm>
#include <cmath>
#include <deque>

namespace vc {
// Adapted from sa-coop's SnapshotBuffer (Copyright (c) 2026 Karimov-Akbar,
// MIT; see vendor/sa-coop.LICENSE). Capture times are already mapped through
// our synchronized clock, so arrival jitter changes delay, never sample spacing.
template<class T> class SnapshotBuffer {
public:
    struct Sample {double time;T state;};
private:
    std::deque<Sample> samples_;
    double arrived_{},jitter_{};
    mutable double requested_{-1},played_{-1};
public:
    void clear() {samples_.clear();arrived_=jitter_=0;requested_=played_=-1;}
    bool empty() const {return samples_.empty();}
    size_t size() const {return samples_.size();}
    const Sample& front() const {return samples_.front();}
    const Sample& back() const {return samples_.back();}
    const Sample& operator[](size_t i) const {return samples_[i];}
    bool Push(double capture,const T& state,double arrival=0) {
        if(!samples_.empty() && capture<=samples_.back().time) return false;
        if(arrival>0 && arrived_>0 && !samples_.empty()) {
            const double deviation=std::abs((arrival-arrived_)-(capture-samples_.back().time));
            jitter_+=(std::min(.25,deviation)-jitter_)*.125;
        }
        arrived_=arrival;samples_.push_back({capture,state});
        while(samples_.size()>32) samples_.pop_front();
        return true;
    }
    double Delay() const {return std::clamp(.1+jitter_*2.5,.1,.18);}
    double Playout(double requested) const {
        double target=requested-(Delay()-.1);
        // Increasing the jitter margin must not play the character backwards.
        if(requested>=requested_) target=std::max(target,played_);
        requested_=requested;played_=target;return target;
    }
};
}
