#pragma once
#include "Protocol.h"
namespace vc {
struct PopulationViewer { bool ready{}; uint8_t area{}; Vec3 position{}; };
inline uint8_t ChoosePopulation(const std::array<PopulationViewer,kMaxPlayers>& players,uint8_t host,uint8_t previous) {
    std::array<uint8_t,kMaxPlayers> order{}; size_t size=0;
    if(host<kMaxPlayers) order[size++]=host;
    for(uint8_t id=0;id<kMaxPlayers;++id) if(id!=host) order[size++]=id;
    uint8_t selected=0;
    for(auto id:order) {
        if(!players[id].ready) continue;
        const float radius=(previous&(1u<<id)) ? 360.f : 450.f;
        bool covered=false;
        for(uint8_t source=0;source<kMaxPlayers;++source) if((selected&(1u<<source)) && players[id].area==players[source].area) {
            auto a=players[id].position,b=players[source].position; const float dx=a.x-b.x,dy=a.y-b.y;
            if(dx*dx+dy*dy<radius*radius) { covered=true; break; }
        }
        if(!covered) selected|=uint8_t(1u<<id);
    }
    return selected;
}
}
