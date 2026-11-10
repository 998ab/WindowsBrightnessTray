#pragma once
#include <windows.h>
#include <algorithm>

namespace tray_position {
// Coordinates are physical screen pixels, including negative monitor origins.
inline bool InTaskbar(const RECT& icon,const RECT& taskbar){
    POINT center{(icon.left+icon.right)/2,(icon.top+icon.bottom)/2};return PtInRect(&taskbar,center)!=FALSE;
}
inline RECT OverflowAnchor(const RECT& notification,const RECT& taskbar,int buttonSize){
    RECT r=notification;
    if(taskbar.right-taskbar.left>=taskbar.bottom-taskbar.top)r.right=std::min(r.right,r.left+buttonSize);
    else r.bottom=std::min(r.bottom,r.top+buttonSize);
    return r;
}
inline POINT Place(const RECT& work,const RECT& anchor,const RECT& taskbar,int width,int height,int gap,const RECT* overflow=nullptr){
    LONG x=anchor.right-width,y=anchor.bottom-height;
    if(taskbar.right-taskbar.left>=taskbar.bottom-taskbar.top){
        if((taskbar.top+taskbar.bottom)/2>=(work.top+work.bottom)/2)y=std::min(work.bottom,taskbar.top)-height-gap;
        else y=std::max(work.top,taskbar.bottom)+gap;
    }else{
        if((taskbar.left+taskbar.right)/2>=(work.left+work.right)/2)x=std::min(work.right,taskbar.left)-width-gap;
        else x=std::max(work.left,taskbar.right)+gap;
    }
    if(overflow&&!IsRectEmpty(overflow)){
        if(taskbar.right-taskbar.left>=taskbar.bottom-taskbar.top){
            if((taskbar.top+taskbar.bottom)/2>=(work.top+work.bottom)/2)y=overflow->top-height-gap;
            else y=overflow->bottom+gap;
        }else{
            if((taskbar.left+taskbar.right)/2>=(work.left+work.right)/2)x=overflow->left-width-gap;
            else x=overflow->right+gap;
        }
    }
    return {std::clamp(x,work.left,std::max(work.left,work.right-width)),std::clamp(y,work.top,std::max(work.top,work.bottom-height))};
}
bool Resolve(HWND owner,RECT& anchor,RECT& taskbar,RECT* overflow=nullptr);
}
