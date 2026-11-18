# README media

## Current gallery · v0.2.4

The main README uses fresh captures of BrightnessTray's native Win32 interface in demo mode. Virtual monitors are used throughout; no physical monitor brightness was changed for these recordings.

| Asset | Content |
| --- | --- |
| `hero.png` | Current linked-monitor panel on the README cover. |
| `wheel.gif` | Actual wheel input over the shared slider: 50% → 70% → 50%, including the native sun animation. The tray uses the same brightness direction. |
| `brightness.gif` | Actual shared-slider changes: 50% → 80% → 25% → 50%. The thumb and confirmed fill are captured from the app. |
| `calibration-v2.gif` | All seven wizard pages, from introduction through five-point review. Values are illustrative, not measured calibration results. |
| `settings-v2.png` | Current English settings, including the saved-calibration switch. |
| `themes-v2.png` | Current dark and light native panels. |
| `wheel-still.png`, `brightness-still.png`, `calibration-still.png` | Static alternatives selected by the README when the browser requests reduced motion. |

A native capture harness hosts the production windows, rendering, event handlers, and demo monitor worker in an isolated process. Screenshots exclude other desktop content. Calibration captures include only the wizard; the actual full-height comparison strips appear on the demo display edges outside that window.

Presentation backgrounds, captions, shadows, and layout are added around the native captures. Controls and reported brightness are not redrawn or generated. Long idle gaps are shortened; actual transitions are retained. GIFs loop and use fixed palettes; the two motion recordings also omit unchanged pixels for compact files. These are interface demonstrations, not measurements of device latency, backlight luminance, or power use.

## Earlier captures

The older `tray-scroll.gif`, `synchronization.gif`, `calibration-walkthrough.gif`, `themes.png`, `languages.png`, `calibration.png`, and their stills are retained as historical material. They show earlier interfaces and are not used for the current README gallery.

The historical tray-scroll recording used the installed application and real monitors. Its animated mouse and direction labels were editorial annotations, rather than captured wheel events.
