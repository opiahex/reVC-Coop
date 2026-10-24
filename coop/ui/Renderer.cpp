// Adapted from SA:COOP (MIT), Copyright (c) 2026 Karimov-Akbar.
#include "Renderer.h"

#include <d3d9.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)


namespace vc::ui {

Renderer& Gfx() {
    static Renderer r;
    return r;
}

std::wstring Widen(std::string_view s) { int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0); std::wstring out(n,0); if(n) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n); return out; }
std::string Narrow(std::wstring_view s) { int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr); std::string out(n,0); if(n) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr); return out; }

static constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// Characters baked into the atlases.
static void CollectCharset(std::vector<wchar_t>& out) {
    for (wchar_t c = 0x20; c <= 0x7E; ++c) out.push_back(c);
    for (wchar_t c = 0xA0; c <= 0xFF; ++c) out.push_back(c);
    for (wchar_t c = 0x400; c <= 0x45F; ++c) out.push_back(c);  // Cyrillic
    for (wchar_t c = 0x490; c <= 0x491; ++c) out.push_back(c);  // Ґ ґ
    const wchar_t extra[] = {0x2010, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026,
                             0x2116, 0x20AC, 0x2190, 0x2191, 0x2192, 0x2193, 0x25B2, 0x25BC, 0x25CF, 0x2713};
    for (wchar_t c : extra) out.push_back(c);
}

bool Renderer::BuildFont(Font& f, const wchar_t* face, int pixelHeight, int weight, float stretch, float tracking,
                         bool upper) {
    std::vector<wchar_t> chars;
    CollectCharset(chars);

    HDC dc = CreateCompatibleDC(nullptr);
    auto create = [&](int width) {
        return ::CreateFontW(-pixelHeight, width, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    HFONT font = create(0);
    if (stretch != 1.0f) {  // wider glyphs: an explicit average width makes GDI scale them horizontally
        HGDIOBJ prev = SelectObject(dc, font);
        TEXTMETRICW tm0;
        GetTextMetricsW(dc, &tm0);
        SelectObject(dc, prev);
        DeleteObject(font);
        font = create((int)std::lround(tm0.tmAveCharWidth * stretch));
    }
    f.tracking = tracking;
    f.upper = upper;
    HGDIOBJ oldFont = SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int lineH = tm.tmHeight;
    int pad = std::max(2, pixelHeight / 10);
    f.pad = (float)pad;

    // pack glyphs in rows
    const int texW = 1024;
    int x = 0, y = 0, rowH = lineH + pad * 2;
    struct Pos {
        wchar_t ch;
        int x, y, cellW, adv;
    };
    std::vector<Pos> pos;
    // white block for rectangles
    pos.push_back({0, 0, 0, 4, 0});
    x = 4 + 2;
    for (wchar_t ch : chars) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, &ch, 1, &sz);
        int cellW = sz.cx + pad * 2;
        if (x + cellW > texW) {
            x = 0;
            y += rowH;
        }
        pos.push_back({ch, x, y, cellW, (int)sz.cx});
        x += cellW + 1;
    }
    int texH = 64;
    while (texH < y + rowH) texH <<= 1;
    if (texH > 2048) {
        LOGE("Font atlas too large");
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        return false;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = texW;
    bi.bmiHeader.biHeight = -texH;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)texW * texH * 4);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    for (auto& p : pos) {
        if (p.ch == 0) continue;
        TextOutW(dc, p.x + pad, p.y + pad, &p.ch, 1);
    }
    GdiFlush();

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(m_dev->CreateTexture(texW, texH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) {
        LOGE("CreateTexture for font failed");
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        return false;
    }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        const uint32_t* src = (const uint32_t*)bits;
        for (int row = 0; row < texH; ++row) {
            uint32_t* dst = (uint32_t*)((uint8_t*)lr.pBits + row * lr.Pitch);
            for (int col = 0; col < texW; ++col) {
                uint32_t px = src[row * texW + col];
                uint32_t r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
                uint32_t a = std::max(r, std::max(g, b));
                dst[col] = (a << 24) | 0x00FFFFFF;
            }
        }
        // opaque white block for rectangles
        for (int row = 0; row < 4; ++row) {
            uint32_t* dst = (uint32_t*)((uint8_t*)lr.pBits + row * lr.Pitch);
            for (int col = 0; col < 4; ++col) dst[col] = 0xFFFFFFFF;
        }
        tex->UnlockRect(0);
    }

    f.tex = tex;
    f.lineHeight = (float)lineH;
    f.pixelHeight = pixelHeight;
    f.whiteU = 2.0f / texW;
    f.whiteV = 2.0f / texH;
    f.glyphs.clear();
    for (auto& p : pos) {
        if (p.ch == 0) continue;
        Glyph g;
        g.u0 = (float)p.x / texW;
        g.v0 = (float)p.y / texH;
        g.u1 = (float)(p.x + p.cellW) / texW;
        g.v1 = (float)(p.y + rowH) / texH;
        g.w = (float)p.cellW;
        g.h = (float)rowH;
        g.advance = (float)p.adv;
        f.glyphs[p.ch] = g;
    }

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteDC(dc);
    return true;
}

bool Renderer::BuildFonts() {
    Invalidate();
    int h = (int)m_height;
    int normal = std::max(12, (int)std::lround(h / 36.0));
    int smallFont = std::max(11, (int)std::lround(h / 45.0));
    int big = std::max(16, (int)std::lround(h / 30.0));
    int menu = std::max(12, (int)std::lround(h / 45.0));
    int menuSmall = std::max(10, (int)std::lround(h / 66.0));
    // the menu font of the game is wide and spaced out: Bahnschrift stretched by a third, with tracking
    bool ok = BuildFont(m_fonts[FONT_NORMAL], L"Arial", normal, FW_BOLD) &&
              BuildFont(m_fonts[FONT_SMALL], L"Arial", smallFont, FW_BOLD) &&
              BuildFont(m_fonts[FONT_BIG], L"Bahnschrift", big, FW_BOLD, 1.35f, big * 0.12f, true) &&
              BuildFont(m_fonts[FONT_MENU], L"Bahnschrift", menu, FW_BOLD, 1.3f, menu * 0.12f, true) &&
              BuildFont(m_fonts[FONT_MENU_SMALL], L"Bahnschrift", menuSmall, FW_SEMIBOLD, 1.3f, menuSmall * 0.16f, true) &&
              BuildFont(m_fonts[FONT_CHAT], m_chatSettings.Face(), m_chatSettings.fontSize, FW_BOLD);
    m_fontHeightFor = h;
    if (ok) LOGI("UI fonts created for %dx%d (%d/%d/%d/%d/%d px)", (int)m_width, h, normal, smallFont, big, menu, menuSmall);
    return ok;
}

void Renderer::Invalidate() {
    for (auto& f : m_fonts) {
        if (f.tex) f.tex->Release();
        f.tex = nullptr;
        f.glyphs.clear();
    }
    m_ready = false;
}
void Renderer::ConfigureChat(ChatSettings settings) {
    settings.Normalize();
    if(settings.fontSize!=m_chatSettings.fontSize) m_ready=false;
    m_chatSettings=settings;
}

bool Renderer::Begin(void* device) {
    m_dev = static_cast<IDirect3DDevice9*>(device);
    if (!m_dev) return false;

    D3DVIEWPORT9 vp;
    if (SUCCEEDED(m_dev->GetViewport(&vp)) && vp.Width > 0) {
        // The game may render into a viewport smaller than the back buffer (widescreen borders);
        // use the back buffer size instead.
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(m_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            D3DSURFACE_DESC d;
            bb->GetDesc(&d);
            bb->Release();
            m_width = (float)d.Width;
            m_height = (float)d.Height;
        } else {
            m_width = (float)vp.Width;
            m_height = (float)vp.Height;
        }
    }
    if (!m_ready || m_fontHeightFor != (int)m_height) {
        m_ready = BuildFonts();
        if (!m_ready) return false;
    }

    IDirect3DStateBlock9* sb = nullptr;
    if (FAILED(m_dev->CreateStateBlock(D3DSBT_ALL, &sb))) return false;
    m_stateBlock = sb;

    D3DVIEWPORT9 full{0, 0, (DWORD)m_width, (DWORD)m_height, 0.0f, 1.0f};
    m_dev->SetViewport(&full);
    m_dev->SetVertexShader(nullptr);
    m_dev->SetFVF(kFvf);
    m_dev->SetPixelShader(0);
    
    m_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    m_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    m_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    
    m_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    
    m_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    m_dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    m_dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    m_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    m_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    m_dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    m_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    m_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    m_curTex = nullptr;
    m_dev->SetTexture(0, m_fonts[FONT_NORMAL].tex);
    m_curTex = m_fonts[FONT_NORMAL].tex;
    m_verts.clear();
    m_inFrame = true;
    return true;
}

void Renderer::End() {
    if (!m_inFrame) return;
    Flush();
    auto* sb = m_stateBlock;
    if (sb) {
        sb->Apply();
        sb->Release();
    }
    m_stateBlock = 0;
    m_inFrame = false;
}

void Renderer::Flush() {
    if (m_verts.empty() || !m_dev) return;
    m_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(m_verts.size() / 3), m_verts.data(), sizeof(Vertex));
    m_verts.clear();
}

void Renderer::SetTexture(IDirect3DTexture9* t) {
    if (t == m_curTex) return;
    Flush();
    m_dev->SetTexture(0, t);
    m_curTex = t;
}

void Renderer::Quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
    x -= 0.5f;
    y -= 0.5f;
    Vertex a{x, y, 0, 1, c, u0, v0}, b{x + w, y, 0, 1, c, u1, v0}, d{x, y + h, 0, 1, c, u0, v1},
        e{x + w, y + h, 0, 1, c, u1, v1};
    m_verts.push_back(a);
    m_verts.push_back(b);
    m_verts.push_back(d);
    m_verts.push_back(b);
    m_verts.push_back(e);
    m_verts.push_back(d);
}

void Renderer::Image(void* texture,float x,float y,float w,float h,uint32_t color) {
    if(!m_inFrame || !texture || !(color>>24)) return;
    SetTexture(static_cast<IDirect3DTexture9*>(texture));
    Quad(x,y,w,h,0,0,1,1,color);
}
void Renderer::ImageRegion(void* texture,float x,float y,float w,float h,float u0,float v0,float u1,float v1,uint32_t color) {
    if(!m_inFrame || !texture || !(color>>24)) return;
    SetTexture(static_cast<IDirect3DTexture9*>(texture));Quad(x,y,w,h,u0,v0,u1,v1,color);
}
void Renderer::Rect(float x, float y, float w, float h, uint32_t argb) {
    if (!m_inFrame || (argb >> 24) == 0) return;
    Font& f = m_fonts[FONT_NORMAL];
    SetTexture(f.tex);
    Quad(x, y, w, h, f.whiteU, f.whiteV, f.whiteU, f.whiteV, argb);
}
void Renderer::Triangle(float x1,float y1,float x2,float y2,float x3,float y3,uint32_t color) {
    if (!m_inFrame || (color>>24)==0) return;
    const auto& f=m_fonts[FONT_NORMAL]; SetTexture(f.tex);
    for (auto point:{std::pair{x1,y1},std::pair{x2,y2},std::pair{x3,y3}})
        m_verts.push_back({point.first-.5f,point.second-.5f,0,1,color,f.whiteU,f.whiteV});
}

void Renderer::RectOutline(float x, float y, float w, float h, uint32_t argb, float t) {
    Rect(x, y, w, t, argb);
    Rect(x, y + h - t, w, t, argb);
    Rect(x, y + t, t, h - 2 * t, argb);
    Rect(x + w - t, y + t, t, h - 2 * t, argb);
}
void Renderer::RoundRect(float x,float y,float w,float h,uint32_t color,float radius) {
    radius=std::min(radius,std::min(w,h)*0.5f);
    if(radius<=0) { Rect(x,y,w,h,color); return; }
    Rect(x+radius,y,w-2*radius,h,color); Rect(x,y+radius,radius,h-2*radius,color); Rect(x+w-radius,y+radius,radius,h-2*radius,color);
    const auto& f=m_fonts[FONT_NORMAL]; SetTexture(f.tex);
    for(int corner=0;corner<4;++corner) {
        float cx=x+(corner==0 || corner==3 ? radius : w-radius),cy=y+(corner<2 ? radius : h-radius);
        float start=3.14159265f+corner*1.57079633f;
        for(int i=0;i<12;++i) {
            auto vertex=[&](float angle){return Vertex{cx+radius*std::cos(angle)-0.5f,cy+radius*std::sin(angle)-0.5f,0,1,color,f.whiteU,f.whiteV};};
            m_verts.push_back({cx-0.5f,cy-0.5f,0,1,color,f.whiteU,f.whiteV});
            m_verts.push_back(vertex(start+i*1.57079633f/12)); m_verts.push_back(vertex(start+(i+1)*1.57079633f/12));
        }
    }
}

wchar_t Renderer::Shape(const Font& f, wchar_t c) {
    return f.upper ? (wchar_t)(uintptr_t)CharUpperW((LPWSTR)(uintptr_t)c) : c;  // one character in the low word
}

const Renderer::Glyph* Renderer::GetGlyph(Font& f, wchar_t ch) {
    auto it = f.glyphs.find(ch);
    if (it != f.glyphs.end()) return &it->second;
    if (ch == 0x401) return GetGlyph(f, 0x415);  // Ё -> Е if missing
    it = f.glyphs.find(L'?');
    return it != f.glyphs.end() ? &it->second : nullptr;
}

static bool ParseColorCode(std::wstring_view t, size_t i, uint32_t& rgb) {
    if (i + 7 >= t.size() + 0 || t[i] != L'{' || i + 7 >= t.size() || t[i + 7] != L'}') return false;
    uint32_t v = 0;
    for (size_t k = 1; k <= 6; ++k) {
        wchar_t c = t[i + k];
        v <<= 4;
        if (c >= L'0' && c <= L'9') v |= c - L'0';
        else if (c >= L'a' && c <= L'f') v |= c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') v |= c - L'A' + 10;
        else return false;
    }
    rgb = v;
    return true;
}

void Renderer::DrawRun(Font& f, float x, float y, std::wstring_view text, uint32_t argb) {
    SetTexture(f.tex);
    float pen = x;
    uint32_t color = argb;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t rgb;
        if (text[i] == L'{' && ParseColorCode(text, i, rgb)) {
            color = (argb & 0xFF000000) | rgb;
            i += 7;
            continue;
        }
        const Glyph* g = GetGlyph(f, Shape(f, text[i]));
        if (!g) continue;
        if (text[i] != L' ') Quad(std::floor(pen - f.pad), std::floor(y - f.pad), g->w, g->h, g->u0, g->v0, g->u1, g->v1, color);
        pen += g->advance + f.tracking;
    }
}

float Renderer::TextWidthW(FontId fid, std::wstring_view text) {
    Font& f = m_fonts[fid];
    float w = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t rgb;
        if (text[i] == L'{' && ParseColorCode(text, i, rgb)) {
            i += 7;
            continue;
        }
        if (const Glyph* g = GetGlyph(f, Shape(f, text[i]))) w += g->advance + f.tracking;
    }
    return w;
}

float Renderer::TextWidth(FontId f, std::string_view utf8) { return TextWidthW(f, Widen(utf8)); }

void Renderer::TextW(FontId fid, float x, float y, std::wstring_view text, uint32_t argb, uint32_t flags) {
    if (!m_inFrame || text.empty()) return;
    Font& f = m_fonts[fid];
    if (flags & (TEXT_CENTER | TEXT_RIGHT)) {
        float w = TextWidthW(fid, text);
        x -= (flags & TEXT_CENTER) ? w * 0.5f : w;
    }
    x = std::floor(x);
    y = std::floor(y);
    uint32_t shadow = fid==FONT_CHAT ? (argb&0xFF000000) : WithAlpha(0xFF000000, (float)(argb >> 24) / 255.0f * 0.85f);
    // shadows must not take inline colours: strip codes by drawing with a fixed colour
    auto drawShadow = [&](float dx, float dy) {
        std::wstring plain;
        plain.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            uint32_t rgb;
            if (text[i] == L'{' && ParseColorCode(text, i, rgb)) {
                i += 7;
                continue;
            }
            plain += text[i];
        }
        DrawRun(f, x + dx, y + dy, plain, shadow);
    };
    if (flags & TEXT_OUTLINE) {
        float stroke=fid==FONT_CHAT ? float(m_chatSettings.outline) : 1.f;
        if(stroke>0) {
            drawShadow(-stroke,0); drawShadow(stroke,0); drawShadow(0,-stroke); drawShadow(0,stroke);
            if(stroke>1) { drawShadow(-1,-1); drawShadow(1,-1); drawShadow(-1,1); drawShadow(1,1); }
        }
    } else if (flags & TEXT_SHADOW) {
        drawShadow(1, 1);
    }
    DrawRun(f, x, y, text, argb);
}

void Renderer::Text(FontId f, float x, float y, std::string_view utf8, uint32_t argb, uint32_t flags) {
    TextW(f, x, y, Widen(utf8), argb, flags);
}

std::vector<std::wstring> Renderer::Wrap(FontId fid, std::wstring_view text, float maxWidth) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    std::wstring lastColor;
    float curW = 0;
    Font& f = m_fonts[fid];
    size_t lastSpace = std::wstring::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t rgb;
        if (text[i] == L'{' && ParseColorCode(text, i, rgb)) {
            lastColor = std::wstring(text.substr(i, 8));
            cur += lastColor;
            i += 7;
            continue;
        }
        wchar_t ch = text[i];
        const Glyph* g = GetGlyph(f, Shape(f, ch));
        float adv = g ? g->advance + f.tracking : 0;
        if (curW + adv > maxWidth && !cur.empty()) {
            if (lastSpace != std::wstring::npos && lastSpace > 0) {
                std::wstring rest = cur.substr(lastSpace + 1);
                cur.resize(lastSpace);
                lines.push_back(cur);
                cur = lastColor + rest;
                curW = TextWidthW(fid, rest);
            } else {
                lines.push_back(cur);
                cur = lastColor;
                curW = 0;
            }
            lastSpace = std::wstring::npos;
        }
        if (ch == L' ') lastSpace = cur.size();
        cur += ch;
        curW += adv;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.emplace_back();
    return lines;
}


} // namespace vc::ui

