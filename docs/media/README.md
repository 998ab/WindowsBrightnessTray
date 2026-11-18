# README media

## Current README · v0.2.5

The README uses a small placement diagram, an illustrated tray interaction, and plain native screenshots. It does not use the earlier promotional covers or feature cards.

| Asset | Source |
| --- | --- |
| `tray-location.png` | Actual 320 × 250 native panel placed above a schematic notification area. Panel alignment follows the production bottom-taskbar placement rule: right-aligned to the icon, with a 10-DIP gap above the taskbar. |
| `tray-wheel.gif` | Illustrated pointer over the production-rendered sun icon. Positive and negative wheel deltas are replayed through the production tray-wheel message handler. Native demo acknowledgements provide the 50% → 70% → 50% values. The final click and panel reveal are illustrated. |
| `tray-wheel-still.png` | Static reduced-motion alternative to the tray interaction. |
| `settings-native.png` | Unscaled native English Settings window. |
| `calibration-native.png` | Native midpoint calibration page. |

The desktop, taskbar, pointer, scroll arrows, and explanatory percentage annotation are drawn for the diagrams. They are **not recordings of the Windows shell or its low-level input hook**. The percentage label is an explanation, not an on-screen display supplied by the app. The panel and icon are rendered by the actual application code; they are not AI-generated or recreated controls.

A native capture harness hosts the production windows, rendering, event handlers, and demo monitor worker. It records native panel frames, native tray-icon frames, and acknowledged demo brightness values. The illustrated sequence runs at 15 frames per second for 7 seconds: hover, wheel up, wheel down, then a panel reveal. The GIF uses a fixed palette and transparent unchanged pixels. No physical monitor brightness is changed.

The schematic is for a visible icon on a bottom taskbar. Windows can instead place the icon in its hidden-icons menu; the real app also supports that anchor and other taskbar positions. Actual OS themes and tray contents vary.

Calibration values are illustrative. The screenshot includes the wizard; comparison strips appear on display edges outside that window. Interface demonstrations do not measure physical luminance, device latency, or power use.

## Earlier assets

The previous `hero.png`, `wheel.gif`, `brightness.gif`, `calibration-v2.gif`, `settings-v2.png`, `themes-v2.png`, and accompanying stills are retained as earlier presentation material. They are not used by the current main README.

The historical `tray-scroll.gif` used an older installed application and real monitors. Its animated mouse and direction labels were editorial annotations; it also shows an older icon and brightness overlay, so it is not used as an example of the current application.
