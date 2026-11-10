# Native popup preview

Implementation of the research/interface concept in the existing C++17/Win32 application. No WebView or browser runtime is added.

- 352 DIP tool-window popup anchored to the real notification icon and clamped to the monitor work area; no normal taskbar button. Outside activation loss, Escape and repeated tray click dismiss it.
- Compact chain/settings/back glyphs; settings are a popup page. The existing separate calibration window hides the popup and restores it on completion/cancellation.
- One shared slider when linked. Independent sliders and per-display options when unlinked. Disabled DDC/CI devices are excluded from writes; scan/disconnect/resume and existing error reporting remain in place.
- Thumb is the requested level; track uses confirmed worker events, including intermediate writes. Pending distance has a subdued blue fill. Failed writes restore the affected thumb to the confirmed level. User targets survive language and DPI rebuilds.
- Existing wheel/Ctrl handling, response preferences, presets, active monitor selection, calibration persistence/reset and tray exit remain available.
- Native owner-drawn language selector with Russian/English autonyms, no persistent focus rectangle and a hand cursor over entries. Selection is persisted; opening the list does not dismiss the popup. Escape first dismisses a dropped list.
- Procedural sun in notification area and popup, no frame cache. Timer stops on idle; temporary icons and GDI resources are released.

## Validation

`./build.ps1 -OutputDirectory build/popup-preview` succeeded, including localization, brightness curves/transitions, calibration layout, worker concurrency/fades, tray hook/icon resources, display names, and the new popup native demo smoke test. The smoke test also passed with `--dark`; light and dark native captures were inspected.

`tests/popup_smoke.cpp` launches an actual demo executable with a separate test INI. It verifies styles/taskbar exclusion, shared/independent mode, confirmed brightness progress, settings/languages, separate calibration, popup hiding/restoration, and deactivation dismissal. It is included in CMake/CTest and build.ps1. Demo-only `--settings=<path>` allows profile-isolated tests.

`build/review/` and `build/BrightnessTray-review.zip` contain review executables. No installation, tag, push or release publication was performed. The existing application version stays 0.1.0 until acceptance.

## Review boundaries

Automated UI tests use demo DDC responses, not the user's physical panels. Real driver latency, Explorer restart, taskbar placements and per-monitor DPI changes should be checked on the target machine before release. The native calibration wizard retains the existing production workflow and screen-edge patterns rather than the illustrative single-screen HTML samples.

## Follow-up manual inspection, 2026-11-11

The first review build was handed off too early. Desktop interaction found focus rectangles on sliders and the native language combo, an oversized legacy calibration wizard, and an activation/tray-click ordering flaw in popup dismissal.

The replacement build removes the slider rectangle, uses a borderless language button with a native selection menu, aligns the per-display percentage/DDC/options, and reduces calibration to 440 DIP with reflowed content. The tray handler suppresses reopening immediately after deactivation over the notification icon; closing a popup menu defers an active-window check. Smoke tests use a profile-derived mutex so a user-running demo cannot cause the test to inspect or signal the wrong instance. Tests also check compact calibration bounds and capture its first working step.

Desktop checks used Computer Use to drag the actual native shared slider, observe confirmed brightness settling, change languages, open calibration, start/cancel a step, switch away with Alt+Tab and reopen the panel. Since the automation inventory omits tool windows, a diagnostic executable named BrightnessTray.Inspection.exe exposes the same UI as an app window. This diagnostic variation is not packaged in the review archive and cannot be used as proof of real tray placement or taskbar exclusion; the regular native smoke test covers the window-style checks separately.

Final build/tests passed; native dark calibration capture was inspected after reflow. Distribution: build/review-v2 and build/BrightnessTray-review-v2.zip. No release published. Physical DDC response remains to be reviewed on the user's devices.

## Rendering quality, 2026-11-11

Rounded controls and glyphs previously used non-antialiased GDI RoundRect/LineTo/Polygon. They now use the Windows GDI+ rasterizer with antialiasing and fractional coordinates. Text stays on the existing DPI-aware ClearType path; the whole window is not raster-scaled. The popup sun is drawn directly as vector geometry rather than stretching an HICON. Notification icons are generated at the current DPI size and regenerated when that size changes, without an animation frame cache.

Tests assert intermediate edge coverage on rounded geometry and render 1000 icons across 16/20/24/32 px while checking GDI/USER resource counts. The OS-provided GDI+ DLL is used; no bundled framework or bitmap sprite sheet is added.

### Tray optical sizing
The tray sun now uses a separate, heavier optical geometry for its 16-DIP canvas, with an explicit transparent safety margin. A regression checks all eight rays at 16 angles for 16/20/24/32-pixel icons and asserts transparent boundary pixels to catch clipping. The popup's larger vector geometry is kept separate. Review v4 replaces v3.

### Wheel feedback
The standalone brightness OSD has been removed from wheel/preset adjustments. Confirmed brightness is still reflected in the panel and the notification icon tooltip. The native smoke test verifies a wheel adjustment changes the target and creates no BrightnessTray.OSD window.

### Hidden notification icons
Popup positioning resolves the taskbar on the icon's monitor. A visible pinned icon remains its own anchor; an icon outside taskbar bounds is treated as overflow and projected to the notification area. Placement uses the taskbar edge rather than the overflow row's vertical coordinate. An opening captures the anchor so subsequent settings/size changes do not jump when the shell overflow flyout disappears. Geometry regression covers pinned/overflow, top/bottom/left/right bars, negative monitor origins and constrained work areas. Review v6 supersedes v5.

### Overflow follow-up (v7)
Desktop read-only diagnostics showed Shell_NotifyIconGetRect returning 1615,1032..1647,1080 (the chevron area) for a hidden icon, while the overflow window occupied 1514,918..1748,1032. Thus v6's icon-only classification was insufficient, and edge-alignment still allowed overlap with an open overflow flyout. Resolve now separately detects the visible Windows 10/11 overflow root using pointer/icon hit testing. Placement excludes its complete bounds, with an exact-geometry regression asserting no intersection. This corrects the earlier v6 description; actual shell interaction still needs the user's review.

### Prototype motion (v8)
SunMotion replaces the DDC-busy/pose-alignment loop with the prototype's input-driven timing: 110 deg/s, a 650ms retriggerable pulse and a 280ms linear velocity fade. Angle is integrated analytically to avoid timer-frequency-dependent stopping. It is preserved at rest and on reversal, with no 45/120-degree snapping. Hover uses the prototype's 280ms fade-only pulse. Windows client-area animation settings are respected; the timer stops at rest. Regression tests exercise the production timing class for cruise speed, fade distance, retriggering, reversal, stable idle phase and hover.

### Frame pacing (v9)
Optional --perf writes timing-only render-perf.csv beside the executable. A comparable native demo smoke run measured mean UI animation message intervals of 25.69 ms before the change and 7.17 ms afterward (51 / 182 samples); mean paint duration was 0.81 / 0.45 ms. These are message/paint measurements, not a DWM presentation FPS trace.

WM_TIMER and coarse animation timestamps were replaced by a QPC-based clock and an active-only high-resolution waitable timer. FrameClock selects the current monitor refresh frequency, never restarts the schedule on retrigger, coalesces queued frame messages, and never draws outside the UI thread. It waits without periodic wakeups when inactive. Main/list/trackbar back buffers are reused and released deterministically; sun-only repaints are clipped and skip other content. Tests exercise continuous retriggers without starvation, one outstanding frame, idle cancellation, buffer resource balance, and all prior behavior. DDC worker cadence (40 ms plus device latency) and confirmed brightness semantics are unchanged.

### Confirmed line interpolation (v10)
The v9 frame-cadence fix did not smooth discrete acknowledgement-driven progress. ConfirmedMotion now interpolates the displayed value over 40ms between acknowledgements using fractional levels and subpixel GDI+ coordinates. It never targets unacknowledged input or sends extra DDC writes. This deliberately adds up to 40ms of visual lag; numeric confirmed values remain direct acknowledgements. Instant response and Windows reduced-motion settings bypass interpolation.

The shared frame clock remains active while a visible progress transition is running, even after the sun pulse ends, and stops when both are idle. Blue fill ends at the displayed confirmed level on both rising and falling changes; it no longer clips immediately to a lowered target thumb. Tests cover fractionality, bounded reversal, duplicate responses, completion, snapping, and an actual native-control bitmap assertion that declining confirmed fill extends beyond the lowered target. Desktop checks exercised rising and falling drags in the diagnostic native window. Review v10 is packaged separately; review-current is the stable launch location for future updates.

### Acknowledgement playback (v11)
The v10 retargeted 40ms transitions could finish before the next response and changed slope at each retarget. ConfirmedMotion now plays a bounded 16-sample timestamped acknowledgement history with an 80ms delay, using linear segments without restarting each segment from the last rendered position. A regular stream retains constant velocity across sample joins. Reversal discards queued old-direction motion while preserving the current displayed position; after idle, a hold sample prevents a jump. Equal responses do not restart playback. There is no extrapolation beyond acknowledged values. Instant/reduced-motion modes snap and reset history.

Continuous inverse calibration removes a second integer rounding stage from the visual line; target controls and confirmed numeric labels retain their integer semantics. Device quantization and irregular physical responses can still affect the acknowledged trajectory. Tests verify per-millisecond velocity through 40 regular responses (including bounded-history eviction), idle/reversal/completion, fractional calibration segments, and all native control smoke checks. Review v11 supersedes v10.

### Review corrections (v12)
Independent review reproduced v11's recurring 45ms holds with regular 125ms DDC replies. ConfirmedMotion now follows each acknowledgement over the measured interval between replies. An explicit Begin marks a newly submitted regulation when no write is pending for that monitor; time between separate user requests is excluded. Continuous retargeting does not reset that timing. This replaces the v11 fixed-delay history. New segments start at the current displayed value, retain fractional values and target only acknowledged values. Snapshot/reduced-motion updates reset measurement. Row rebuilds reset the shared line; calibration updates snap while the main popup is hidden.

Deterministic tests check every millisecond through 40 regular responses at 40/125/250/1000ms, a cadence change, a 60-second idle with explicit new request, reversals, duplicates and reset. As with any acknowledgement-only display, the first unknown interval or irregular physical responses cannot guarantee uniform motion without prediction.

The native smoke test now enumerates windows by its own child PID, including calibration and OSD windows, and re-resolves ownership for cleanup. Cleanup waits for the child and only terminates its own process handle on timeout. A same-class/same-caption decoy in the parent process must remain untouched; both normal and injected-failure cleanup paths passed. CTest runs the two GUI tests serially. The complete build/test suite passed; the smoke executable was rebuilt after adding its failure-path regression and both variants passed again. Independent reviewer confirmed both original findings closed, with no further proven defects in the corrections. No real device/DWM presentation trace was performed for this change.

### UI review corrections (v13)
Settings controls now live inside a clipped, vertically scrollable child pane beneath the fixed header. Content retains its 380-DIP extent; the viewport fits the actual popup height, and wheel/scrollbar movement reaches language and refresh on short work areas. Control lookup, command/owner-draw routing, DPI fonts, invalidation and buffered painting include the new pane. Reparented controls are explicitly placed in logical Tab order.

FocusReveal scrolls monitor and settings controls into view on WM_SETFOCUS, including dialog Tab navigation. The language owner-draw branch displays an accent underline only when keyboard focus cues are visible. Partial wheel deltas accumulate independently in each pane.

A native UI regression executable includes the production handlers and control construction without invoking the app entry point, worker, tray hook or hardware I/O. It checks actual IsDialogMessageW Tab transitions to a fourth monitor and to language, a 312-DIP popup, backward focus movement, settings scrollbar, four partial wheel events, expanded viewport, and pixels for visible/hidden language focus cues. It writes native-short-language-focus.bmp, native-short-refresh.bmp and native-four-monitor-focus.bmp. UI reviewer inspected these images, including the final localized versions. The strengthened Tab-to-language test exposed reparented child ordering, corrected with explicit HWND_BOTTOM placement. Afterward the UI test passed, the final app was rebuilt, and normal plus injected-failure smoke checks passed. Both the code reviewer and separate UI reviewer approved the final corrections with no further proven findings. Physical DPI changes and real DDC/Shell behavior were not newly exercised.
