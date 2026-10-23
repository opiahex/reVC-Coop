#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace vc {
inline constexpr size_t kScriptSpaceSize=260512,kMissionArgs=12,kMissionCounters=16;
struct ScriptOp { uint16_t opcode; const char* signature; const char* name; };
inline constexpr ScriptOp kScriptOps[]{
    {0x0003,"v","SHAKE_CAM"},
    {0x015F,"ffffff","SET_FIXED_CAMERA_POSITION"},
    {0x0160,"fffv","POINT_CAMERA_AT_POINT"},
    {0x0158,"Cvv","POINT_CAMERA_AT_CAR"},
    {0x0159,"Pvv","POINT_CAMERA_AT_CHAR"},
    {0x015A,"","RESTORE_CAMERA"},
    {0x02EB,"","RESTORE_CAMERA_JUMPCUT"},
    {0x0373,"","SET_CAMERA_BEHIND_PLAYER"},
    {0x03C8,"","SET_CAMERA_IN_FRONT_OF_PLAYER"},
    {0x0460,"vv","SET_INTERPOLATION_PARAMETERS"},
    {0x016A,"vv","DO_FADE"},
    {0x0169,"vvv","SET_FADING_COLOUR"},
    {0x02A3,"v","SWITCH_WIDESCREEN"},
    {0x01B4,"iv","SET_PLAYER_CONTROL"},
    {0x03EF,"i","MAKE_PLAYER_SAFE_FOR_CUTSCENE"},
    {0x0336,"iv","SET_PLAYER_VISIBLE"},
    {0x03BF,"iv","SET_EVERYONE_IGNORE_PLAYER"},
    {0x00BA,"lvv","PRINT_BIG"},
    {0x00BB,"lvv","PRINT"},
    {0x00BC,"lvv","PRINT_NOW"},
    {0x00BE,"","CLEAR_PRINTS"},
    {0x03E5,"l","PRINT_HELP"},
    {0x03E6,"","CLEAR_HELP"},
    {0x01E3,"lvvv","PRINT_WITH_NUMBER_BIG"},
    {0x01E4,"lvvv","PRINT_WITH_NUMBER"},
    {0x01E5,"lvvv","PRINT_WITH_NUMBER_NOW"},
    {0x02FD,"lvvvv","PRINT_WITH_2_NUMBERS_NOW"},
    {0x03D5,"l","CLEAR_THIS_PRINT"},
    {0x03D6,"l","CLEAR_THIS_BIG_PRINT"},
    {0x03EB,"","CLEAR_SMALL_PRINTS"},
    {0x02E4,"l","LOAD_CUTSCENE"},
    {0x0244,"fff","SET_CUTSCENE_OFFSET"},
    {0x02E5,"mQ","CREATE_CUTSCENE_OBJECT"},
    {0x02E6,"ql","SET_CUTSCENE_ANIM"},
    {0x02F4,"qmQ","CREATE_CUTSCENE_HEAD"},
    {0x02F5,"ql","SET_CUTSCENE_HEAD_ANIM"},
    {0x04BC,"l","SET_CUTSCENE_ANIM_TO_LOOP"},
    {0x02E7,"","START_CUTSCENE"},
    {0x02EA,"","CLEAR_CUTSCENE"},
    {0x0569,"l","LOAD_UNCOMPRESSED_ANIM"},
    {0x023C,"vl","LOAD_SPECIAL_CHARACTER"},
    {0x0296,"v","UNLOAD_SPECIAL_CHARACTER"},
    {0x02F3,"ml","LOAD_SPECIAL_MODEL"},
    {0x0247,"m","REQUEST_MODEL"},
    {0x0249,"m","MARK_MODEL_AS_NO_LONGER_NEEDED"},
    {0x038B,"","LOAD_ALL_MODELS_NOW"},
    {0x03CB,"fff","LOAD_SCENE"},
    {0x04E4,"ff","REQUEST_COLLISION"},
    {0x04BB,"v","SET_AREA_VISIBLE"},
    {0x041D,"f","SET_NEAR_CLIP"},
    {0x054C,"l","LOAD_MISSION_TEXT"},
    {0x03CF,"al","LOAD_MISSION_AUDIO"},
    {0x03D1,"a","PLAY_MISSION_AUDIO"},
    {0x040D,"a","CLEAR_MISSION_AUDIO"},
    {0x03D7,"afff","SET_MISSION_AUDIO_POSITION"},
    {0x018C,"fffv","ADD_ONE_OFF_SOUND"},
    {0x0394,"v","PLAY_MISSION_PASSED_TUNE"},
    {0x0186,"CB","ADD_BLIP_FOR_CAR"},
    {0x0187,"PB","ADD_BLIP_FOR_CHAR"},
    {0x0188,"OB","ADD_BLIP_FOR_OBJECT"},
    {0x018A,"fffB","ADD_BLIP_FOR_COORD"},
    {0x02A7,"fffvB","ADD_SPRITE_BLIP_FOR_CONTACT_POINT"},
    {0x02A8,"fffvB","ADD_SPRITE_BLIP_FOR_COORD"},
    {0x04CE,"fffvB","ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD"},
    {0x0164,"b","REMOVE_BLIP"},
    {0x0165,"bv","CHANGE_BLIP_COLOUR"},
    {0x018B,"bv","CHANGE_BLIP_DISPLAY"},
    {0x0168,"bv","CHANGE_BLIP_SCALE"},
    {0x0166,"bv","DIM_BLIP"},
    {0x014E,"gv","DISPLAY_ONSCREEN_TIMER"},
    {0x014F,"g","CLEAR_ONSCREEN_TIMER"},
    {0x0150,"gv","DISPLAY_ONSCREEN_COUNTER"},
    {0x0151,"g","CLEAR_ONSCREEN_COUNTER"},
    {0x03C3,"gvl","DISPLAY_ONSCREEN_TIMER_WITH_STRING"},
    {0x03C4,"gvl","DISPLAY_ONSCREEN_COUNTER_WITH_STRING"},
    {0x04F7,"gvvl","DISPLAY_NTH_ONSCREEN_COUNTER_WITH_STRING"},
    {0x0396,"v","FREEZE_ONSCREEN_TIMER"},
    {0x0055,"ifff","SET_PLAYER_COORDINATES"},
    {0x012A,"ifff","WARP_PLAYER_FROM_CAR_TO_COORD"},
    {0x0171,"if","SET_PLAYER_HEADING"},
    {0x01B1,"iwv","GIVE_WEAPON_TO_PLAYER"},
    {0x01B8,"iw","SET_CURRENT_PLAYER_WEAPON"},
    {0x0109,"iv","ADD_SCORE"},
};
inline const ScriptOp* FindScriptOp(uint16_t opcode) {
    for(const auto& op:kScriptOps) if(op.opcode==opcode) return &op;
    return nullptr;
}
struct MissionArg { uint32_t value{},extra{}; bool operator==(const MissionArg&) const=default; };
struct MissionCommand {
    uint32_t run{1},ip{}; uint16_t opcode{};
    std::array<MissionArg,kMissionArgs> args{};
};
struct MissionState {
    uint32_t run{1},stage{},ip{},cutsceneMs{};
    uint16_t mission{65535}; uint8_t active{},result{},cinematic{},skipped{},counterCount{};
    uint8_t marker{},waitMask{},markerArea{},markerFlags{};
    std::array<float,6> bounds{};
    std::array<char,8> thread{};
    std::array<uint16_t,kMissionCounters> offsets{};
    std::array<uint32_t,kMissionCounters> values{};
};
inline bool ScriptLabel(const MissionArg& a) {
    std::array<char,8> text{}; std::memcpy(text.data(),&a,8); bool ended=false;
    for(unsigned char ch:text) {
        if(!ch) { ended=true; continue; }
        if(ended || !((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch=='_')) return false;
    }
    return text[0]!=0;
}
inline bool ValidMissionState(const MissionState& s) {
    if(!s.run || s.ip>=kScriptSpaceSize || (s.mission!=65535 && s.mission>1023) || s.active>1 || s.result>2 || s.cinematic>1 || s.skipped>1 || s.cutsceneMs>3600000 || s.counterCount>kMissionCounters) return false;
    if(s.marker>1 || s.waitMask>=8 || s.markerArea>32 || s.markerFlags>15 || (s.markerFlags&3)>2) return false;
    for(float f:s.bounds) if(!std::isfinite(f) || std::abs(f)>200000) return false;
    for(auto c:s.thread) if(c && (c<32 || c>126)) return false;
    for(size_t i=0;i<s.counterCount;++i) {
        if(s.offsets[i]<8 || s.offsets[i]>65532 || s.offsets[i]%4) return false;
        for(size_t j=0;j<i;++j) if(s.offsets[i]==s.offsets[j]) return false;
    }
    return true;
}
inline bool ValidMissionCommand(const MissionCommand& c) {
    const auto* op=FindScriptOp(c.opcode);
    if(!op || !c.run || c.ip>=kScriptSpaceSize) return false;
    const auto n=std::strlen(op->signature); if(n>kMissionArgs) return false;
    for(size_t i=0;i<n;++i) {
        const auto a=c.args[i]; const char k=op->signature[i];
        if(k=='l') { if(!ScriptLabel(a)) return false; continue; }
        if(a.extra) return false;
        if(k=='f' && (!std::isfinite(std::bit_cast<float>(a.value)) || std::abs(std::bit_cast<float>(a.value))>200000)) return false;
        if((k=='m' && a.value>=6500) || (k=='w' && a.value>36) || (k=='i' && a.value!=0) || (k=='a' && (a.value<1 || a.value>2))) return false;
        if(k=='g' && (a.value<8 || a.value>65532 || a.value%4)) return false;
    }
    // These indices are consumed without bounds checks in native VC handlers.
    const auto v=c.args[0].value;
    if((c.opcode==0x023C || c.opcode==0x0296) && (v<1 || v>21)) return false;
    if(c.opcode==0x04BB && v>32) return false;
    if(c.opcode==0x016A && (v>60000 || c.args[1].value>1)) return false;
    return true;
}
struct ScriptOperand { uint8_t type{}; uint16_t offset{}; };
struct ParsedScript {
    MissionCommand command;
    std::array<ScriptOperand,kMissionArgs> operands{};
    uint32_t end{};
};
// Decode SCM values without changing the live VM IP or global ScriptParams.
// Output variables are read again after the original native handler executes.
inline bool ParseScript(std::span<const uint8_t> space,std::span<const uint32_t> locals,uint32_t ip,ParsedScript& out,const char* signature=nullptr) {
    if(ip>=space.size() || space.size()-ip<2) return false;
    ParsedScript p; p.command.ip=ip; p.command.opcode=uint16_t(space[ip]|space[ip+1]<<8)&0x7FFF;
    const auto* op=FindScriptOp(p.command.opcode); if(!signature && !op) return false;
    if(!signature) signature=op->signature;
    size_t at=ip+2,index=0;
    for(const char* k=signature;*k;++k,++index) {
        if(index>=kMissionArgs || at>=space.size()) return false;
        auto& a=p.command.args[index]; auto& ref=p.operands[index];
        if(*k=='l') { if(space.size()-at<8) return false; std::memcpy(&a,space.data()+at,8); auto* label=reinterpret_cast<uint8_t*>(&a); bool ended=false; for(size_t j=0;j<8;++j) { if(!label[j]) ended=true; if(ended) label[j]=0; } at+=8; continue; }
        ref.type=space[at++];
        switch(ref.type) {
            case 1: case 6: if(space.size()-at<4) return false; std::memcpy(&a.value,space.data()+at,4); at+=4; break;
            case 2: case 3: {
                if(space.size()-at<2) return false; ref.offset=uint16_t(space[at]|space[at+1]<<8); at+=2;
                if(ref.type==2) { if(ref.offset<8 || size_t(ref.offset)+4>std::min(space.size(),size_t(65536))) return false; std::memcpy(&a.value,space.data()+ref.offset,4); }
                else { if(ref.offset>=locals.size()) return false; a.value=locals[ref.offset]; }
                break;
            }
            case 4: if(at==space.size()) return false; a.value=uint32_t(int32_t(int8_t(space[at++]))); break;
            case 5: if(space.size()-at<2) return false; a.value=uint32_t(int32_t(int16_t(space[at]|space[at+1]<<8))); at+=2; break;
            default:return false;
        }
        if((*k=='B' || *k=='Q') && ref.type!=2 && ref.type!=3) return false;
        if(*k=='g') { if(ref.type!=2) return false; a.value=ref.offset; }
    }
    p.end=uint32_t(at); out=p; return true;
}
inline std::vector<uint8_t> ScriptBytecode(const MissionCommand& c,const std::array<uint32_t,kMissionArgs>& values) {
    const auto* op=FindScriptOp(c.opcode); if(!op) return {};
    std::vector<uint8_t> out; out.reserve(128);
    auto u16=[&](uint16_t v){out.push_back(uint8_t(v)); out.push_back(uint8_t(v>>8));};
    auto u32=[&](uint32_t v){for(unsigned j=0;j<4;++j) out.push_back(uint8_t(v>>(j*8)));};
    u16(c.opcode);
    for(size_t i=0;op->signature[i];++i) {
        const char k=op->signature[i];
        if(k=='l') { const auto* p=reinterpret_cast<const uint8_t*>(&c.args[i]); out.insert(out.end(),p,p+8); }
        else if(k=='g') { out.push_back(2); u16(uint16_t(values[i])); }
        else if(k=='B' || k=='Q') { out.push_back(3); u16(uint16_t(i)); }
        else { out.push_back(1); u32(values[i]); }
    }
    return out;
}
struct MissionArea {
    float lowX{},lowY{},lowZ{},highX{},highY{},highZ{};
    uint8_t mode{}; bool threeD{},stopped{};
    bool Contains(float x,float y,float z,bool inVehicle,float speed) const {
        return (mode==0 || (mode==1 && !inVehicle) || (mode==2 && inVehicle))
            && (!stopped || speed<.5f) && x>=lowX && x<=highX && y>=lowY && y<=highY
            && (!threeD || (z>=lowZ && z<=highZ));
    }
};
inline const char* MissionAreaSignature(uint16_t op) {
    if((op>=0x00E3 && op<=0x00E8) || op==0x0056) return "iffffv";
    if((op>=0x00F5 && op<=0x00FA) || op==0x0057) return "iffffffv";
    return nullptr;
}
inline bool ParseMissionArea(const MissionCommand& c,MissionArea& out) {
    if(!MissionAreaSignature(c.opcode) || c.args[0].value) return false;
    const auto f=[&](unsigned i){return std::bit_cast<float>(c.args[i].value);};
    const bool rectangle=c.opcode==0x0056 || c.opcode==0x0057;
    MissionArea a; a.threeD=(c.opcode>=0x00F5 && c.opcode<=0x00FA) || c.opcode==0x0057;
    if(rectangle) {
        a.lowX=std::min(f(1),f(a.threeD ? 4:3)); a.highX=std::max(f(1),f(a.threeD ? 4:3));
        a.lowY=std::min(f(2),f(a.threeD ? 5:4)); a.highY=std::max(f(2),f(a.threeD ? 5:4));
        if(a.threeD) { a.lowZ=std::min(f(3),f(6)); a.highZ=std::max(f(3),f(6)); }
    } else {
        a.mode=uint8_t((c.opcode-(a.threeD ? 0x00F5:0x00E3))%3);
        a.stopped=c.opcode>=(a.threeD ? 0x00F8:0x00E6);
        a.lowX=f(1)-f(a.threeD ? 4:3); a.highX=f(1)+f(a.threeD ? 4:3);
        a.lowY=f(2)-f(a.threeD ? 5:4); a.highY=f(2)+f(a.threeD ? 5:4);
        if(a.threeD) { a.lowZ=f(3)-f(6); a.highZ=f(3)+f(6); }
    }
    for(float f:{a.lowX,a.lowY,a.lowZ,a.highX,a.highY,a.highZ}) if(!std::isfinite(f) || std::abs(f)>200000) return false;
    out=a; return true;
}
inline bool IsRadarScript(uint16_t op) {
    const auto* spec=FindScriptOp(op); if(!spec) return false;
    for(const char* p=spec->signature;*p;++p) if(*p=='b' || *p=='B') return true;
    return false;
}
inline bool IsMissionBarrier(uint16_t op) { return op==0x02E7 || op==0x03D1; }
inline bool IdempotentScript(uint16_t op) {
    switch(op) {
        case 0x0169:case 0x02A3:case 0x01B4:case 0x03EF:case 0x03BF:
        case 0x00BA:case 0x00BB:case 0x00BC:case 0x03E5:case 0x03E6:case 0x00BE:
        case 0x015F:case 0x0160:case 0x0165:case 0x018B:case 0x0168:return true;
        default:return false;
    }
}
struct MissionMember { bool ready{},alive{},inVehicle{}; uint8_t area{}; float x{},y{},z{},speed{}; };
struct GroupPresence { uint8_t arrived{},missing{}; };
inline GroupPresence MissionGroup(const MissionArea& a,uint8_t area,uint8_t members,const std::array<MissionMember,3>& players,uint8_t host) {
    GroupPresence result;
    for(uint8_t id=0;id<3;++id) if(members&(1u<<id)) {
        auto box=a; const float slack=id==host ? 0.f:1.f;
        box.lowX-=slack;box.highX+=slack;box.lowY-=slack;box.highY+=slack;box.lowZ-=slack;box.highZ+=slack;
        const auto& p=players[id];
        const bool inside=p.ready && p.alive && p.area==area && box.Contains(p.x,p.y,p.z,p.inVehicle,p.speed);
        (inside ? result.arrived:result.missing)|=uint8_t(1u<<id);
    }
    return result;
}
inline MissionArea MarkerArea(const MissionState& s) {
    return {s.bounds[0],s.bounds[1],s.bounds[2],s.bounds[3],s.bounds[4],s.bounds[5],uint8_t(s.markerFlags&3),bool(s.markerFlags&8),bool(s.markerFlags&4)};
}
struct CutsceneVote {
    uint32_t scene{}; uint8_t members{},yes{},open{},declined{255},passed{};
    bool Submit(uint8_t id,bool accept) {
        if(!scene || id>=3 || !(members&(1u<<id)) || passed) return false;
        if(!accept) { yes=0; open=0; declined=id; return true; }
        declined=255; open=1; yes|=uint8_t(1u<<id); Recount(); return true;
    }
    void Recount() { if(open && members && (yes&members)==members) { passed=1; open=0; } }
    bool Valid() const { return members<8 && yes<8 && !(yes&~members) && open<=1 && passed<=1 && (declined==255 || declined<3); }
};
// A barrier is bound to a host session and stream serial; duplicate/stale ACKs
// cannot release the next cutscene. Disconnects remove only departed members.
struct MissionBarrier {
    uint32_t token{},run{},epoch{}; uint8_t members{},ready{};
    bool Ack(uint8_t player,uint32_t serial) {
        if(player>=3 || serial!=token || !token || !(members&(1u<<player))) return false;
        ready|=uint8_t(1u<<player); return true;
    }
    bool Complete() const { return token && (ready&members)==members; }
};
}
