#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>

namespace ui {
struct Palette { COLORREF background, card, text, muted, border, accent, onAccent, hover, track; };
extern Palette colors;
extern HBRUSH backgroundBrush, cardBrush;
extern int themeOverride; // 0: system, 1: light, 2: dark; optional launch override.
void RefreshTheme();
struct Surface {
    UINT dpi = 96;
    HFONT body{}, caption{}, title{}, heading{}, number{}, symbol{};
    void Init(HWND window);
    void Clear();
    int S(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    ~Surface() { Clear(); }
};
class BackBuffer {
public:
    BackBuffer()=default;
    BackBuffer(const BackBuffer&)=delete;
    BackBuffer& operator=(const BackBuffer&)=delete;
    HDC Begin(HDC target,int width,int height);
    void Clear();
    ~BackBuffer(){Clear();}
private:
    HDC dc_{};HBITMAP bitmap_{};HGDIOBJ original_{};int width_{},height_{};
};
void Frame(HWND window);
void Fill(HDC dc, RECT rect, COLORREF color);
void Round(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius);
void Text(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
HWND Control(HWND parent, Surface& surface, const wchar_t* type, const std::wstring& text, DWORD style,
    int id, bool card = false);
void Place(HWND control, const Surface& surface, int x, int y, int width, int height);
void Button(HWND parent, Surface& surface, const std::wstring& text, int id, bool accent = false, bool card = false);
LRESULT SliderDraw(NMHDR* header, const Surface& surface);
bool DrawButton(const DRAWITEMSTRUCT& item, const Surface& surface);
LRESULT ColorControl(UINT message, WPARAM dc, LPARAM control);
enum class Glyph { Link,Settings,Back,Chevron,Monitor };
void DrawGlyph(HDC dc,RECT bounds,Glyph glyph,COLORREF color);
void DrawSun(HDC dc,RECT bounds,double angle=0);
bool SetSliderActual(HWND slider,double percent,bool animate=true);
void BeginSliderActual(HWND slider);
bool AdvanceSliderActual(HWND slider);
HICON CreateTrayIcon(bool forTray=true,double angle=0,int pixels=32);
}

