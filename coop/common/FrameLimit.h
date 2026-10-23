#pragma once
#include <charconv>
#include <string_view>

namespace vc {
inline constexpr int kDefaultOnlineFps = 60;
inline int ParseFrameLimit(std::string_view text) {
    int value{};
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return result.ec==std::errc{} && result.ptr==text.data()+text.size() && value>=0
        ? value:kDefaultOnlineFps;
}
struct FrameLimits { bool enabled{};int fps{30}; };
class SessionFrameLimit {
    bool engaged_{};FrameLimits saved_;
public:
    FrameLimits Update(bool online,int configured,FrameLimits current) {
        if(online) {
            if(!engaged_) {saved_=current;engaged_=true;}
            // Keep a valid menu/VSync fallback even when the game limiter is off.
            return {configured>0,configured>0?configured:kDefaultOnlineFps};
        }
        if(engaged_) {engaged_=false;return saved_;}
        return current;
    }
};
}
