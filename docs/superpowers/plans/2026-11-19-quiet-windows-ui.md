# Quiet Windows UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the selected quiet Windows 11 appearance in the existing compact native interface.

**Architecture:** Keep Win32 windows, layout and hardware handlers. Update the shared GDI/GDI+ drawing layer and distinguish selected preferences, toggles, quiet icon actions, and primary actions.

**Tech Stack:** C++17, Win32, GDI/GDI+, PowerShell.

**Spec:** docs/design/2026-11-19-interface-review.md (user selected quiet Windows 11).

## Global Constraints

- Linked two-monitor panel 320 × 250 DIPs; Settings 320 × 348 DIPs.
- Existing 14-DIP body and 12-DIP caption fonts; no new runtime dependencies.
- Preserve confirmed brightness feedback, hardware behavior, keyboard navigation and EN/RU localization.
- Preserve the user's existing uncommitted profiling work.

## Review Focus

- Long Russian toggle captions must leave space for the switch; test measured label bounds.
- Preference selection must remain distinguishable from primary actions; inspect rasterized selected controls.
- Hover, disabled and keyboard focus must remain visible in both themes; inspect native captures and drawing states.
- Segmented controls must fit reduced settings viewports; run the existing DPI/scroll matrix.
- Calibration action wrapping and focus reveal must remain intact; run full layout regression including cloned displays.

### Task 1: Shared visual language

**Files:** src/ui.cpp, src/ui.h, src/main.cpp, tests/ui_layout_tests.cpp.

- [x] Add a raster regression proving selected preferences do not become solid primary-action blocks; run and observe failure.
- [x] Introduce neutral palettes, subtle selected surfaces and indicators, refined focus/hover/pressed states, and a lighter slider thumb.
- [x] Implement grouped preference backgrounds and a compact calibration switch with reserved caption space.
- [x] Run native appearance/layout tests in both themes and locales.

### Task 2: Main panel and documentation

**Files:** src/main.cpp, locales/en.tsv, locales/ru.tsv, docs/design, docs/media.

- [x] Group monitor rows, quiet overflow actions and hardware metadata, keep a clear shared brightness hierarchy.
- [x] Verify all existing controls, localization and layout tests; inspect actual native screenshots.
- [x] Capture native demonstrations and update README media to the implemented design.
- [x] Review the whole change and build a distributable release if verification passes.

## Execution ledger

- Implement inline in this session. The user's selected direction authorizes implementation; avoid further approval gates for routine visual decisions.
- Keep hardware handlers and persisted setting keys unchanged. Use existing labels as accessible state descriptions even when drawing a shorter toggle caption.

- Verified: selected preference raster regression failed before implementation and passed after it. Primary focus regression failed before the contrasting 2-DIP inset outline and passed after it.
- Verified: clean full build, ordinary and cloned small-screen layout suites, EN/RU, both themes, 100–200% DPI, packaging, and native image inspection. Secondary text contrasts are 5.69:1 (light) and 7.86:1 (dark).
