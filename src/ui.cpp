#include "ui.h"
#include "confirmed_motion.h"
#include "render_profile.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib,"gdiplus.lib")

namespace ui {
namespace {
struct DrawingRuntime {ULONG_PTR token{};DrawingRuntime(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}~DrawingRuntime(){if(token)Gdiplus::GdiplusShutdown(token);}};
void Ready(){static DrawingRuntime runtime;}
Gdiplus::Color Color(COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));}
void Smooth(Gdiplus::Graphics& g){g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);}
void RoundedPath(Gdiplus::GraphicsPath& p,float x,float y,float w,float h,float diameter){float d=std::min(diameter,std::min(w,h));if(d<=0){p.AddRectangle(Gdiplus::RectF(x,y,w,h));return;}p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();}
}
namespace {
struct SliderProgress {ConfirmedMotion motion;bool repaint=false,hasAck=false;double acknowledged=0;};
SliderProgress* Progress(HWND hwnd){return static_cast<SliderProgress*>(GetPropW(hwnd,L"ActualMotion"));}
}
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
        : Palette{RGB(32,38,49), RGB(39,47,60), RGB(237,243,252), RGB(168,180,197),
        RGB(59,67,80), RGB(141,204,255), RGB(22,37,50), RGB(49,64,82), RGB(77,87,104)};
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
void BackBuffer::Clear(){if(dc_){if(original_)SelectObject(dc_,original_);if(bitmap_)DeleteObject(bitmap_);DeleteDC(dc_);}dc_=nullptr;bitmap_=nullptr;original_=nullptr;width_=height_=0;}
HDC BackBuffer::Begin(HDC target,int width,int height){
    if(!dc_)dc_=CreateCompatibleDC(target);if(!dc_)return nullptr;
    if(width_!=width||height_!=height){HBITMAP next=CreateCompatibleBitmap(target,width,height);if(!next)return nullptr;auto old=SelectObject(dc_,next);if(!original_)original_=old;if(bitmap_)DeleteObject(bitmap_);bitmap_=next;width_=width;height_=height;}
    return dc_;
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
    if(rect.right<=rect.left||rect.bottom<=rect.top)return;
    Ready();Gdiplus::Graphics g(dc);Smooth(g);Gdiplus::GraphicsPath p;
    RoundedPath(p,rect.left+0.5f,rect.top+0.5f,static_cast<float>(rect.right-rect.left-1),static_cast<float>(rect.bottom-rect.top-1),static_cast<float>(radius));
    Gdiplus::SolidBrush brush(Color(fill));g.FillPath(&brush,&p);
    if(fill!=border){Gdiplus::Pen pen(Color(border),1.0f);g.DrawPath(&pen,&p);}

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
    const float x=(thumb.left+thumb.right)/2.0f,y=(thumb.top+thumb.bottom)/2.0f;
    int half=(thumb.right-thumb.left)/2;
    const float left=static_cast<float>(std::max<int>(channel.left,half)),right=static_cast<float>(std::min<int>(channel.right,client.right-half));
    COLORREF accent=IsWindowEnabled(hwnd)?colors.accent:colors.track;
    float actual=x;auto encoded=reinterpret_cast<INT_PTR>(GetPropW(hwnd,L"ActualLevel"));
    if(encoded){int low=static_cast<int>(SendMessageW(hwnd,TBM_GETRANGEMIN,0,0)),high=static_cast<int>(SendMessageW(hwnd,TBM_GETRANGEMAX,0,0));
        auto progress=Progress(hwnd);double value=progress?progress->motion.Value(render_profile::NowMilliseconds()):static_cast<double>(encoded-1);
        actual=std::clamp(left+static_cast<float>((value-low)*(right-left)/std::max(1,high-low)),left,right);
    }
    auto mix=[](COLORREF a,COLORREF b){return RGB((GetRValue(a)*38+GetRValue(b)*62)/100,(GetGValue(a)*38+GetGValue(b)*62)/100,(GetBValue(a)*38+GetBValue(b)*62)/100);};
    Ready();Gdiplus::Graphics g(dc);Smooth(g);
    auto track=[&](float end,COLORREF color){if(end<=left)return;Gdiplus::GraphicsPath path;RoundedPath(path,left,y-surface.S(2),end-left,static_cast<float>(surface.S(4)),static_cast<float>(surface.S(4)));Gdiplus::SolidBrush fill(Color(color));g.FillPath(&fill,&path);};
    track(right,colors.track);track(std::max(x,actual),mix(accent,colors.track));track(actual,accent);
    float radius=static_cast<float>(surface.S(10));Gdiplus::SolidBrush outer(Color(colors.card));g.FillEllipse(&outer,x-radius,y-radius,2*radius,2*radius);
    Gdiplus::Pen border(Color(colors.border),1);g.DrawEllipse(&border,x-radius+0.5f,y-radius+0.5f,2*radius-1,2*radius-1);
    float inner=radius-surface.S(4);Gdiplus::SolidBrush dot(Color(accent));g.FillEllipse(&dot,x-inner,y-inner,2*inner,2*inner);
    if(GetFocus()==hwnd&&!(SendMessageW(hwnd,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){Gdiplus::Pen focus(Color(accent),1);float r=radius+surface.S(2);g.DrawEllipse(&focus,x-r,y-r,2*r,2*r);}

}
LRESULT CALLBACK Trackbar(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& surface=*reinterpret_cast<Surface*>(data);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        if(r.right>0&&r.bottom>0){auto buffer=static_cast<BackBuffer*>(GetPropW(hwnd,L"PaintBuffer"));if(!buffer){buffer=new BackBuffer;SetPropW(hwnd,L"PaintBuffer",buffer);}HDC target=buffer->Begin(dc,r.right,r.bottom);if(target){PaintTrackbar(hwnd,target,surface);BitBlt(dc,0,0,r.right,r.bottom,target,0,0,SRCCOPY);}}
        EndPaint(hwnd,&ps);return 0;
    }
    if(message==WM_PRINTCLIENT){PaintTrackbar(hwnd,reinterpret_cast<HDC>(wp),surface);return 0;}
    if(message==WM_NCDESTROY){delete static_cast<BackBuffer*>(RemovePropW(hwnd,L"PaintBuffer"));delete static_cast<SliderProgress*>(RemovePropW(hwnd,L"ActualMotion"));RemovePropW(hwnd,L"ActualLevel");RemoveWindowSubclass(hwnd,Trackbar,2);}
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
void BeginSliderActual(HWND slider){if(auto state=Progress(slider))state->motion.Begin(render_profile::NowMilliseconds());}
bool SetSliderActual(HWND slider,double percent,bool animate){
    if(!slider)return false;auto now=render_profile::NowMilliseconds();auto state=Progress(slider);
    if(!state){state=new SliderProgress;if(!SetPropW(slider,L"ActualMotion",state)){delete state;return false;}}
    percent=std::clamp(percent,0.0,100.0);auto value=reinterpret_cast<HANDLE>(static_cast<INT_PTR>(std::lround(percent)+1));
    if(state->hasAck&&state->acknowledged==percent&&animate)return state->motion.Active(now);state->hasAck=true;state->acknowledged=percent;
    SetPropW(slider,L"ActualLevel",value);bool moving=state->motion.Confirm(percent,now,animate);state->repaint=moving;InvalidateRect(slider,nullptr,FALSE);return moving;
}
bool AdvanceSliderActual(HWND slider){
    auto state=Progress(slider);if(!state||!state->repaint)return false;
    bool moving=state->motion.Active(render_profile::NowMilliseconds());InvalidateRect(slider,nullptr,FALSE);state->repaint=moving;return moving;
}

void DrawGlyph(HDC dc,RECT bounds,Glyph glyph,COLORREF color){
    Ready();Gdiplus::Graphics g(dc);Smooth(g);const float size=static_cast<float>(std::min(bounds.right-bounds.left,bounds.bottom-bounds.top));
    g.TranslateTransform((bounds.left+bounds.right)/2.0f,(bounds.top+bounds.bottom)/2.0f);g.ScaleTransform(size/32,size/32);g.TranslateTransform(-12,-12);
    Gdiplus::Pen pen(Color(color),1.6f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
    if(glyph==Glyph::Back){Gdiplus::PointF points[]={{14,5},{7,12},{14,19}};g.DrawLines(&pen,points,3);g.DrawLine(&pen,7.0f,12.0f,21.0f,12.0f);}
    else if(glyph==Glyph::Chevron){Gdiplus::PointF points[]={{6,9},{12,15},{18,9}};g.DrawLines(&pen,points,3);}
    else if(glyph==Glyph::Monitor){Gdiplus::GraphicsPath p;RoundedPath(p,2,3,20,14,3);g.DrawPath(&pen,&p);g.DrawLine(&pen,12.0f,17.0f,12.0f,21.0f);g.DrawLine(&pen,8.0f,21.0f,16.0f,21.0f);}
    else if(glyph==Glyph::Settings){Gdiplus::PointF points[32];for(int i=0;i<32;++i){double a=i*6.283185307179586/32,r=(i%4==0||i%4==3)?10:8;points[i]={static_cast<float>(12+std::sin(a)*r),static_cast<float>(12+std::cos(a)*r)};}g.DrawPolygon(&pen,points,32);g.DrawEllipse(&pen,8.0f,8.0f,8.0f,8.0f);}
    else {Gdiplus::GraphicsPath first,second;first.AddLine(8.0f,11.0f,6.0f,13.0f);first.AddBezier(6.0f,13.0f,2.687f,16.313f,8.687f,22.313f,12.0f,19.0f);first.AddLine(12.0f,19.0f,15.0f,16.0f);second.AddLine(16.0f,13.0f,18.0f,11.0f);second.AddBezier(18.0f,11.0f,21.313f,7.687f,15.313f,1.687f,12.0f,5.0f);second.AddLine(12.0f,5.0f,9.0f,8.0f);g.DrawPath(&pen,&first);g.DrawPath(&pen,&second);g.DrawLine(&pen,10.0f,14.0f,14.0f,10.0f);}
}
void DrawSun(HDC dc,RECT bounds,double angle){
    Ready();Gdiplus::Graphics g(dc);Smooth(g);float size=static_cast<float>(std::min(bounds.right-bounds.left,bounds.bottom-bounds.top));
    g.TranslateTransform((bounds.left+bounds.right)/2.0f,(bounds.top+bounds.bottom)/2.0f);g.ScaleTransform(size/32,size/32);
    Gdiplus::Pen pen(Color(colors.text),2.0f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);g.DrawEllipse(&pen,-6.7f,-6.7f,13.4f,13.4f);
    g.RotateTransform(static_cast<float>(angle*180/3.14159265358979323846));
    for(int i=0;i<8;++i){g.DrawLine(&pen,0.0f,-10.8f,0.0f,-13.7f);g.RotateTransform(45);}
}
HICON CreateTrayIcon(bool forTray,double angle,int pixelSize) {
    // Analytic ring and round-ended rays, with antialiased coverage.
    // No raster assets or retained animation frames.
    const int extent=std::clamp(pixelSize,16,128);
    constexpr double pi=3.14159265358979323846;
    struct Ray { double x,y; };
    Ray rays[8]{};
    for(int i=0;i<8;++i){double a=angle+i*pi/4;rays[i]={std::cos(a),std::sin(a)};}
    DWORD light=0,size=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
    bool white=forTray?light==0:GetRValue(colors.background)<100;
    BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth=extent;format.bmiHeader.biHeight=-extent;
    format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;
    HDC dc=GetDC(nullptr);void* bits=nullptr;
    HBITMAP color=CreateDIBSection(dc,&format,DIB_RGB_COLORS,&bits,nullptr,0);
    HICON result=nullptr;
    if(color&&bits) {
        // Optical sizing: the tray is a 16-DIP glyph. Stronger strokes and
        // shorter rays keep every spoke visible without touching the canvas.
        const double ringRadius=forTray?5.6:6.7,ringHalf=forTray?1.6:1.0;
        const double rayInner=forTray?10.0:10.8,rayOuter=forTray?12.2:13.7,rayHalf=forTray?1.7:1.0;
        auto pixels=static_cast<DWORD*>(bits);
        for(int y=0;y<extent;++y)for(int x=0;x<extent;++x) {
            double px=(x+0.5)*32/extent-16,py=(y+0.5)*32/extent-16;
            double distance=std::abs(std::hypot(px,py)-ringRadius)-ringHalf;
            for(const auto& ray:rays) {
                double along=std::clamp(px*ray.x+py*ray.y,rayInner,rayOuter);
                distance=std::min(distance,std::hypot(px-along*ray.x,py-along*ray.y)-rayHalf);
            }
            DWORD alpha=static_cast<DWORD>(std::lround(std::clamp(0.5-distance*extent/32,0.0,1.0)*255));
            pixels[y*extent+x]=(alpha<<24)|(white?(alpha<<16)|(alpha<<8)|alpha:0);
        }
        std::vector<BYTE> maskBits(static_cast<size_t>((extent+15)/16)*2*extent);
        HBITMAP mask=CreateBitmap(extent,extent,1,1,maskBits.data());
        if(mask){ICONINFO info{};info.fIcon=TRUE;info.hbmColor=color;info.hbmMask=mask;
            result=CreateIconIndirect(&info);DeleteObject(mask);}
    }
    if(color)DeleteObject(color);ReleaseDC(nullptr,dc);
    return result;
}
}
