# Visual brightness matching

## Purpose

BrightnessTray matches perceived backlight brightness across external displays. Equal hardware percentages do not imply equal luminance. Near-black/white clipping-symbol tests do not establish correspondence between monitors, so this wizard does not use them.

This is an approximation for the selected display modes, viewing position, and lighting. It does not measure cd/m², characterize gamma, install an ICC profile, or replace a colorimeter.

## Five-point workflow

1. Choose a reference. Samples use neutral RGB 230 on an RGB 80 surround.
2. Match a shared maximum. The dimmest display at its achievable maximum limits the others. **All to 100%** is optional.
3. Match a comfortable shared minimum. The brightest minimum limits the others. Hardware 0% is not zero light; entering this step does not automatically set every display to 0%.
4. Hold the reference at its midpoint and match the other displays.
5. Match the 25% and 75% points with the reference fixed.
6. Review five nodes and save the group. Displays remain at the matched midpoint.

Five nodes are an engineering choice, not a standard-mandated procedure. Each display gets a monotonic piecewise-linear mapping from shared position to hardware setting, not a measured luminance function. Slider limits enforce strict ordering.

## Placement

Windows monitor rectangles determine facing edges. Side-by-side displays get full-height strips; stacked displays get full-width strips. Light reaches the bezel without a border, including the taskbar area. Negative coordinates, offsets, portrait orientation, and gaps are supported.

A nearest-neighbor spanning layout connects multi-monitor arrangements. A middle display can have strips on both sides. Windows does not report physical bezel widths or the desk layout; configure its arrangement to match the desk. Controls stay away from reference strips.

## Persistence and scheduling

Wizard changes are direct parallel writes without animation or a step cap. **Next** waits for acknowledgements. Original raw settings are read before the first write. **Cancel** restores exact values without percentage round-trips.

Profiles change only on Save. Curves are written to a temporary settings file and replaced as a group. Three-point profiles remain compatible. Historical keys are preserved independently of friendly labels.

Each display has a persistent sleeping worker. A common gate starts grouped writes; calls to one physical handle remain sequential. Pending targets coalesce without crossing read or topology barriers. Normal transitions preserve fractional position and velocity, then round at the hardware boundary.

Parallel calls do not make DDC/CI atomic across displays. Drivers may serialize the bus and firmware may respond at different times. A captured two-display setup returned writes in approximately 62–125 ms. Simultaneous light changes and continuous animation are not guaranteed.

## Conditions and limitations

Warm up displays and use stable familiar lighting and intended SDR modes. Compare samples rather than percentages. The app does not change HDR, dynamic contrast, power modes, GPU gamma, or color settings.

White-point differences, gamma, viewing angles, ABL, and local dimming may prevent matching on other content. Full-height strips favor proximity at the seam; ABL/local-dimming displays may react differently than to small patches. Matching is approximate between anchors and may need repeating for another mode.

## References

- [DisplayCAL: adjusting a display](https://displaycal.net/#adjusting-a-display) distinguishes white-level backlight adjustment from clipping controls.
- [DisplayCAL settings](https://displaycal.net/#calibration-settings) discusses comparison conditions.
- [ColourSpace perceptual matching](https://lightillusion.com/perceptual_match_guide.html) discusses identical samples and ABL/local-dimming effects. Its full procedure includes measurements and color adjustments not performed here.
- [Microsoft: SetMonitorBrightness](https://learn.microsoft.com/en-us/windows/win32/api/highlevelmonitorconfigurationapi/nf-highlevelmonitorconfigurationapi-setmonitorbrightness) documents the call and typical latency.
