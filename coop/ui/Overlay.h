#pragma once
#include "Renderer.h"
#include "PlayerIndicators.h"
#include "net/BackgroundClient.h"
#include <windows.h>
#include <deque>
#include <functional>
namespace vc::ui {
struct Action {
    enum Kind { Host,Join,Disconnect,Chat } kind;
    std::string name,address,text;
    uint16_t port{7777};
};
class Overlay {
public:
    std::function<void(const Action&)> onAction;
    std::function<void(bool)> onStoryVote;
    void Story(bool scene,bool voteOpen,std::string title,std::string hint);
    void Render(void* device,const BackgroundClient::Snapshot&,const std::string& name,const std::string& address,uint16_t port,bool ready,bool frontend=false,const std::vector<PlayerIndicator>& indicators={});
    bool Capturing() const { return !suspended_ && (menu_ || chat_); }
    void Add(const std::string&,uint32_t color=0xFFE4E8F0);
    void Configure(const std::wstring& ini) { ini_=ini; settings_=LoadChatSettings(ini); SaveChatSettings(ini_,settings_); }
    void Shutdown();
    void ClearMessages() { lines_.clear();bubbles_={};scroll_=0; }
    bool ProcessMessage(HWND window,UINT msg,WPARAM wp,LPARAM lp) { window_=window; return Message(msg,wp,lp); }
private:
    struct Line { std::wstring text; uint32_t color; };
    HWND window_{};
    bool menu_{true},chat_{},players_{},hidden_{},loaded_{},online_{},clicked_{},selectAll_{};
    bool skipCharacter_{},suspended_{},storyScene_{},storyVoteOpen_{};
    std::string storyTitle_,storyHint_;
    int tab_{},focus_{},scroll_{},historyIndex_{-1};
    size_t cursor_{}; float mouseX_{},mouseY_{};
    std::wstring nick_,address_,port_,input_;
    std::string error_;
    std::deque<Line> lines_;
    std::vector<std::wstring> history_;
    uint64_t lastChat_{},lastSubmit_{};
    Connection lastStatus_{Connection::Offline};
    ChatSettings settings_;
    struct Bubble { std::wstring text; uint64_t time{},generation{}; };
    std::array<Bubble,kMaxPlayers> bubbles_{};
    std::wstring ini_;
    int maxScroll_{};
    void Install(void* device);
    bool Message(UINT,WPARAM,LPARAM);
    std::wstring& Editing();
    void Focus(int);
    bool Hit(float,float,float,float) const;
    bool Button(const char*,float,float,float,float,bool enabled=true,bool active=false);
    void Field(const char*,int,float,float,float,std::wstring&);
    void Connect(bool host);
    void DrawChat();
    void DrawIndicators(const std::vector<PlayerIndicator>&);
    void DrawPlayers(const BackgroundClient::Snapshot&,float,float,float);
    bool Command(const std::string&);
    void SaveSettings();
};
Overlay& Hud();
}
