#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace vc {
// A single host owns weather progression. Particle positions remain local to
// each camera, but their weather inputs and lightning events are shared.
struct Environment {
    uint8_t hour{12},minute{},second{},oldWeather{4},weather{4},flags{},thunderDuration{};
    int16_t forcedWeather{-1};
    uint32_t thunderSerial{},minuteLength{1000};uint16_t thunderDelay{};
    // blend, rain, wind, wet roads, clouds, fog, sun, rainbow, glare,
    // clipped wind, traffic-light brightness
    std::array<float,11> values{};
};
inline bool ValidEnvironment(const Environment& e) {
    if(e.hour>=24 || e.minute>=60 || e.second>=60 || e.oldWeather>=6 || e.weather>=6 ||
       e.forcedWeather < -1 || e.forcedWeather>=6 || e.flags>7 || e.thunderDuration>20 ||
       e.thunderDelay>5000 || e.minuteLength<1 || e.minuteLength>3600000) return false;
    for(unsigned i=0;i<e.values.size();++i)
        if(!std::isfinite(e.values[i]) || e.values[i]<0 || e.values[i]>(i==2?4.f:1.f)) return false;
    return true;
}
}
