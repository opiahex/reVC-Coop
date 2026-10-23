#pragma once
// Direct3D 9 adaptation of SA:COOP's GDI glyph-atlas renderer (MIT).
// Copyright (c) 2026 Karimov-Akbar. See third_party/sa-coop-ui.LICENSE.
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "ChatSettings.h"
struct IDirect3DStateBlock9;
struct IDirect3DDevice9;
struct IDirect3DTexture9;
namespace vc::ui {
enum FontId { FONT_NORMAL,FONT_SMALL,FONT_BIG,FONT_MENU,FONT_MENU_SMALL,FONT_CHAT,FONT_COUNT };
enum TextFlags : uint32_t { TEXT_SHADOW=1,TEXT_OUTLINE=2,TEXT_CENTER=4,TEXT_RIGHT=8 };
inline uint32_t WithAlpha(uint32_t color,float alpha) { return (color&0xFFFFFF)|(uint32_t((color>>24)*alpha)<<24); }
std::wstring Widen(std::string_view);
std::string Narrow(std::wstring_view);
class Renderer {
public:
    bool Begin(void* device);
    void End();
    void Rect(float x,float y,float w,float h,uint32_t color);
    void Image(void* texture,float x,float y,float w,float h,uint32_t color=0xFFFFFFFF);
    void RoundRect(float x,float y,float w,float h,uint32_t color,float radius=7);
    void Triangle(float x1,float y1,float x2,float y2,float x3,float y3,uint32_t color);
    void RectOutline(float x,float y,float w,float h,uint32_t color,float t=1);
    void Text(FontId,float x,float y,std::string_view text,uint32_t color,uint32_t flags=TEXT_OUTLINE);
    void TextW(FontId,float x,float y,std::wstring_view text,uint32_t color,uint32_t flags=TEXT_OUTLINE);
    float TextWidth(FontId,std::string_view);
    float TextWidthW(FontId,std::wstring_view);
    std::vector<std::wstring> Wrap(FontId,std::wstring_view,float width);
    float LineHeight(FontId f) const { return m_fonts[f].lineHeight; }
    float Width() const { return m_width; }
    float Height() const { return m_height; }
    float Scale() const { return m_height/720.f; }
    void Invalidate();
    void ConfigureChat(ChatSettings settings);
private:
    struct Glyph { float u0,v0,u1,v1,w,h,advance; };
    struct Font {
        IDirect3DTexture9* tex{}; std::unordered_map<wchar_t,Glyph> glyphs;
        float lineHeight{},pad{2},whiteU{},whiteV{}; int pixelHeight{}; float tracking{}; bool upper{};
    };
    struct Vertex { float x,y,z,rhw; uint32_t color; float u,v; };
    bool BuildFonts();
    bool BuildFont(Font&,const wchar_t*,int,int,float stretch=1,float tracking=0,bool upper=false);
    static wchar_t Shape(const Font&,wchar_t);
    void Flush();
    void SetTexture(IDirect3DTexture9*);
    void Quad(float,float,float,float,float,float,float,float,uint32_t);
    const Glyph* GetGlyph(Font&,wchar_t);
    void DrawRun(Font&,float,float,std::wstring_view,uint32_t);
    IDirect3DDevice9* m_dev{}; IDirect3DStateBlock9* m_stateBlock{};
    Font m_fonts[FONT_COUNT]; IDirect3DTexture9* m_curTex{}; std::vector<Vertex> m_verts;
    float m_width{1280},m_height{720}; int m_fontHeightFor{}; bool m_ready{},m_inFrame{};
    ChatSettings m_chatSettings;
};
Renderer& Gfx();
}
