# Development

## Build and test

Install Visual Studio 2022 C++ Build Tools with the Windows SDK, then run from the repository root:

```powershell
./build.ps1 -OutputDirectory build/release
```

The build uses the static C++ runtime. Tests cover localization, calibration curves and geometry, worker behavior, tray rendering, display names, native popup behavior, failure cleanup, and UI focus/scrolling. Test instances use virtual monitors and do not change physical brightness.

CMake supports MSVC Release builds and CTest. The application version is defined in `src/version.h`.

## Package

Install Inno Setup 6 or 7:

```powershell
./scripts/package.ps1 -BuildDirectory build/release -InnoCompiler 'C:/Program Files/Inno Setup 7/ISCC.exe'
./scripts/test-installer.ps1
```

Exit the normal application before the installer test. It installs into `build/installer-smoke`, verifies files, uninstalls them, and checks that the existing profile was preserved. Artifacts and SHA-256 checksums go to `build/dist`.

GitHub Actions tests pushes and pull requests. A version tag, manual publish run, or main-branch commit containing `[release]` publishes after build, packaging and installer checks pass. Add release notes in `docs/releases/v<version>.md`. See [the workflow](../.github/workflows/build.yml).

## Launch options

| Option | Purpose |
| --- | --- |
| `--demo` | Two virtual monitors; no physical brightness changes. |
| `--lang=en`, `--lang=ru` | Override the language for one launch. |
| `--light`, `--dark` | Override the application theme. |
| `--trace` | Write brightness command timing to `brightness-trace.csv` beside the executable; limited to 20,000 rows. |
| `--perf` | Write render timing to `render-perf.csv` beside the executable. |

Naming the executable `BrightnessTray.Demo.exe` also enables demo mode. Its profile is `%LOCALAPPDATA%\BrightnessTray\settings-demo.ini`; the normal app uses `settings.ini` in the same directory.

## Assets and translations

Run `scripts/package-icon.ps1` to regenerate the ICO from the original PNG. The current README screenshot is a native demo capture of the 0.2.0 interface; older recordings remain in `docs/media` as historical assets.

English strings are maintained in source and `locales/en.tsv`; Russian translations are in `locales/ru.tsv`. Installer translations are in `locales/installer-ru.iss`. Catalogs are embedded and validated on each build. Documentation and release notes are in English.

The interactive design prototype and its tests are in [research/interface](../research/interface/README.md). Native UI implementation and review notes are in [popup-preview.md](popup-preview.md).
