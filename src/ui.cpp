#include "ui.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <vector>
#include <cmath>

namespace ui {
Palette colors;
HBRUSH backgroundBrush{}, cardBrush{};
int themeOverride=0;
void RefreshTheme() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    if(themeOverride) light=themeOverride==1;
    colors = light ? Palette{RGB(243,243,243), RGB(255,255,255), RGB(26,26,26), RGB(100,100,100),
        RGB(225,225,225), RGB(0,95,184), RGB(255,255,255), RGB(245,245,245), RGB(135,135,135)}
        : Palette{RGB(32,32,32), RGB(43,43,43), RGB(245,245,245), RGB(178,178,178),
        RGB(65,65,65), RGB(96,205,255), RGB(0,45,65), RGB(58,58,58), RGB(155,155,155)};
    if (backgroundBrush) DeleteObject(backgroundBrush);
    if (cardBrush) DeleteObject(cardBrush);
    backgroundBrush = CreateSolidBrush(colors.background); cardBrush = CreateSolidBrush(colors.card);
}
void Surface::Clear() {
    for (HFONT f : {body, caption, title, heading, number, symbol}) if (f) DeleteObject(f);
    body = caption = title = heading = number = symbol = nullptr;
}
void Surface::Init(HWND window) {
    Clear(); dpi = GetDpiForWindow(window);
    auto make = [&](int height, int weight, const wchar_t* face = L"Segoe UI") {
        return CreateFontW(-S(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    };
    body = make(14, FW_NORMAL); caption = make(12, FW_NORMAL); title = make(28, FW_SEMIBOLD);
    heading = make(18, FW_SEMIBOLD); number = make(26, FW_SEMIBOLD); symbol = make(38, FW_SEMIBOLD);
}
void Frame(HWND window) {
    const BOOL dark = GetRValue(colors.background) < 100;
    const DWORD corner = 2;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
    DwmSetWindowAttribute(window, 35, &colors.background, sizeof(COLORREF));
}
void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
}
void Round(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, border);
    auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(brush); DeleteObject(pen);
}
void Text(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT flags) {
    auto old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags); SelectObject(dc, old);
}
LRESULT CALLBACK Hover(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEMOVE && !GetPropW(hwnd, L"Hot")) {
        SetPropW(hwnd, L"Hot", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track); InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE) { RemovePropW(hwnd, L"Hot"); InvalidateRect(hwnd, nullptr, FALSE); }
    if (message == WM_NCDESTROY) { RemovePropW(hwnd, L"Hot"); RemoveWindowSubclass(hwnd, Hover, 1); }
    return DefSubclassProc(hwnd, message, wp, lp);
}
void PaintTrackbar(HWND hwnd,HDC dc,const Surface& surface) {
    RECT client;GetClientRect(hwnd,&client);Fill(dc,client,GetPropW(hwnd,L"Card")?colors.card:colors.background);
    RECT channel{},thumb{};SendMessageW(hwnd,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    SendMessageW(hwnd,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    int x=(thumb.left+thumb.right)/2,y=(thumb.top+thumb.bottom)/2;
    RECT track{channel.left,y-surface.S(2),channel.right,y+surface.S(2)};
    Round(dc,track,colors.track,colors.track,surface.S(4));
    COLORREF accent=IsWindowEnabled(hwnd)?colors.accent:colors.track;
    track.right=std::clamp<LONG>(x,track.left,track.right);
    if(track.right>track.left)Round(dc,track,accent,accent,surface.S(4));
    int radius=surface.S(10);RECT outer{x-radius,y-radius,x+radius,y+radius};
    Round(dc,outer,colors.card,colors.border,radius*2);InflateRect(&outer,-surface.S(4),-surface.S(4));
    Round(dc,outer,accent,accent,radius*2);
    if(GetFocus()==hwnd){InflateRect(&client,-2,-2);DrawFocusRect(dc,&client);}
}
LRESULT CALLBACK Trackbar(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& surface=*reinterpret_cast<Surface*>(data);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        if(r.right>0&&r.bottom>0){HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
            auto old=SelectObject(buffer,bitmap);PaintTrackbar(hwnd,buffer,surface);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);}
        EndPaint(hwnd,&ps);return 0;
    }
    if(message==WM_PRINTCLIENT){PaintTrackbar(hwnd,reinterpret_cast<HDC>(wp),surface);return 0;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,Trackbar,2);
    LRESULT result=DefSubclassProc(hwnd,message,wp,lp);
    switch(message){case TBM_SETPOS:case TBM_SETRANGE:case TBM_SETRANGEMIN:case TBM_SETRANGEMAX:
    case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_KEYDOWN:case WM_KEYUP:
    case WM_MOUSEWHEEL:case WM_ENABLE:case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(hwnd,nullptr,FALSE);break;}
    return result;
}
HWND Control(HWND parent, Surface& surface, const wchar_t* type, const std::wstring& text, DWORD style, int id, bool card) {
    HWND control = CreateWindowExW(0, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (card) SetPropW(control, L"Card", reinterpret_cast<HANDLE>(1));
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(surface.body), TRUE);
    SetWindowTheme(control, L"Explorer", nullptr);
    if(wcscmp(type,TRACKBAR_CLASSW)==0)SetWindowSubclass(control,Trackbar,2,reinterpret_cast<DWORD_PTR>(&surface));
    return control;
}
void Place(HWND control, const Surface& surface, int x, int y, int width, int height) {
    MoveWindow(control, surface.S(x), surface.S(y), surface.S(width), surface.S(height), TRUE);
}
void Button(HWND parent, Surface& surface, const std::wstring& text, int id, bool accent, bool card) {
    HWND control = Control(parent, surface, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, card);
    if (accent) SetPropW(control, L"Accent", reinterpret_cast<HANDLE>(1));
    SetWindowSubclass(control, Hover, 1, 0);
}
bool DrawButton(const DRAWITEMSTRUCT& item, const Surface& surface) {
    if (item.CtlType != ODT_BUTTON) return false;
    const bool accent = GetPropW(item.hwndItem, L"Accent") != nullptr;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hover = GetPropW(item.hwndItem, L"Hot") || (item.itemState & ODS_SELECTED);
    Fill(item.hDC, item.rcItem, GetPropW(item.hwndItem, L"Card") ? colors.card : colors.background);
    RECT r = item.rcItem; InflateRect(&r, -1, -1);
    Round(item.hDC, r, disabled ? colors.background : accent ? colors.accent : hover ? colors.hover : colors.card,
        accent && !disabled ? colors.accent : colors.border, surface.S(8));
    wchar_t label[256]{}; GetWindowTextW(item.hwndItem, label, 256);
    Text(item.hDC, label, r, surface.body, disabled ? colors.muted : accent ? colors.onAccent : colors.text,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (item.itemState & ODS_FOCUS) { InflateRect(&r, -surface.S(4), -surface.S(4)); DrawFocusRect(item.hDC, &r); }
    return true;
}
LRESULT SliderDraw(NMHDR* header, const Surface& surface) {
    if (header->code != NM_CUSTOMDRAW) return CDRF_DODEFAULT;
    auto& draw = *reinterpret_cast<NMCUSTOMDRAW*>(header);
    bool card = GetPropW(header->hwndFrom, L"Card") != nullptr;
    COLORREF background = card ? colors.card : colors.background;
    if (draw.dwDrawStage == CDDS_PREPAINT) {
        RECT rect; GetClientRect(header->hwndFrom, &rect); Fill(draw.hdc, rect, background);
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    }
    if (draw.dwDrawStage == CDDS_ITEMPREPAINT) {
        RECT rect = draw.rc;
        const COLORREF accent = IsWindowEnabled(header->hwndFrom) ? colors.accent : colors.track;
        if (draw.dwItemSpec == TBCD_CHANNEL) {
            RECT thumb{}; SendMessageW(header->hwndFrom, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
            rect.top = (thumb.top + thumb.bottom) / 2 - surface.S(2); rect.bottom = rect.top + surface.S(4);
            Round(draw.hdc, rect, colors.track, colors.track, surface.S(4));
            rect.right = std::clamp((thumb.left + thumb.right) / 2, rect.left, rect.right);
            Round(draw.hdc, rect, accent, accent, surface.S(4));
        } else if (draw.dwItemSpec == TBCD_THUMB) {
            int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2, radius = surface.S(10);
            RECT outer{x-radius,y-radius,x+radius,y+radius};
            Round(draw.hdc, outer, colors.card, colors.border, radius * 2);
            InflateRect(&outer, -surface.S(4), -surface.S(4)); Round(draw.hdc, outer, accent, accent, radius * 2);
        }
        return CDRF_SKIPDEFAULT;
    }
    if (draw.dwDrawStage == CDDS_POSTPAINT && GetFocus() == header->hwndFrom) {
        RECT rect; GetClientRect(header->hwndFrom, &rect); InflateRect(&rect, -2, -2); DrawFocusRect(draw.hdc, &rect);
    }
    return CDRF_DODEFAULT;
}
LRESULT ColorControl(UINT, WPARAM wp, LPARAM lp) {
    HDC dc = reinterpret_cast<HDC>(wp); HWND control = reinterpret_cast<HWND>(lp);
    bool card = GetPropW(control, L"Card") != nullptr;
    SetTextColor(dc, colors.text); SetBkColor(dc, card ? colors.card : colors.background);
    return reinterpret_cast<LRESULT>(card ? cardBrush : backgroundBrush);
}
HICON CreateTrayIcon(bool forTray,double angle) {
    HICON source=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100),IMAGE_ICON,32,32,0));
    if(!source)return nullptr;
    ICONINFO info{};if(!GetIconInfo(source,&info)){DestroyIcon(source);return nullptr;}
    BITMAP bitmap{};GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap);
    BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth=bitmap.bmWidth;format.bmiHeader.biHeight=-bitmap.bmHeight;
    format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;
    std::vector<DWORD> pixels(static_cast<size_t>(bitmap.bmWidth)*bitmap.bmHeight);
    HDC dc=GetDC(nullptr);GetDIBits(dc,info.hbmColor,0,static_cast<UINT>(bitmap.bmHeight),pixels.data(),&format,DIB_RGB_COLORS);
    DWORD light=0,size=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
    bool white=forTray?light==0:GetRValue(colors.background)<100;
    if(angle!=0) {
        auto sourcePixels=pixels;double cosine=std::cos(angle),sine=std::sin(angle);
        double cx=(bitmap.bmWidth-1)/2.0,cy=(bitmap.bmHeight-1)/2.0;
        auto alpha=[&](int x,int y)->double{return x>=0&&y>=0&&x<bitmap.bmWidth&&y<bitmap.bmHeight?sourcePixels[static_cast<size_t>(y)*bitmap.bmWidth+x]>>24:0;};
        for(int y=0;y<bitmap.bmHeight;++y)for(int x=0;x<bitmap.bmWidth;++x) {
            double sx=cosine*(x-cx)+sine*(y-cy)+cx,sy=-sine*(x-cx)+cosine*(y-cy)+cy;
            int ix=static_cast<int>(std::floor(sx)),iy=static_cast<int>(std::floor(sy));double fx=sx-ix,fy=sy-iy;
            auto a=static_cast<DWORD>(std::lround((1-fy)*((1-fx)*alpha(ix,iy)+fx*alpha(ix+1,iy))+fy*((1-fx)*alpha(ix,iy+1)+fx*alpha(ix+1,iy+1))));
            pixels[static_cast<size_t>(y)*bitmap.bmWidth+x]=a<<24;
        }
    }
    // Use the generated raster's alpha silhouette, as Windows does with monochrome glyphs.
    for(auto& pixel:pixels){DWORD alpha=pixel>>24;pixel=(alpha<<24)|(white?(alpha<<16)|(alpha<<8)|alpha:0);}
    void* bits=nullptr;HBITMAP color=CreateDIBSection(dc,&format,DIB_RGB_COLORS,&bits,nullptr,0);
    HICON result=nullptr;
    if(color&&bits){memcpy(bits,pixels.data(),pixels.size()*sizeof(DWORD));ICONINFO tinted=info;tinted.hbmColor=color;result=CreateIconIndirect(&tinted);}
    if(color)DeleteObject(color);DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);ReleaseDC(nullptr,dc);DestroyIcon(source);
    return result;
}
}

