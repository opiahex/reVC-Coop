#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace vc {
inline constexpr uint64_t kMaxNetworkTime=uint64_t(1)<<52;
// Four monotonic timestamps; remote processing time is excluded from the RTT.
// Prefer the least queued recent exchange. After acquisition, slew the mapping
// instead of jumping all remote entities when an individual reply is delayed.
class NetworkClock {
    struct Sample { double offset;uint64_t rtt; };
    std::deque<Sample> samples_;
    double offset_{};uint64_t rtt_{},accepted_{};
public:
    void Clear() {samples_.clear();offset_=0;rtt_=accepted_=0;}
    bool Ready() const {return accepted_!=0;}
    uint64_t Samples() const {return accepted_;}
    uint64_t Rtt() const {return rtt_;}
    double Offset() const {return offset_;}
    bool Observe(uint64_t sent,uint64_t serverIn,uint64_t serverOut,uint64_t received) {
        if(!sent || !serverIn || sent>received || serverIn>serverOut ||
           received>kMaxNetworkTime || serverOut>kMaxNetworkTime || received-sent>5000 ||
           serverOut-serverIn>received-sent) return false;
        auto rtt=(received-sent)-(serverOut-serverIn);
        const double offset=((double(serverIn)-double(sent))+(double(serverOut)-double(received)))*.5;
        samples_.push_back({offset,rtt});if(samples_.size()>16) samples_.pop_front();
        auto best=std::min_element(samples_.begin(),samples_.end(),[](auto a,auto b){return a.rtt<b.rtt;});
        rtt_=best->rtt;
        offset_=accepted_<4 ? best->offset : offset_+std::clamp((best->offset-offset_)*.125,-1.,1.);
        ++accepted_;return true;
    }
    uint64_t ServerTime(uint64_t local) const {
        if(!Ready()) return 0;
        return uint64_t(std::clamp(std::round(double(local)+offset_),1.,double(kMaxNetworkTime)));
    }
    uint64_t LocalTime(uint64_t server,uint64_t arrival,uint16_t staging=0) const {
        if(!Ready() || !server) return arrival-std::min<uint64_t>(arrival,staging);
        return uint64_t(std::clamp(std::round(double(server)-offset_),double(arrival>2000?arrival-2000:0),double(arrival)));
    }
};
}
