#include "localization.h"
#include "calibration.h"
#include <numeric>

namespace {
constexpr int SliderBase=200,Back=101,Next=102,CancelButton=103,Limit=104,Reference=105;
constexpr int anchors[]={-1,4,0,2,1,3,-1};
const wchar_t* titles[]={L"Match brightness across displays",L"Common maximum",L"Common minimum",
    L"Match the midpoint",L"Match the lower quarter",L"Match the upper quarter",L"Review matched curves"};
const wchar_t* descriptions[]={
    L"Each display will show a light sample on a neutral background. Choose a reference display. First match the range, then three intermediate levels.",
    L"Find the brightest shared level. The dimmest display at its maximum limits the others: lower their backlights until the samples match.",
    L"Find a comfortable shared minimum. The display with the brightest minimum limits the others: raise their brightness to match. Do not look for disappearing symbols.",
    L"The reference is fixed at the midpoint of its range. Adjust the other displays until the light samples look equally bright.",
    L"The reference is fixed at 25% of the shared slider. Match the other displays to account for their different brightness scales.",
    L"The reference is fixed at 75% of the shared slider. Match the light samples again.",
    L"Each display will have its own 0 / 25 / 50 / 75 / 100% curve. All displays are now at their matched midpoint."};
void Buffer(HWND hwnd,const std::function<void(HDC)>& paint) {
    PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
    if(r.right>0&&r.bottom>0) {
        HDC memory=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
        auto old=SelectObject(memory,bitmap);paint(memory);BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);
        SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
    }
    EndPaint(hwnd,&ps);
}
std::vector<calibration_layout::Placement> PatternLayout(const std::vector<Monitor>& monitors,size_t reference) {
    std::vector<calibration_layout::Rect> screens;
    for(const auto& m:monitors)screens.push_back({m.bounds.left,m.bounds.top,m.bounds.right,m.bounds.bottom});
    return calibration_layout::Plan(screens,reference);
}
RECT PatternBounds(const RECT& bounds,calibration_layout::Edge edge,int scale) {
    using calibration_layout::Edge;
    int width=std::min<int>(scale,(bounds.right-bounds.left)/4);
    int height=std::min<int>(scale,(bounds.bottom-bounds.top)/4);
    RECT r=bounds;
    if(edge==Edge::Left)r.right=r.left+width;if(edge==Edge::Right)r.left=r.right-width;
    if(edge==Edge::Top)r.bottom=r.top+height;if(edge==Edge::Bottom)r.top=r.bottom-height;
    return r;
}
RECT WizardWorkArea(HWND window,const std::vector<Monitor>& monitors,size_t reference,int scale){
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info);
    RECT work=info.rcWork;
    for(const auto& p:PatternLayout(monitors,reference))if(EqualRect(&monitors[p.monitor].bounds,&info.rcMonitor)){
        auto strip=PatternBounds(monitors[p.monitor].bounds,p.edge,scale);
        using calibration_layout::Edge;
        if(p.edge==Edge::Left)work.left=std::max(work.left,strip.right);
        if(p.edge==Edge::Right)work.right=std::min(work.right,strip.left);
        if(p.edge==Edge::Top)work.top=std::max(work.top,strip.bottom);
        if(p.edge==Edge::Bottom)work.bottom=std::min(work.bottom,strip.top);
    }
    return work;
}
}
void CalibrationWindow::Open(HWND owner,const std::vector<Monitor>& monitors,size_t reference,MonitorWorker& worker,
    std::function<bool(const std::vector<Calibration>&)> save) {
    if(window_){SetForegroundWindow(window_);return;}
    if(monitors.size()<2)return;
    owner_=owner;monitors_=monitors;reference_=std::min(reference,monitors_.size()-1);worker_=&worker;save_=std::move(save);
    nodes_.clear();values_.clear();patterns_.clear();scroll_=0;pageScroll_=0;step_=0;touched_=false;
    for(const auto& m:monitors_){nodes_.push_back(m.calibration.Nodes());values_.push_back(FromRaw(m.current,m.minimum,m.maximum));}
    originals_.assign(monitors_.size(),0);requests_.assign(monitors_.size(),0);known_.assign(monitors_.size(),false);failed_.assign(monitors_.size(),false);
    Normalize();
    WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BrightnessTray.Calibration";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    wc.lpfnWndProc=RowsProc;wc.lpszClassName=L"BrightnessTray.MatchRows";RegisterClassW(&wc);
    wc.lpfnWndProc=PatternProc;wc.lpszClassName=L"BrightnessTray.MatchPattern";RegisterClassW(&wc);
    const auto& bounds=monitors_[reference_].bounds;
    window_=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"BrightnessTray.Calibration",tr(L"Match display brightness"),
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,bounds.left+24,bounds.top+32,780,760,owner,nullptr,wc.hInstance,this);
    if(!window_)return;
    surface_.Init(window_);ui::Frame(window_);
    SendMessageW(window_,WM_SETICON,ICON_SMALL,SendMessageW(owner,WM_GETICON,ICON_SMALL,0));
    rows_=CreateWindowExW(WS_EX_CONTROLPARENT,L"BrightnessTray.MatchRows",tr(L"Brightness of each display"),
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,window_,nullptr,wc.hInstance,this);
    for(size_t i=0;i<monitors_.size();++i) {
        auto slider=ui::Control(rows_,surface_,TRACKBAR_CLASSW,tr(L"Brightness: ")+monitors_[i].name,TBS_NOTICKS|WS_TABSTOP,SliderBase+static_cast<int>(i),false);
        SetWindowSubclass(slider,RevealRow,91,reinterpret_cast<DWORD_PTR>(this));
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(slider,TBM_SETPOS,TRUE,values_[i]);
    }
    ui::Button(window_,surface_,tr(L"Cancel"),CancelButton);
    ui::Button(window_,surface_,tr(L"Back"),Back);
    ui::Button(window_,surface_,tr(L"Start"),Next,true);
    ui::Button(window_,surface_,tr(L"All to 100%"),Limit);
    ui::Button(window_,surface_,tr(L"Reference"),Reference);
    for(int id:{Back,Next,CancelButton,Limit,Reference})SetWindowSubclass(GetDlgItem(window_,id),RevealRow,91,reinterpret_cast<DWORD_PTR>(this));
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&info);
    auto placements=PatternLayout(monitors_,reference_);
    for(const auto& p:placements)if(p.monitor==reference_) {
        auto strip=PatternBounds(bounds,p.edge,surface_.S(240));
        using calibration_layout::Edge;
        if(p.edge==Edge::Left)info.rcWork.left=std::max(info.rcWork.left,strip.right);
        if(p.edge==Edge::Right)info.rcWork.right=std::min(info.rcWork.right,strip.left);
        if(p.edge==Edge::Top)info.rcWork.top=std::max(info.rcWork.top,strip.bottom);
        if(p.edge==Edge::Bottom)info.rcWork.bottom=std::min(info.rcWork.bottom,strip.top);
    }
    int width=std::min(surface_.S(420),static_cast<int>(info.rcWork.right-info.rcWork.left)-surface_.S(24));
    int height=std::min(surface_.S(520),static_cast<int>(info.rcWork.bottom-info.rcWork.top)-surface_.S(24));
    SetWindowPos(window_,nullptr,info.rcWork.left+surface_.S(12),info.rcWork.top+surface_.S(12),width,height,SWP_NOZORDER|SWP_SHOWWINDOW);
    EnableWindow(owner_,FALSE);Layout();ReadOriginals();SetForegroundWindow(window_);
}
LRESULT CALLBACK CalibrationWindow::Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto self=reinterpret_cast<CalibrationWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<CalibrationWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    return self?self->Message(msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK CalibrationWindow::RowsProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto self=reinterpret_cast<CalibrationWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<CalibrationWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:Buffer(hwnd,[&](HDC dc){if(self->surface_.body)self->PaintRows(dc);});return 0;
    case WM_SIZE:self->LayoutRows();return 0;
    case WM_HSCROLL:case WM_COMMAND:case WM_NOTIFY:case WM_DRAWITEM:return SendMessageW(self->window_,msg,wp,lp);
    case WM_MOUSEWHEEL:{RECT r;GetClientRect(self->window_,&r);if(self->surface_.S(self->pageHeight_)>r.bottom)return SendMessageW(self->window_,msg,wp,lp);self->scroll_-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;self->LayoutRows();return 0;}
    case WM_VSCROLL:{
        SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&si);int cmd=LOWORD(wp);
        if(cmd==SB_THUMBTRACK)self->scroll_=si.nTrackPos;
        if(cmd==SB_LINEUP)self->scroll_-=24;if(cmd==SB_LINEDOWN)self->scroll_+=24;
        if(cmd==SB_PAGEUP)self->scroll_-=static_cast<int>(si.nPage);if(cmd==SB_PAGEDOWN)self->scroll_+=static_cast<int>(si.nPage);
        self->LayoutRows();return 0;
    }
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK CalibrationWindow::RevealRow(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto self=reinterpret_cast<CalibrationWindow*>(data);
    if(message==WM_SETFOCUS)self->Reveal(child);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(child,RevealRow,91);
    return DefSubclassProc(child,message,wp,lp);
}
void CalibrationWindow::Reveal(HWND child){
    if(revealing_||!child||!IsWindowVisible(child)||!IsWindowEnabled(child)||(GetParent(child)!=rows_&&GetParent(child)!=window_))return;
    revealing_=true;
    if(GetParent(child)==rows_){
        RECT r,view;GetWindowRect(child,&r);MapWindowPoints(nullptr,rows_,reinterpret_cast<POINT*>(&r),2);GetClientRect(rows_,&view);
        int delta=r.top<surface_.S(4)?r.top-surface_.S(4):r.bottom>view.bottom-surface_.S(4)?r.bottom-view.bottom+surface_.S(4):0;
        if(delta){scroll_+=MulDiv(delta,96,static_cast<int>(surface_.dpi));LayoutRows();}
    }
    {
        RECT r,view;GetWindowRect(child,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&r),2);GetClientRect(window_,&view);
        int delta=r.top<surface_.S(4)?r.top-surface_.S(4):r.bottom>view.bottom-surface_.S(4)?r.bottom-view.bottom+surface_.S(4):0;
        if(delta){pageScroll_+=MulDiv(delta,96,static_cast<int>(surface_.dpi));Layout(false,false);}
    }
    revealing_=false;
}
void CalibrationWindow::ReadOriginals() {
    std::vector<Command> commands;message_=tr(L"Reading original brightness from all displays…");
    for(size_t i=0;i<monitors_.size();++i){failed_[i]=false;requests_[i]=NextTag();commands.push_back({Operation::Read,monitors_[i].key,0,requests_[i]});}
    worker_->QueueBatch(std::move(commands));Update();
}
void CalibrationWindow::OnEvent(const Event& event) {
    if(!window_||!event.final)return;
    for(size_t i=0;i<monitors_.size();++i)if(requests_[i] && event.tag==requests_[i] && event.key==monitors_[i].key) {
        requests_[i]=0;failed_[i]=!event.ok;
        if(step_==0&&event.operation==Operation::Read&&event.ok){originals_[i]=event.raw;values_[i]=FromRaw(event.raw,monitors_[i].minimum,monitors_[i].maximum);known_[i]=true;}
        if(event.ok)monitors_[i].current=event.raw;
        bool failed=std::any_of(failed_.begin(),failed_.end(),[](bool b){return b;});
        bool pending=std::any_of(requests_.begin(),requests_.end(),[](auto tag){return tag!=0;});
        if(failed)message_=step_==0?tr(L"Some displays did not respond. Click Read again or cancel calibration.")
            :tr(L"A display did not respond. Move its slider to retry; changes have not been saved.");
        else if(!pending)message_=step_==0?tr(L"Original values recorded. Warm up your displays and use your usual lighting and the same SDR mode.")
            :step_==6?tr(L"Review the curves and save. Cancel restores the original brightness on all displays.")
            :tr(L"Compare the samples across displays. Continue when their brightness matches.");
        Update();break;
    }
}
void CalibrationWindow::Normalize() {
    for(auto& n:nodes_) {
        n[4]=std::clamp(n[4],4,100);n[0]=std::clamp(n[0],0,n[4]-4);
        n[2]=std::clamp(n[2],n[0]+2,n[4]-2);n[1]=std::clamp(n[1],n[0]+1,n[2]-1);n[3]=std::clamp(n[3],n[2]+1,n[4]-1);
    }
}
void CalibrationWindow::Preview(const std::vector<size_t>& indices) {
    std::vector<Command> commands;touched_=true;
    for(auto i:indices){requests_[i]=NextTag();failed_[i]=false;commands.push_back({Operation::Write,monitors_[i].key,ToRaw(values_[i],monitors_[i].minimum,monitors_[i].maximum),requests_[i]});}
    worker_->QueueBatch(std::move(commands));message_=tr(L"Applying brightness…");Update();
}
void CalibrationWindow::Go(int step) {
    int previous=step_;step_=step;pageScroll_=0;Normalize();
    const int anchor=anchors[step_];
    std::vector<size_t> all;
    for(size_t i=0;i<monitors_.size();++i) {
        auto& n=nodes_[i];int low=0,high=100;
        if(step_==1){low=4;high=100;values_[i]=previous==0?values_[i]:n[4];}
        if(step_==2){low=0;high=n[4]-4;values_[i]=previous==1?std::min(values_[i],std::max(n[0],FromRaw(originals_[i],monitors_[i].minimum,monitors_[i].maximum)/4)):n[0];}
        if(step_==3){low=n[0]+2;high=n[4]-2;values_[i]=n[2];}
        if(step_==4){low=n[0]+1;high=n[2]-1;values_[i]=n[1];}
        if(step_==5){low=n[2]+1;high=n[4]-1;values_[i]=n[3];}
        if(step_>=3&&step_<=5&&i==reference_) {
            values_[i]=n[0]+((n[4]-n[0])*(anchor*25)+50)/100;
        }
        if(step_==6)values_[i]=n[2];
        values_[i]=std::clamp(values_[i],low,high);
        auto slider=GetDlgItem(rows_,SliderBase+static_cast<int>(i));
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(low,high));SendMessageW(slider,TBM_SETPOS,TRUE,values_[i]);all.push_back(i);
    }
    if(previous==0)ShowPatterns();
    Preview(all);Layout();
    if(step_<6) {
        size_t editable=step_>=3?(reference_+1)%monitors_.size():reference_;
        SetFocus(GetDlgItem(rows_,SliderBase+static_cast<int>(editable)));
    }else SetFocus(GetDlgItem(window_,CancelButton));
}
void CalibrationWindow::Update() {
    if(!window_)return;
    bool pending=std::any_of(requests_.begin(),requests_.end(),[](auto tag){return tag!=0;});
    bool known=std::all_of(known_.begin(),known_.end(),[](bool b){return b;});
    bool failed=std::any_of(failed_.begin(),failed_.end(),[](bool b){return b;});
    EnableWindow(GetDlgItem(window_,Next),!pending&&((known&&!failed)||step_==0));
    EnableWindow(GetDlgItem(window_,Back),!pending&&step_>1);
    ShowWindow(GetDlgItem(window_,Back),step_>1?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(window_,Limit),step_==1||step_==2?SW_SHOW:SW_HIDE);
    EnableWindow(GetDlgItem(window_,Limit),!pending);
    SetDlgItemTextW(window_,Limit,step_==2?tr(L"All to 0%"):tr(L"All to 100%"));
    EnableWindow(GetDlgItem(window_,Reference),step_==0&&!pending);
    SetDlgItemTextW(window_,Reference,(tr(L"Reference: ")+monitors_[reference_].name).c_str());
    SetDlgItemTextW(window_,Next,step_==0?(failed?tr(L"Read again"):tr(L"Start")):step_==6?tr(L"Save"):tr(L"Matched · next"));
    for(size_t i=0;i<monitors_.size();++i) {
        ShowWindow(GetDlgItem(rows_,SliderBase+static_cast<int>(i)),step_==0||step_==6?SW_HIDE:SW_SHOW);
        EnableWindow(GetDlgItem(rows_,SliderBase+static_cast<int>(i)),step_>0&&step_<6&&known_[i]&&!(i==reference_&&step_>=3));
        if(step_==0)SendDlgItemMessageW(rows_,SliderBase+static_cast<int>(i),TBM_SETPOS,TRUE,values_[i]);
    }
    InvalidateRect(window_,nullptr,FALSE);InvalidateRect(rows_,nullptr,FALSE);
    for(auto& pattern:patterns_)InvalidateRect(pattern->window,nullptr,FALSE);
    Layout();
    // Disabling the clicked Next/Limit button while waiting for DDC/CI can
    // otherwise leave this thread with no keyboard focus (including Escape).
    if(GetActiveWindow()==window_&&!GetFocus())SetFocus(GetDlgItem(window_,CancelButton));
}
void CalibrationWindow::Close(bool restore) {
    if(restore&&touched_) {
        std::vector<Command> commands;
        for(size_t i=0;i<monitors_.size();++i)if(known_[i])commands.push_back({Operation::Write,monitors_[i].key,originals_[i],NextTag()});
        worker_->QueueBatch(std::move(commands));
    }
    for(auto& pattern:patterns_)DestroyWindow(pattern->window);patterns_.clear();
    EnableWindow(owner_,TRUE);DestroyWindow(window_);window_=rows_=nullptr;surface_.Clear();ShowWindow(owner_,SW_SHOWNOACTIVATE);SetForegroundWindow(owner_);
}
void CalibrationWindow::Cancel(){if(window_)Close(true);}
std::vector<Monitor> CalibrationWindow::OriginalLevels() const {
    std::vector<Monitor> originals;
    if(window_)for(size_t i=0;i<monitors_.size();++i)if(known_[i]){
        auto m=monitors_[i];m.current=originals_[i];originals.push_back(std::move(m));
    }
    return originals;
}
std::wstring CalibrationWindow::StepText() const {
    return step_==0?tr(L"Click the reference name to choose another display."):step_==6?tr(L"All five points are ready.")
        :tr(L"STEP ")+std::to_wstring(step_)+tr(L" / 5   ·   Shared level ")+std::to_wstring(anchors[step_]*25)+L"%";
}
CalibrationWindow::LayoutInfo CalibrationWindow::MeasureLayout(int width,int height) const {
    LayoutInfo layout;HDC dc=GetDC(window_);
    auto dip=[&](int pixels){return (pixels*96+static_cast<int>(surface_.dpi)-1)/static_cast<int>(surface_.dpi);};
    auto textHeight=[&](const std::wstring& value,HFONT font){return dip(ui::MeasureText(dc,value,font,surface_.S(width-32)).cy);};
    auto buttonWidth=[&](int id,int minimum){wchar_t value[256]{};GetDlgItemTextW(window_,id,value,256);return std::max(minimum,dip(ui::MeasureText(dc,value,surface_.body,10000,DT_SINGLELINE|DT_NOPREFIX).cx)+24);};
    layout.buttonHeight=std::max(28,textHeight(L"Ag",surface_.body)+10);
    layout.titleHeight=textHeight(tr(titles[step_]),surface_.heading);
    layout.descriptionY=layout.titleY+layout.titleHeight+6;
    layout.descriptionHeight=textHeight(tr(descriptions[step_]),surface_.caption);
    layout.referenceY=layout.descriptionY+layout.descriptionHeight+8;
    layout.progressY=layout.referenceY+layout.buttonHeight+8;
    layout.progressHeight=textHeight(StepText(),surface_.caption);
    layout.rowsY=layout.progressY+layout.progressHeight+8;
    layout.statusHeight=std::max(textHeight(message_,surface_.caption),textHeight(L"Ag",surface_.caption)*2);
    layout.hintHeight=textHeight(tr(L"Esc cancels and restores original brightness."),surface_.caption);
    layout.cancelWidth=buttonWidth(CancelButton,72);layout.backWidth=buttonWidth(Back,64);
    layout.nextWidth=buttonWidth(Next,88);layout.limitWidth=buttonWidth(Limit,96);
    if(width<layout.cancelWidth+layout.backWidth+layout.nextWidth+56)layout.buttonRows=width>=layout.backWidth+layout.nextWidth+40?2:3;
    int below=10+layout.statusHeight+6+layout.hintHeight+12+layout.buttonHeight+16+(layout.buttonRows-1)*(layout.buttonHeight+8);
    int wantedRows=std::min(3,static_cast<int>(monitors_.size()))*68;
    layout.desiredHeight=layout.rowsY+wantedRows+below;
    layout.rowsHeight=std::max(40,height-layout.rowsY-below);
    layout.statusY=layout.rowsY+layout.rowsHeight+10;
    layout.hintY=layout.statusY+layout.statusHeight+6;
    layout.buttonsY=layout.hintY+layout.hintHeight+12;
    ReleaseDC(window_,dc);return layout;
}
void CalibrationWindow::Layout(bool fit,bool reveal) {
    if(!window_||!rows_||!surface_.body||layoutBusy_)return;
    layoutBusy_=true;
    RECT r;GetClientRect(window_,&r);int w=MulDiv(r.right,96,static_cast<int>(surface_.dpi)),h=MulDiv(r.bottom,96,static_cast<int>(surface_.dpi));
    auto layout=MeasureLayout(w,h);
    if(fit){
        RECT outer;GetWindowRect(window_,&outer);auto work=WizardWorkArea(window_,monitors_,reference_,surface_.S(240));
        int width=std::min(static_cast<int>(outer.right-outer.left),static_cast<int>(work.right-work.left)-surface_.S(24));
        int x=std::clamp(outer.left,work.left+surface_.S(12),std::max(work.left+surface_.S(12),work.right-width-surface_.S(12)));
        if(width!=outer.right-outer.left){
            SetWindowPos(window_,nullptr,x,outer.top,width,outer.bottom-outer.top,SWP_NOZORDER|SWP_NOACTIVATE);
            GetClientRect(window_,&r);w=MulDiv(r.right,96,static_cast<int>(surface_.dpi));h=MulDiv(r.bottom,96,static_cast<int>(surface_.dpi));layout=MeasureLayout(w,h);
            GetWindowRect(window_,&outer);
        }
        int nonclient=outer.bottom-outer.top-r.bottom;
        int height=std::min(surface_.S(layout.desiredHeight)+nonclient,static_cast<int>(work.bottom-work.top)-surface_.S(24));
        int y=std::clamp(outer.top,work.top+surface_.S(12),std::max(work.top+surface_.S(12),work.bottom-height-surface_.S(12)));
        SetWindowPos(window_,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        GetClientRect(window_,&r);h=MulDiv(r.bottom,96,static_cast<int>(surface_.dpi));layout=MeasureLayout(w,h);
    }
    pageHeight_=layout.buttonsY+layout.buttonHeight+16+(layout.buttonRows-1)*(layout.buttonHeight+8);
    pageScroll_=std::clamp(pageScroll_,0,std::max(0,pageHeight_-h));
    SCROLLINFO page{sizeof(page),SIF_RANGE|SIF_PAGE|SIF_POS,0,pageHeight_-1,static_cast<UINT>(h),pageScroll_,0};SetScrollInfo(window_,SB_VERT,&page,TRUE);
    GetClientRect(window_,&r);int currentWidth=MulDiv(r.right,96,static_cast<int>(surface_.dpi));
    if(currentWidth!=w){layoutBusy_=false;Layout(false,reveal);return;}
    bool limit=step_==1||step_==2;
    ui::Place(GetDlgItem(window_,Reference),surface_,16,layout.referenceY-pageScroll_,w-32-(limit?layout.limitWidth+8:0),layout.buttonHeight);
    ui::Place(GetDlgItem(window_,Limit),surface_,w-16-layout.limitWidth,layout.referenceY-pageScroll_,layout.limitWidth,layout.buttonHeight);
    ui::Place(rows_,surface_,4,layout.rowsY-pageScroll_,w-8,layout.rowsHeight);
    ui::Place(GetDlgItem(window_,CancelButton),surface_,16,layout.buttonsY-pageScroll_,layout.cancelWidth,layout.buttonHeight);
    int backX=layout.buttonRows==3?16:w-24-layout.nextWidth-layout.backWidth;
    int backY=layout.buttonsY+(layout.buttonRows>1?layout.buttonHeight+8:0);
    int nextY=layout.buttonsY+(layout.buttonRows-1)*(layout.buttonHeight+8);
    ui::Place(GetDlgItem(window_,Back),surface_,backX,backY-pageScroll_,layout.backWidth,layout.buttonHeight);
    ui::Place(GetDlgItem(window_,Next),surface_,w-16-layout.nextWidth,nextY-pageScroll_,layout.nextWidth,layout.buttonHeight);
    LayoutRows();InvalidateRect(window_,nullptr,FALSE);layoutBusy_=false;
    if(reveal)Reveal(GetFocus());
}
void CalibrationWindow::LayoutRows() {
    if(!rows_||!surface_.body)return;RECT r;GetClientRect(rows_,&r);
    int w=MulDiv(r.right,96,static_cast<int>(surface_.dpi)),h=MulDiv(r.bottom,96,static_cast<int>(surface_.dpi)),total=static_cast<int>(monitors_.size())*68;
    scroll_=std::clamp(scroll_,0,std::max(0,total-h));
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),static_cast<UINT>(h),scroll_,0};SetScrollInfo(rows_,SB_VERT,&si,TRUE);
    GetClientRect(rows_,&r);w=MulDiv(r.right,96,static_cast<int>(surface_.dpi));
    for(size_t i=0;i<monitors_.size();++i)ui::Place(GetDlgItem(rows_,SliderBase+static_cast<int>(i)),surface_,12,static_cast<int>(i)*68-scroll_+32,w-24,28);
    InvalidateRect(rows_,nullptr,FALSE);
}
void CalibrationWindow::Paint(HDC dc) {
    RECT r;GetClientRect(window_,&r);ui::Fill(dc,r,ui::colors.background);
    int w=MulDiv(r.right,96,static_cast<int>(surface_.dpi)),h=MulDiv(r.bottom,96,static_cast<int>(surface_.dpi));
    auto layout=MeasureLayout(w,h);
    auto box=[&](int x,int y,int width,int height){return RECT{surface_.S(x),surface_.S(y),surface_.S(x+width),surface_.S(y+height)};};
    SetViewportOrgEx(dc,0,-surface_.S(pageScroll_),nullptr);
    ui::Text(dc,tr(L"MATCH DISPLAYS  /  ")+std::to_wstring(monitors_.size()),box(16,10,w-32,18),surface_.caption,ui::colors.muted);
    ui::Text(dc,tr(titles[step_]),box(16,layout.titleY,w-32,layout.titleHeight),surface_.heading,ui::colors.text);
    ui::Text(dc,tr(descriptions[step_]),box(16,layout.descriptionY,w-32,layout.descriptionHeight),surface_.caption,ui::colors.muted);
    ui::Text(dc,StepText(),box(16,layout.progressY,w-32,layout.progressHeight),surface_.caption,ui::colors.accent);
    ui::Text(dc,message_,box(16,layout.statusY,w-32,layout.statusHeight),surface_.caption,ui::colors.muted);
    ui::Text(dc,tr(L"Esc cancels and restores original brightness."),box(16,layout.hintY,w-32,layout.hintHeight),surface_.caption,ui::colors.muted);
    SetViewportOrgEx(dc,0,0,nullptr);
}
void CalibrationWindow::PaintRows(HDC dc) {
    RECT r;GetClientRect(rows_,&r);ui::Fill(dc,r,ui::colors.background);int w=MulDiv(r.right,96,static_cast<int>(surface_.dpi));
    auto box=[&](int x,int y,int width,int height){return RECT{surface_.S(x),surface_.S(y),surface_.S(x+width),surface_.S(y+height)};};
    for(size_t i=0;i<monitors_.size();++i) {
        int y=static_cast<int>(i)*68-scroll_;
        ui::Fill(dc,box(12,y+66,w-24,1),failed_[i]?ui::colors.accent:ui::colors.border);
        std::wstring name=monitors_[i].name+(i==reference_?tr(L"  ·  Reference"):L"");
        ui::Text(dc,name,box(12,y+5,w-106,22),surface_.body,ui::colors.text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if(step_==6) {
            auto n=nodes_[i];std::wstring points;
            for(int p:n){if(!points.empty())points+=L" / ";points+=std::to_wstring(p);}
            ui::Text(dc,points+L"%",box(12,y+32,w-24,26),surface_.heading,ui::colors.accent);
        }else {
            ui::Text(dc,std::to_wstring(values_[i])+L"%",box(w-82,y+5,65,24),surface_.heading,ui::colors.text,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }
    }
}
void CalibrationWindow::ShowPatterns() {
    for(const auto& placement:PatternLayout(monitors_,reference_)) {
        size_t i=placement.monitor;
        auto pattern=std::make_unique<Pattern>();pattern->owner=this;pattern->index=i;pattern->edge=placement.edge;
        const auto& bounds=monitors_[i].bounds;auto r=PatternBounds(bounds,pattern->edge,surface_.S(240));
        pattern->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,L"BrightnessTray.MatchPattern",tr(L"BrightnessTray · Sample"),
            WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,window_,nullptr,GetModuleHandleW(nullptr),pattern.get());
        if(!pattern->window)continue;
        pattern->surface.Init(pattern->window);
        r=PatternBounds(bounds,pattern->edge,pattern->surface.S(240));
        SetWindowPos(pattern->window,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        patterns_.push_back(std::move(pattern));
    }
}
LRESULT CALLBACK CalibrationWindow::PatternProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto pattern=reinterpret_cast<Pattern*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){pattern=static_cast<Pattern*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);pattern->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(pattern));}
    if(!pattern)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){Buffer(hwnd,[&](HDC dc){if(pattern->surface.body)pattern->owner->PaintPattern(*pattern,dc);});return 0;}
    if(msg==WM_CLOSE){pattern->owner->Cancel();return 0;}
    if(msg==WM_DPICHANGED){pattern->surface.Init(hwnd);auto r=PatternBounds(pattern->owner->monitors_[pattern->index].bounds,pattern->edge,pattern->surface.S(240));SetWindowPos(hwnd,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void CalibrationWindow::PaintPattern(Pattern& pattern,HDC dc) {
    RECT r;GetClientRect(pattern.window,&r);ui::Fill(dc,r,RGB(80,80,80));
    int w=r.right,h=r.bottom;RECT patch=r;
    using calibration_layout::Edge;
    if(pattern.edge==Edge::Left)patch.right=w*3/4;
    if(pattern.edge==Edge::Right)patch.left=w/4;
    if(pattern.edge==Edge::Top)patch.bottom=h*3/4;
    if(pattern.edge==Edge::Bottom)patch.top=h/4;
    // Identical neutral strips reach the bezel and span the complete edge,
    // including the taskbar area. Labels stay within the sample, away from the seam.
    ui::Fill(dc,patch,RGB(230,230,230));
    RECT caption{patch.left+pattern.surface.S(12),patch.top+pattern.surface.S(16),patch.right-pattern.surface.S(12),patch.top+pattern.surface.S(42)};
    ui::Text(dc,monitors_[pattern.index].name,caption,pattern.surface.body,RGB(70,70,70),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT label{patch.left+pattern.surface.S(12),patch.bottom-pattern.surface.S(42),patch.right-pattern.surface.S(12),patch.bottom-pattern.surface.S(12)};
    std::wstring text=pattern.index==reference_?tr(L"REFERENCE"):tr(L"MATCH TO REFERENCE");
    ui::Text(dc,text,label,pattern.surface.caption,RGB(70,70,70),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
LRESULT CalibrationWindow::Message(UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CLOSE:Close(true);return 0;
    case WM_COMMAND:{
        int id=LOWORD(wp);
        if(id==CancelButton||id==IDCANCEL){Close(true);return 0;}
        bool pending=std::any_of(requests_.begin(),requests_.end(),[](auto tag){return tag!=0;});
        if(id==Reference&&step_==0&&!pending){reference_=(reference_+1)%monitors_.size();Update();}
        if(id==Limit&&!pending&&(step_==1||step_==2)){
            std::vector<size_t> all;
            for(size_t i=0;i<monitors_.size();++i){values_[i]=step_==1?100:0;SendDlgItemMessageW(rows_,SliderBase+static_cast<int>(i),TBM_SETPOS,TRUE,values_[i]);all.push_back(i);}
            Preview(all);
        }
        if(id==Next&&!pending){
            if(step_==0){
                bool known=std::all_of(known_.begin(),known_.end(),[](bool b){return b;});
                bool failed=std::any_of(failed_.begin(),failed_.end(),[](bool b){return b;});
                if(!known||failed){ReadOriginals();return 0;}
                Go(1);
            }else if(!std::any_of(failed_.begin(),failed_.end(),[](bool b){return b;})){
                if(step_==6){
                    std::vector<Calibration> curves;for(auto& n:nodes_)curves.push_back(Calibration::FromNodes(n));
                    if(save_(curves))Close(false);else{message_=tr(L"Settings could not be saved. Nothing was overwritten; please try again.");Update();}
                }else{
                    for(size_t i=0;i<nodes_.size();++i)nodes_[i][anchors[step_]]=values_[i];
                    Go(step_+1);
                }
            }
        }
        if(id==Back&&!pending&&step_>1)Go(step_-1);
        return 0;
    }
    case WM_HSCROLL:{
        int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));size_t i=static_cast<size_t>(id-SliderBase);
        if(i<monitors_.size()&&step_>0&&step_<6&&!(i==reference_&&step_>=3)){
            values_[i]=static_cast<int>(SendDlgItemMessageW(rows_,id,TBM_GETPOS,0,0));Preview({i});
        }return 0;
    }
    case WM_NOTIFY:return ui::SliderDraw(reinterpret_cast<NMHDR*>(lp),surface_);
    case WM_DRAWITEM:if(ui::DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp),surface_))return TRUE;break;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:Buffer(window_,[&](HDC dc){if(surface_.body)Paint(dc);});return 0;
    case WM_SIZE:Layout(false);return 0;
    case WM_MOUSEWHEEL:pageScroll_-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;Layout(false,false);return 0;
    case WM_VSCROLL:{
        SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(window_,SB_VERT,&si);
        switch(LOWORD(wp)){
        case SB_TOP:pageScroll_=0;break;case SB_BOTTOM:pageScroll_=pageHeight_;break;
        case SB_THUMBTRACK:pageScroll_=si.nTrackPos;break;
        case SB_LINEUP:pageScroll_-=24;break;case SB_LINEDOWN:pageScroll_+=24;break;
        case SB_PAGEUP:pageScroll_-=static_cast<int>(si.nPage);break;case SB_PAGEDOWN:pageScroll_+=static_cast<int>(si.nPage);break;
        }Layout(false,false);return 0;
    }
    case WM_GETMINMAXINFO:{
        int height=360;
        if(surface_.body&&rows_){
            RECT r,outer;GetClientRect(window_,&r);GetWindowRect(window_,&outer);
            int width=MulDiv(r.right,96,static_cast<int>(surface_.dpi));auto layout=MeasureLayout(width,0);
            int rows=std::min(3,static_cast<int>(monitors_.size()))*68;
            height=layout.desiredHeight-rows+40;
            auto work=WizardWorkArea(window_,monitors_,reference_,surface_.S(240));
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={std::min(surface_.S(360),static_cast<int>(work.right-work.left)-surface_.S(24)),std::min<int>(surface_.S(height)+outer.bottom-outer.top-r.bottom,static_cast<int>(work.bottom-work.top)-surface_.S(24))};
        }else reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={surface_.S(360),surface_.S(height)};
        return 0;
    }
    case WM_DPICHANGED:{
        surface_.Init(window_,HIWORD(wp));for(int id:{Back,Next,CancelButton,Limit,Reference})SendDlgItemMessageW(window_,id,WM_SETFONT,reinterpret_cast<WPARAM>(surface_.body),TRUE);
        for(size_t i=0;i<monitors_.size();++i)SendDlgItemMessageW(rows_,SliderBase+static_cast<int>(i),WM_SETFONT,reinterpret_cast<WPARAM>(surface_.body),TRUE);
        RECT r=*reinterpret_cast<RECT*>(lp);SetWindowPos(window_,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER);Layout();return 0;
    }
    }return DefWindowProcW(window_,msg,wp,lp);
}

