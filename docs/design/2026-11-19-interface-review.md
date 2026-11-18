# Compact interface: visual review and proposed direction

Status: selected by the user; implementation follows the quiet Windows 11 direction.

## Intent

Make BrightnessTray feel like a carefully finished Windows utility. Keep the main linked two-monitor panel at 320 × 250 DIPs and Settings at 320 × 348 DIPs. Preserve keyboard navigation, localization, DPI adaptation, confirmed brightness feedback, and current hardware behavior.

## Findings

| Priority | Observed issue | Proposed correction |
| --- | --- | --- |
| High | Selected preferences and primary actions share the same large blue treatment. The calibration preference dominates Settings. | Distinguish selection from action. Use a subtle selected segment and a compact calibration switch; reserve solid accent buttons for the wizard's next/save action. |
| High | Settings reads as a collection of independently bordered rectangles. | Use continuous segmented groups for response and wheel step, consistent control heights and aligned section boundaries. |
| Medium | The blue-tinted dark surface, bright metadata, repeated percentages, and row dividers compete for attention. | Use neutral dark surfaces, quieter metadata, restrained separators, and one clear shared brightness value. Keep hardware state available in monitor rows and menus. |
| Medium | The double-ring slider thumb looks heavy relative to the thin track and small window. | Use a clean 16-DIP thumb with a subtle border and a 4-DIP track. Retain a separate visible keyboard focus ring and confirmed-progress feedback. |
| Medium | Overflow controls look like tiny blank rectangular buttons, while language selection lacks a strong dropdown affordance. | Render an explicit three-dot glyph with a consistent quiet hover surface; keep a visible language chevron. Preserve existing hit areas and keyboard labels. |
| Medium | Button focus uses an inset dotted rectangle that conflicts with rounded controls. | Use a consistent accent outline only during keyboard navigation. Preserve distinguishable focus on every interactive element. |

## Approaches

1. **Quiet Windows 11 — proposed.** Neutral surfaces, one blue accent, grouped selections, restrained borders. Fits the existing native C++ renderer and compact dimensions.
2. **Flat minimal.** Remove most surfaces and borders. Visually lighter, but selections and secondary actions become less obvious.
3. **Expressive blue.** Stronger accent surfaces and contrast. More distinctive, but less suited to a small background utility.

The user selected quiet Windows 11.

## Proposed visual system

- Prefer Segoe UI Variable Text with Segoe UI as fallback, retaining the existing 14-DIP body / 12-DIP caption sizes; retain readable hierarchy without reducing fonts.
- Dark: background `#202020`, raised surface `#292929`, text `#F3F3F3`, secondary text `#B4B4B4`, border `#3A3A3A`, accent `#80BFFF`.
- Light: background `#F7F7F7`, raised surface `#FFFFFF`, text `#202020`, secondary text `#626262`, border `#DEDEDE`, accent `#0067C0`.
- Use consistent 4/8/16-DIP spacing. Outer window rounding follows the OS; controls use a restrained 6-DIP radius.
- Preference selection uses a raised neutral surface, a short accent indicator, and explicit checked state. The accent does not fill the entire preference row.
- Keep actual monitor and target values distinct while reducing the visual weight of hardware metadata.
- Maintain hover, press, disabled, error, and keyboard-focus states. Selection is communicated through shape/indicator as well as color.
- Calibration uses the same surfaces and controls; retain measured text layout, responsive footer, and scrolling for small work areas.

## Scope and verification

Modify the existing native renderer and drawing handlers. Keep dependencies, hardware control, settings persistence, and window dimensions. This is a visual revision, not a migration to a web UI framework.

Verify actual native captures in English/Russian, light/dark, and 100–200% DPI. Reuse the full layout and hardware regression suite, including cloned and stacked displays. Inspect hover/focus/disabled states and check palette contrast. Refresh README media only after the native result is visually accepted.

The accompanying HTML and PNG are design concepts, not screenshots of implemented controls. The illustrations omit detailed error and wizard pages; those inherit the proposed design system during implementation.
