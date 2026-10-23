#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <d3d9.h>
#include <fstream>
namespace vccoop {
namespace {
LONG WINAPI Crash(EXCEPTION_POINTERS* exception) {
    HANDLE dump=CreateFileW(L"vc-coop-crash.dmp",GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(dump!=INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
        MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),dump,MiniDumpNormal,&info,nullptr,nullptr);CloseHandle(dump);
    }
    FILE* output=nullptr;fopen_s(&output,"vc-coop-crash.txt","w");
    if(output) {
        auto* record=exception->ExceptionRecord;
        fprintf(output,"exception=%08lx address=%p module=%p\n",record->ExceptionCode,record->ExceptionAddress,GetModuleHandleW(nullptr));
        SymInitialize(GetCurrentProcess(),nullptr,TRUE);
        CONTEXT context=*exception->ContextRecord;STACKFRAME64 frame{};
        frame.AddrPC.Offset=context.Eip;frame.AddrPC.Mode=AddrModeFlat;
        frame.AddrFrame.Offset=context.Ebp;frame.AddrFrame.Mode=AddrModeFlat;
        frame.AddrStack.Offset=context.Esp;frame.AddrStack.Mode=AddrModeFlat;
        for(unsigned i=0;i<24;++i) {
            char buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;
            DWORD64 displacement{};IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD lineOffset{};
            if(SymFromAddr(GetCurrentProcess(),frame.AddrPC.Offset,&displacement,symbol)) fprintf(output,"%u %s + %llu",i,symbol->Name,displacement);else fprintf(output,"%u %llx",i,frame.AddrPC.Offset);
            if(SymGetLineFromAddr64(GetCurrentProcess(),frame.AddrPC.Offset,&lineOffset,&line)) fprintf(output," %s:%lu",line.FileName,line.LineNumber);
            fprintf(output,"\n");
            if(!StackWalk64(IMAGE_FILE_MACHINE_I386,GetCurrentProcess(),GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)) break;
        }
        fflush(output);fclose(output);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void InstallDiagnostics() {SetUnhandledExceptionFilter(Crash);}
}

namespace vccoop {
// Development readback of this engine's render target, independent of desktop/window capture.
bool CaptureNativeFrame(void* nativeDevice,const char* path) {
    auto* device=static_cast<IDirect3DDevice9*>(nativeDevice);IDirect3DSurface9 *target=nullptr,*copy=nullptr;
    if(!device || FAILED(device->GetRenderTarget(0,&target))) return false;
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);
    bool ok=desc.MultiSampleType==D3DMULTISAMPLE_NONE && (desc.Format==D3DFMT_A8R8G8B8 || desc.Format==D3DFMT_X8R8G8B8)
        && SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr))
        && SUCCEEDED(device->GetRenderTargetData(target,copy));
    D3DLOCKED_RECT pixels{};
    if(ok && SUCCEEDED(copy->LockRect(&pixels,nullptr,D3DLOCK_READONLY))) {
        BITMAPFILEHEADER header{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=desc.Width;info.biHeight=-LONG(desc.Height);info.biPlanes=1;info.biBitCount=32;info.biSizeImage=desc.Width*desc.Height*4;
        header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info);header.bfSize=header.bfOffBits+info.biSizeImage;
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&header),sizeof(header));out.write(reinterpret_cast<char*>(&info),sizeof(info));
        for(unsigned y=0;y<desc.Height;++y) out.write(static_cast<char*>(pixels.pBits)+y*pixels.Pitch,desc.Width*4);
        ok=out.good();copy->UnlockRect();
    } else ok=false;
    if(copy) copy->Release();target->Release();return ok;
}
}
