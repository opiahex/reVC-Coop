#include "Overlay.h"
#include <d3d9.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <windowsx.h>

namespace vc::ui {
namespace {
constexpr uint32_t pink=0xFFFF81B7,teal=0xFF71E3D3,muted=0xFFA5ADBE,white=0xFFF1F3F8,panel=0xF0131824;
uint32_t PlayerColor(unsigned id) { constexpr uint32_t colors[]={pink,teal,0xFFFFD28C}; return colors[id%3]; }
std::string ColorCode(uint32_t color) { char b[10]; std::snprintf(b,sizeof(b),"{%06X}",unsigned(color&0xFFFFFF)); return b; }
}
Overlay& Hud() { static Overlay ui; return ui; }
void Overlay::Add(const std::string& text,uint32_t color) {
    lines_.push_back({Widen(text),color}); if (lines_.size()>200) lines_.pop_front();
}
void Overlay::Install(void* device) {
    if (window_) return;
    D3DDEVICE_CREATION_PARAMETERS parameters{};
    if (FAILED(static_cast<IDirect3DDevice9*>(device)->GetCreationParameters(&parameters)) || !parameters.hFocusWindow) return;
    window_=parameters.hFocusWindow;
// Native input is dispatched explicitly by reVC's MainWndProc.
}
void Overlay::Shutdown() {
    window_=nullptr; Gfx().Invalidate();
}
std::wstring& Overlay::Editing() { return chat_ ? input_ : focus_==0 ? nick_ : focus_==1 ? address_ : port_; }
void Overlay::Focus(int field) { focus_=field; cursor_=Editing().size(); selectAll_=true; }
void Overlay::Story(bool scene,bool voteOpen,std::string title,std::string hint) {
    storyScene_=scene; storyVoteOpen_=voteOpen; storyTitle_=std::move(title); storyHint_=std::move(hint);
    if(scene) chat_=players_=false;
}
bool Overlay::Message(UINT msg,WPARAM wp,LPARAM lp) {
    if(suspended_) return false;
    if (msg==WM_MOUSEMOVE) { mouseX_=float(GET_X_LPARAM(lp)); mouseY_=float(GET_Y_LPARAM(lp)); }
    if (msg==WM_LBUTTONDOWN && menu_) { clicked_=true; return true; }
    if (msg==WM_MOUSEWHEEL && chat_) { scroll_=std::clamp(scroll_+GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*3,0,maxScroll_); return true; }
    if (msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN) {
        bool repeat=(lp&(1u<<30))!=0;
        if ((GetKeyState(VK_MENU)&0x8000) && (wp==VK_F4 || wp==VK_TAB || wp==VK_RETURN)) return false;
        if(storyScene_ && !menu_ && !repeat) {
            if(wp==VK_RETURN || wp==VK_SPACE) { if(onStoryVote) onStoryVote(true); return true; }
            if(wp==VK_ESCAPE && storyVoteOpen_) { if(onStoryVote) onStoryVote(false); return true; }
        }
        if (wp==VK_F8 && !repeat) { menu_=!menu_; chat_=false; return true; }
        if (wp==VK_F7 && !repeat && !Capturing()) { hidden_=!hidden_; return true; }
        if (!Capturing() && wp==VK_TAB) { players_=true; return true; }
        if (!Capturing() && online_ && (wp=='T' || wp==VK_RETURN) && !repeat) {
            chat_=true; input_.clear(); cursor_=0; selectAll_=false; historyIndex_=-1; scroll_=0; skipCharacter_=wp=='T'; return true;
        }
        if (Capturing()) {
            if (wp==VK_ESCAPE) { menu_=chat_=false; return true; }
            if (wp==VK_TAB && menu_) { Focus((focus_+1)%3); return true; }
            auto& text=Editing(); cursor_=std::min(cursor_,text.size());
            bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            if (ctrl && wp=='A') { selectAll_=true; return true; }
            if (ctrl && wp=='V') {
                if (OpenClipboard(window_)) {
                    auto h=GetClipboardData(CF_UNICODETEXT);
                    if (h) { auto* p=static_cast<const wchar_t*>(GlobalLock(h)); if (p) {
                        std::wstring paste(p); GlobalUnlock(h);
                        paste.erase(std::remove_if(paste.begin(),paste.end(),[](wchar_t c){return c<32 || c==127;}),paste.end());
                        if (selectAll_) { text.clear(); cursor_=0; selectAll_=false; }
                        size_t limit=chat_ ? 160 : focus_==0 ? 24 : focus_==1 ? 128 : 5;
                        paste.resize(std::min(paste.size(),limit-text.size())); text.insert(cursor_,paste); cursor_+=paste.size();
                    }} CloseClipboard();
                } return true;
            }
            if (wp==VK_RETURN) {
                if (chat_) {
                    auto value=Narrow(input_); chat_=false;
                    if (!value.empty() && value.find_first_not_of(' ')!=std::string::npos) {
                        if (value[0]=='/') { Command(value); }
                        else if (!ValidText(value)) { Add("Сообщение слишком длинное (максимум 192 байта)",pink); chat_=true; }
                        else if (NowMs()-lastSubmit_<750) { Add("Подожди немного перед следующим сообщением",muted); chat_=true; }
                        else { lastSubmit_=NowMs(); history_.push_back(input_); if(history_.size()>50) history_.erase(history_.begin()); if(onAction) onAction({Action::Chat,{},{},value}); }
                    }
                } else Connect(tab_==1);
                return true;
            }
            if (chat_ && (wp==VK_UP || wp==VK_DOWN)) {
                if (!history_.empty()) {
                    if(historyIndex_<0) historyIndex_=int(history_.size());
                    historyIndex_=std::clamp(historyIndex_+(wp==VK_UP ? -1 : 1),0,int(history_.size()));
                    input_=historyIndex_==int(history_.size()) ? L"" : history_[historyIndex_]; cursor_=input_.size();
                } return true;
            }
            if (chat_ && (wp==VK_PRIOR || wp==VK_NEXT)) { int step=std::max(1,settings_.pageSize-1); scroll_=std::clamp(scroll_+(wp==VK_PRIOR ? step : -step),0,maxScroll_); return true; }
            if (wp==VK_LEFT) { if(cursor_) --cursor_; selectAll_=false; }
            if (wp==VK_RIGHT) { cursor_=std::min(cursor_+1,text.size()); selectAll_=false; }
            if (wp==VK_HOME) { cursor_=0; selectAll_=false; }
            if (wp==VK_END) { cursor_=text.size(); selectAll_=false; }
            if (wp==VK_BACK || wp==VK_DELETE) {
                if (selectAll_) { text.clear(); cursor_=0; selectAll_=false; }
                else if (wp==VK_BACK && cursor_) { text.erase(--cursor_,1); }
                else if (wp==VK_DELETE && cursor_<text.size()) text.erase(cursor_,1);
            }
            return true;
        }
    }
    if ((msg==WM_KEYUP || msg==WM_SYSKEYUP) && wp==VK_TAB) players_=false;
    if (msg==WM_KILLFOCUS) { players_=false; clicked_=false; }
    if (msg==WM_CHAR && Capturing()) {
        if(skipCharacter_) { skipCharacter_=false; return true; }
        wchar_t c=wchar_t(wp);
        if(c>=32 && c!=127) {
            auto& text=Editing();
            if (selectAll_) { text.clear(); cursor_=0; selectAll_=false; }
            size_t limit=chat_ ? 160 : focus_==0 ? 24 : focus_==1 ? 128 : 5;
            if (text.size()<limit) { cursor_=std::min(cursor_,text.size()); text.insert(cursor_++,1,c); }
        } return true;
    }
    return Capturing() && (msg==WM_MOUSEMOVE || (msg>=WM_LBUTTONDOWN && msg<=WM_MBUTTONDBLCLK));
}
bool Overlay::Hit(float x,float y,float w,float h) const { return mouseX_>=x && mouseY_>=y && mouseX_<x+w && mouseY_<y+h; }
bool Overlay::Button(const char* text,float x,float y,float w,float h,bool enabled,bool active) {
    auto& g=Gfx(); bool hover=enabled && Hit(x,y,w,h);
    g.RoundRect(x,y,w,h,active || hover ? pink : 0xFF343D50,7*g.Scale());
    g.RoundRect(x+1,y+1,w-2,h-2,active ? 0xFF593148 : hover ? 0xFF343B50 : 0xFF202637,6*g.Scale());
    g.Text(FONT_NORMAL,x+w/2,y+(h-g.LineHeight(FONT_NORMAL))/2,text,enabled ? white : 0xFF687386,TEXT_CENTER);
    return enabled && hover && clicked_;
}
void Overlay::Field(const char* title,int id,float x,float y,float w,std::wstring& text) {
    auto& g=Gfx(); float s=g.Scale(),h=42*s;
    g.Text(FONT_SMALL,x,y,title,muted,0); y+=24*s;
    if (clicked_ && Hit(x,y,w,h)) Focus(id);
    g.RoundRect(x,y,w,h,focus_==id ? pink : 0xFF384052,7*s); g.RoundRect(x+1,y+1,w-2,h-2,0xFF0B101A,6*s);
    std::wstring shown=text; while(g.TextWidthW(FONT_NORMAL,shown)>w-28*s && !shown.empty()) shown.erase(shown.begin());
    g.TextW(FONT_NORMAL,x+12*s,y+10*s,shown,focus_==id && selectAll_ ? teal : white,0);
    if (focus_==id && (NowMs()/500)%2==0) {
        size_t offset=text.size()-shown.size(); size_t count=cursor_>offset ? cursor_-offset : 0;
        float caret=g.TextWidthW(FONT_NORMAL,shown.substr(0,count));
        g.Rect(x+12*s+caret,y+9*s,1,24*s,pink);
    }
}
void Overlay::Connect(bool host) {
    auto name=Narrow(nick_),address=Narrow(address_),portText=Narrow(port_); unsigned p=0;
    auto parsed=std::from_chars(portText.data(),portText.data()+portText.size(),p);
    if (!ValidName(name)) { error_="Ник: 1–24 символа латиницей, без переноса строк"; return; }
    if (address.empty() || address.size()>128 || address.find_first_of(" \r\n\t")!=std::string::npos) { error_="Укажи IP-адрес или имя сервера"; return; }
    if(parsed.ec!=std::errc{} || parsed.ptr!=portText.data()+portText.size() || !p || p>65535) { error_="Порт должен быть от 1 до 65535"; return; }
    error_.clear(); if(onAction) onAction({host ? Action::Host : Action::Join,name,address,{},uint16_t(p)});
}
void Overlay::SaveSettings() {
    settings_.Normalize(); if(!ini_.empty()) SaveChatSettings(ini_,settings_);
}
bool Overlay::Command(const std::string& input) {
    auto split=input.find(' '); auto command=input.substr(0,split),value=split==std::string::npos ? std::string{} : input.substr(split+1);
    if(command!="/fontsize" && command!="/pagesize") {
        Add("Доступны только /fontsize и /pagesize",muted); return true;
    }
    int n=0; auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) {
        Add(command=="/fontsize" ? "Размер: /fontsize -3..5 или 10..32 px" : "Строки: /pagesize 5..20",pink); return true;
    }
    if(command=="/fontsize") {
        if(n>=-3 && n<=5) n=13+n;
        if(n<10 || n>32) { Add("Размер: /fontsize -3..5 или 10..32 px",pink); return true; }
        settings_.fontSize=n;
    } else {
        if(n<5 || n>20) { Add("Количество строк: /pagesize 5..20",pink); return true; }
        settings_.pageSize=n;
    }
    SaveSettings(); Add(command=="/fontsize" ? "Размер шрифта сохранён" : "Количество строк сохранено",teal); return true;
}
void Overlay::DrawPlayers(const BackgroundClient::Snapshot& net,float x,float y,float w) {
    auto& g=Gfx(); float s=g.Scale();
    for(uint8_t i=0;i<kMaxPlayers;++i) if(net.players[i].active) {
        g.Rect(x,y,w,40*s,0xFF101623);
        g.Rect(x,y,3*s,40*s,PlayerColor(i));
        g.Text(FONT_NORMAL,x+14*s,y+9*s,net.players[i].name+(i==net.self ? " (ты)" : ""),PlayerColor(i));
        g.Text(FONT_SMALL,x+w-14*s,y+12*s,i==net.host ? "ВЕДУЩИЙ" : "НАПАРНИК",muted,TEXT_RIGHT);
        y+=46*s;
    }
}
void Overlay::DrawChat() {
    if ((hidden_ && !chat_) || menu_) return;
    auto& g=Gfx(); float s=g.Scale(),x=22*s,y=24*s,width=std::min(float(settings_.width),g.Width()-x-24*s),lh=g.LineHeight(FONT_CHAT)+2;
    int page=std::min(settings_.pageSize,std::max(3,int((g.Height()-y-100*s)/lh)));
    int total=0; for(const auto& line:lines_) total+=int(g.Wrap(FONT_CHAT,line.text,width).size());
    maxScroll_=std::max(0,total-page); scroll_=std::clamp(scroll_,0,maxScroll_);
    struct Visible { std::wstring text; uint32_t color; };
    std::vector<Visible> visible; int skip=scroll_;
    for(auto it=lines_.rbegin();it!=lines_.rend() && int(visible.size())<page;++it) {
        float alpha=1.f; // SA-MP-style persistent chat; F7 hides it explicitly.
        auto wrapped=g.Wrap(FONT_CHAT,it->text,width);
        for(auto part=wrapped.rbegin();part!=wrapped.rend() && int(visible.size())<page;++part) {
            if(skip>0) { --skip; continue; } visible.push_back({*part,WithAlpha(it->color,alpha)});
        }
    }
    for(auto it=visible.rbegin();it!=visible.rend();++it) { g.TextW(FONT_CHAT,x,y,it->text,it->color,settings_.outline ? TEXT_OUTLINE : 0); y+=lh; }
    if(chat_) {
        y=24*s+page*lh+8*s;
        float box=std::max(40*s,lh+16);
        g.RoundRect(x-8*s,y-7*s,width+16*s,box,0xE00B101A,6*s); g.Rect(x-8*s,y,3*s,box-14*s,pink);
        auto shown=input_; while(g.TextWidthW(FONT_CHAT,shown)>width-35*s && !shown.empty()) shown.erase(shown.begin());
        g.Text(FONT_CHAT,x,y,">",pink); g.TextW(FONT_CHAT,x+22*s,y,shown,white);
        if((NowMs()/500)%2==0) {
            size_t offset=input_.size()-shown.size(),count=cursor_>offset ? cursor_-offset : 0;
            g.Rect(x+22*s+g.TextWidthW(FONT_CHAT,shown.substr(0,count)),y,1,lh-2,white);
        }
        g.Text(FONT_SMALL,x,y+box+6*s,"Enter — отправить    Esc — закрыть    PgUp/PgDn — история",muted);
    }
}
void Overlay::DrawIndicators(const std::vector<PlayerIndicator>& indicators) {
    auto& g=Gfx();
    for(const auto& p:indicators) {
        const float fade=std::clamp((85-p.distance)/20,0.f,1.f),s=std::clamp(g.Scale(),.8f,1.5f);
        const float width=76*s,x=p.x-width/2,y=p.y;
        g.Text(FONT_CHAT,p.x,y-23*s,p.name+" ("+std::to_string(p.id)+")",WithAlpha(PlayerColor(p.id),fade),TEXT_CENTER|TEXT_OUTLINE);
        if(p.wanted && p.wantedIcon) {
            const unsigned stars=std::min<unsigned>(p.wanted,6);float left=p.x-float(stars)*13*s/2;
            for(unsigned i=0;i<stars;++i) {
                auto draw=[&](float dx,float dy,uint32_t color){g.ImageRegion(p.wantedIcon,left+i*13*s+dx,y-40*s+dy,15*s,16*s,14.f/16,1.f/12.8f+.0021f,15.f/16-.001f,2.f/12.8f-.0021f,WithAlpha(color,fade));};
                for(int ox=-1;ox<=1;++ox) for(int oy=-1;oy<=1;++oy) if(ox || oy) draw(float(ox),float(oy),0xFF000000);
                draw(0,0,0xFF61C2F7);
            }
        }
        auto bar=[&](float top,float value,uint32_t color) {
            g.Rect(x-1,top-1,width+2,7*s+2,WithAlpha(0xFF000000,fade));
            g.Rect(x,top,width,7*s,WithAlpha(0xFF471E28,fade));
            g.Rect(x,top,width*std::clamp(value/100.f,0.f,1.f),7*s,WithAlpha(color,fade));
        };
        bar(y,p.health,0xFFFF769D);
        if(p.armour>0) bar(y+11*s,p.armour,0xFFD7ECF2);
        if(p.weaponIcon) {
            const float ix=p.x+44*s,iy=y-9*s,size=34*s;
            for(int ox=-1;ox<=1;++ox) for(int oy=-1;oy<=1;++oy)
                if(ox || oy) g.Image(p.weaponIcon,ix+ox,iy+oy,size,size,WithAlpha(0xFF000000,fade));
            g.Image(p.weaponIcon,ix,iy,size,size,WithAlpha(0xFFFFFFFF,fade));
        }
        const auto& bubble=bubbles_[p.id]; const auto age=NowMs()-bubble.time;
        if(bubble.text.empty() || bubble.generation!=p.generation || age>=8000 || p.distance>45) continue;
        const float alpha=fade*std::min(1.f,float(8000-age)/1000);
        auto lines=g.Wrap(FONT_CHAT,bubble.text,260*s);
        if(lines.size()>4) lines.resize(4);
        float top=y-(p.wanted?53:35)*s-float(lines.size())*(g.LineHeight(FONT_CHAT)+2);
        for(const auto& line:lines) {
            g.TextW(FONT_CHAT,p.x,top,line,WithAlpha(0xFFFFFFFF,alpha),TEXT_CENTER|TEXT_OUTLINE);
            top+=g.LineHeight(FONT_CHAT)+2;
        }
    }
}
void Overlay::Render(void* device,const BackgroundClient::Snapshot& net,const std::string& name,const std::string& address,uint16_t port,bool ready,bool frontend,const std::vector<PlayerIndicator>& indicators) {
    suspended_=frontend && ready;
    if(suspended_) { chat_=players_=false; }
    Install(device);
    if(!loaded_) { nick_=Widen(name); address_=Widen(address); port_=std::to_wstring(port); loaded_=true; Focus(0); Add("{FF81B7}VC:COOP {FFFFFF}• Добро пожаловать в Vice City"); }
    online_=net.status==Connection::Online;
    if(net.status!=lastStatus_) {
        lastStatus_=net.status;
        if(online_) { menu_=false; chat_=false; Add("Соединение установлено.",teal); }
        if(net.status==Connection::Offline) { lastChat_=0; bubbles_={}; if(!net.error.empty()) { Add("Отключено: "+net.error,pink); error_=net.error; } }
        if(net.status==Connection::Connecting) lastChat_=0;
    }
    for(const auto& line:net.chats) if(line.serial>lastChat_) {
        Add(ColorCode(PlayerColor(line.sender))+line.name+"{FFFFFF}: "+line.text); lastChat_=line.serial;
        if(line.sender<kMaxPlayers) bubbles_[line.sender]={PlainBubble(line.text),line.time,net.players[line.sender].generation};
    }
    if(frontend && ready) { clicked_=false; return; }
    auto& g=Gfx(); g.ConfigureChat(settings_); if(!g.Begin(device)) return;
    float s=g.Scale(); RECT client{}; GetClientRect(window_,&client);
    POINT mouse{}; GetCursorPos(&mouse); ScreenToClient(window_,&mouse);
    if(client.right>0 && client.bottom>0) { mouseX_=float(mouse.x)*g.Width()/client.right; mouseY_=float(mouse.y)*g.Height()/client.bottom; }
    if(online_ && ready) { if(!menu_ && !players_) DrawIndicators(indicators); DrawChat(); }
    if(menu_) {
        float w=820*s,h=620*s,x=(g.Width()-w)/2,y=(g.Height()-h)/2,cx=x+34*s,cw=w-68*s;
        g.Rect(0,0,g.Width(),g.Height(),0xA0060911); g.RoundRect(x+7*s,y+9*s,w,h,0x90000000,14*s); g.RoundRect(x,y,w,h,panel,14*s);
        g.Rect(cx,y,110*s,3*s,pink); g.Rect(cx+110*s,y,110*s,3*s,teal); g.Text(FONT_BIG,cx,y+26*s,"VICE CITY",white,0);
        g.Text(FONT_MENU_SMALL,cx,y+65*s,"COOPERATIVE • reVC 0.2.7",pink,0);
        g.RoundRect(x+w-248*s,y+29*s,155*s,29*s,online_ ? 0xFF153C36 : 0xFF292D3D,12*s);
        g.Text(FONT_SMALL,x+w-170*s,y+35*s,online_ ? "В СЕТИ" : "НЕ ПОДКЛЮЧЁН",online_ ? teal : muted,TEXT_CENTER);
        if(Button("×",x+w-65*s,y+24*s,34*s,34*s)) menu_=false;
        float by=y+105*s;
        float tw=(cw-8*s)/2;
        if(Button("Подключиться",cx,by,tw,43*s,true,tab_==0)) tab_=0;
        if(Button("Создать сервер",cx+tw+8*s,by,tw,43*s,true,tab_==1)) tab_=1;
        if(online_) {
            g.Text(FONT_MENU,cx,by+70*s,"ВЫ ПОДКЛЮЧЕНЫ",teal,0);
            DrawPlayers(net,cx,by+115*s,cw);
            if(Button("Отключиться",cx,y+h-118*s,cw,44*s)) { if(onAction) onAction({Action::Disconnect,{},{},{}}); }
        } else {
            Field("Твой ник",0,cx,by+65*s,cw,nick_);
            Field(tab_==1 ? "Адрес для подключения на этом ПК" : "Адрес сервера",1,cx,by+156*s,cw-140*s,address_);
            Field("UDP-порт",2,cx+cw-120*s,by+156*s,120*s,port_);
            bool busy=net.status!=Connection::Offline;
            if(Button(busy ? "Подключение…" : tab_==1 ? "Запустить сервер" : "Подключиться",cx,by+264*s,cw,48*s,!busy)) Connect(tab_==1);
            if(busy && Button("Отмена",cx,by+321*s,cw,34*s)) { if(onAction) onAction({Action::Disconnect,{},{},{}}); }
            if(!error_.empty()) g.Text(FONT_SMALL,cx,by+363*s,error_,pink,0);
            else if(!ready) g.Text(FONT_SMALL,cx,by+363*s,"Мир загрузится автоматически, когда хост будет готов",muted,0);
            else g.Text(FONT_SMALL,cx,by+363*s,tab_==1 ? "Друг подключается по твоему IP и этому порту" : "Сохранённый адрес останется после перезапуска",muted,0);
        }
        if(!online_) {
            g.Rect(cx,y+h-57*s,cw,1,0xFF364052);
            g.Text(FONT_SMALL,cx,y+h-38*s,"F8 — меню    T / Enter — чат    Tab — игроки    F7 — скрыть чат",muted,0);
        }
        g.Triangle(mouseX_,mouseY_,mouseX_+3*s,mouseY_+21*s,mouseX_+15*s,mouseY_+14*s,0xFF05070C);
        g.Triangle(mouseX_+2*s,mouseY_+3*s,mouseX_+4*s,mouseY_+17*s,mouseX_+12*s,mouseY_+13*s,white);
        g.Triangle(mouseX_+7*s,mouseY_+14*s,mouseX_+11*s,mouseY_+23*s,mouseX_+14*s,mouseY_+21*s,pink);
    } else if(players_) {
        float w=520*s,x=(g.Width()-w)/2,y=160*s;
        g.Rect(x-22*s,y-46*s,w+44*s,225*s,panel); g.Text(FONT_MENU,x,y-30*s,"ИГРОКИ • VC:COOP",pink,0); DrawPlayers(net,x,y+16*s,w);
    }
    if(!frontend && online_ && !menu_ && !storyTitle_.empty()) {
        const float height=g.LineHeight(FONT_CHAT), top=g.Height()-110*s;
        const auto width=std::min(g.Width()-60*s,680*s);
        g.RoundRect((g.Width()-width)/2,top-12*s,width,height*(storyHint_.empty() ? 1:2)+25*s,0xC0131824,7*s);
        g.Text(FONT_CHAT,g.Width()/2,top,storyTitle_,pink,TEXT_CENTER|TEXT_OUTLINE);
        if(!storyHint_.empty()) g.Text(FONT_CHAT,g.Width()/2,top+height+4*s,storyHint_,white,TEXT_CENTER|TEXT_OUTLINE);
    }
    if(!frontend && online_ && ready && !menu_) g.Text(FONT_CHAT,g.Width()-22*s,g.Height()-g.LineHeight(FONT_CHAT)-18*s,"VC:COOP reVC 0.2.7",white,TEXT_RIGHT|TEXT_OUTLINE);
    g.End(); clicked_=false;
}
}

