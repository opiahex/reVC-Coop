#include "ChatSettings.h"
#include <windows.h>
namespace vc::ui {
ChatSettings LoadChatSettings(const std::wstring& ini) {
    ChatSettings s;
    auto read=[&](const wchar_t* key,int fallback){return int(GetPrivateProfileIntW(L"chat",key,fallback,ini.c_str()));};
    s.fontSize=read(L"fontsize",s.fontSize); s.pageSize=read(L"pagesize",s.pageSize);
    s.Normalize(); return s;
}
void SaveChatSettings(const std::wstring& ini,ChatSettings s) {
    s.Normalize();
    auto write=[&](const wchar_t* key,int value){WritePrivateProfileStringW(L"chat",key,std::to_wstring(value).c_str(),ini.c_str());};
    write(L"fontsize",s.fontSize); write(L"pagesize",s.pageSize);
    // Migrate earlier prototypes: appearance is now fixed by the product.
    for(const auto* key:{L"width",L"outline",L"background",L"fontface",L"timestamps"})
        WritePrivateProfileStringW(L"chat",key,nullptr,ini.c_str());
}
}
