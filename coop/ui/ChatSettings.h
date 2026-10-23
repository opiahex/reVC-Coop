#pragma once
#include <algorithm>
#include <string>
namespace vc::ui {
struct ChatSettings {
    int fontSize{13},pageSize{12};
    static constexpr int width=640,outline=2;
    void Normalize() {
        fontSize=std::clamp(fontSize,10,32); pageSize=std::clamp(pageSize,5,20);
    }
    const wchar_t* Face() const { return L"Arial"; }
    bool operator==(const ChatSettings&) const=default;
};
ChatSettings LoadChatSettings(const std::wstring& ini);
void SaveChatSettings(const std::wstring& ini,ChatSettings settings);
}
