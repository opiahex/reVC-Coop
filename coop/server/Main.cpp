#include "net/Network.h"
#include <atomic>
#include <charconv>
#include <iostream>
#include <windows.h>

namespace {
std::atomic<bool> running{true};
BOOL WINAPI StopHandler(DWORD) { running=false; return TRUE; }
}
int main(int argc,char** argv) {
    uint16_t port=7777; std::string bind="0.0.0.0";
    for (int i=1;i<argc;++i) {
        std::string arg=argv[i];
        if (arg=="--help") { std::cout<<"VC:COOP experimental server reVC 0.2.8\nUsage: vc-coop-server.exe [--port 7777] [--bind 0.0.0.0]\n"; return 0; }
        if ((arg=="--port" || arg=="--bind") && i+1<argc) {
            std::string value=argv[++i];
            if (arg=="--bind") bind=value;
            else {
                unsigned n{}; auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
                if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || n<1 || n>65535) { std::cerr<<"Invalid port\n"; return 2; }
                port=uint16_t(n);
            }
        } else { std::cerr<<"Unknown/incomplete option: "<<arg<<"\n"; return 2; }
    }
    vc::NetworkRuntime runtime;
    if (!runtime.Ok()) { std::cerr<<"ENet initialization failed\n"; return 1; }
    vc::Server server;
    server.log=[](const std::string& s){ std::cout<<s<<std::endl; };
    if (!server.Start(port,bind)) { std::cerr<<"Cannot bind UDP port "<<port<<"\n"; return 1; }
    SetConsoleCtrlHandler(StopHandler,TRUE);
    std::cout<<"VC:COOP reVC 0.2.8 | protocol "<<vc::kProtocol<<" | up to "<<unsigned(vc::kMaxPlayers)<<" players | Ctrl+C to stop"<<std::endl;
    while (running) server.Poll(10);
    std::cout<<"Server stopped"<<std::endl;
}
