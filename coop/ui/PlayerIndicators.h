#pragma once
#include "Renderer.h"
#include "net/Network.h"
#include <array>
namespace vc::ui {
struct PlayerIndicator {
    uint8_t id{}; uint64_t generation{};
    float x{},y{},distance{},health{},armour{};
    uint8_t weapon{};
    void* weaponIcon{};
    std::string name;
    uint8_t wanted{};void* wantedIcon{};
};
inline std::wstring PlainBubble(std::string_view text) {
    // Chat colour markup is not interpreted inside speech bubbles.
    std::wstring result=Widen(text);
    for(auto& c:result) if(c==L'{' || c==L'}') c=L' ';
    return result;
}
}
