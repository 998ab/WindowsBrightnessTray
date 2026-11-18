#pragma once
#include "monitors.h"
#include "ui.h"
#include "calibration_layout.h"
#include <array>
#include <functional>
#include <memory>

inline unsigned long long NextTag() { static unsigned long long tag=0;return ++tag; }
class CalibrationWindow {
public:
    void Open(HWND owner,const std::vector<Monitor>& monitors,size_t reference,MonitorWorker& worker,
        std::function<bool(const std::vector<Calibration>&)> save);
    void OnEvent(const Event& event);
    void Cancel();
    std::vector<Monitor> OriginalLevels() const;
    HWND Handle() const { return window_; }
private:
    struct LayoutInfo {
        int titleY=34,titleHeight=0,descriptionY=0,descriptionHeight=0,referenceY=0;
        int progressY=0,progressHeight=0,rowsY=0,rowsHeight=0,statusY=0,statusHeight=0;
        int hintY=0,hintHeight=0,buttonsY=0,buttonHeight=28,desiredHeight=0;
        int cancelWidth=72,backWidth=64,nextWidth=88,limitWidth=96;
        int buttonRows=1;
    };
    LayoutInfo MeasureLayout(int width,int height) const;
    std::wstring StepText() const;
    struct Pattern {
        HWND window{};
        ui::Surface surface;
        CalibrationWindow* owner{};
        size_t index{};
        calibration_layout::Edge edge=calibration_layout::Edge::Left;
    };
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK RowsProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK RevealRow(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK PatternProc(HWND,UINT,WPARAM,LPARAM);
    LRESULT Message(UINT,WPARAM,LPARAM);
    void Layout(bool fit=true,bool reveal=true);
    void LayoutRows();
    void Reveal(HWND child);
    void Paint(HDC);
    void PaintRows(HDC);
    void Update();
    void ReadOriginals();
    void Preview(const std::vector<size_t>& indices);
    void Go(int step);
    void Normalize();
    void Close(bool restore);
    void ShowPatterns();
    void PaintPattern(Pattern&,HDC);
    HWND window_{},owner_{},rows_{};
    ui::Surface surface_;
    std::vector<Monitor> monitors_;
    std::vector<std::array<int,5>> nodes_;
    std::vector<DWORD> originals_;
    std::vector<int> values_;
    std::vector<unsigned long long> requests_;
    std::vector<bool> known_,failed_;
    std::vector<std::unique_ptr<Pattern>> patterns_;
    MonitorWorker* worker_{};
    std::function<bool(const std::vector<Calibration>&)> save_;
    bool touched_=false,layoutBusy_=false,revealing_=false;
    size_t reference_=0;
    int step_=0,scroll_=0,pageScroll_=0,pageHeight_=0;
    std::wstring message_;
};

