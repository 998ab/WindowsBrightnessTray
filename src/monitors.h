#pragma once
#include <windows.h>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "brightness.h"

struct Monitor {
    HANDLE handle{}; // Owned exclusively by MonitorWorker, never used by the UI.
    std::wstring name, key;
    RECT bounds{};
    DWORD minimum{}, maximum{}, current{};
    bool supported{}, lowLevel{};
    Calibration calibration;
};
enum class Operation { Scan, Read, Write };
struct Command {
    Operation operation;
    std::wstring key;
    DWORD raw = 0;
    unsigned long long tag = 0;
    bool smooth = false;
    // >=0 uses the calibration curve for intermediate frames; -1 fades in raw
    // hardware space (calibration previews). Plain writes cancel that fade.
    int logical = -1;
    Calibration calibration;
};
struct Event {
    Operation operation;
    std::wstring key;
    DWORD raw = 0;
    unsigned long long tag = 0;
    bool ok = false;
    std::vector<Monitor> monitors;
    LONGLONG started = 0, finished = 0; // QPC timestamps, useful for backend diagnostics.
    bool final = true;
};
class MonitorWorker {
public:
    void Start(HWND target, UINT message, bool demo = false, unsigned demoDelayMs = 0);
    void Queue(Command command);
    void QueueBatch(std::vector<Command> commands);
    void SetTransitionDuration(int milliseconds);
    void SetHardwareStep(int percent);
    std::deque<Event> Events();
    void Stop();
    ~MonitorWorker() { Stop(); }
private:
    void Run();
    HWND target_{};
    UINT message_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::vector<Command>> commands_;
    std::deque<Event> events_;
    bool stopping_ = false;
    bool demo_ = false;
    unsigned demoDelayMs_ = 0;
    std::atomic<int> transitionMs_{-1}; // -1 retains the original bounded mode for legacy callers.
    std::atomic<int> hardwareStep_{0};
    std::thread thread_;
};
