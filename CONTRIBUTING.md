# Contributing to BrightnessTray

Bug reports, code, translations, and documentation improvements are welcome.

## Report a bug

[Open an issue](https://github.com/998ab/WindowsBrightnessTray/issues/new) with:

- The BrightnessTray and Windows versions.
- Steps to reproduce, what happened, and what you expected.
- For hardware issues: monitor models, connection type, and whether DDC/CI is enabled.
- A screenshot or short recording when it helps explain an interface problem.

For a new feature, describe the use case in an issue before starting a large change.

## Build and test

Install Visual Studio 2022 C++ Build Tools and the Windows SDK. From the repository root:

```powershell
./build.ps1 -OutputDirectory build/contribution
./build/contribution/BrightnessTray.exe --demo
```

The build script runs the automated tests. Demo mode uses virtual displays and does not change physical monitor brightness. Native UI tests need a Windows desktop session.

See [development and packaging](docs/development.md) and [calibration behavior](docs/calibration.md) for more detail. Physical DDC/CI behavior varies; when you test real monitors, include the models and connection types in your pull request.

## Send a pull request

1. Fork the repository and create a branch for your change.
2. Keep the change focused and follow the surrounding C++/Win32 style.
3. For a bug fix, include a regression test that reproduces the original failure when practical.
4. Run `build.ps1` and describe the checks you performed. Include before/after screenshots for visual changes.
5. Open a pull request explaining the problem and the resulting behavior. Link the issue if there is one.

English UI strings live in source and `locales/en.tsv`; Russian translations are in `locales/ru.tsv`. Keep the catalogs in sync. The build checks catalog consistency.

## License

BrightnessTray is distributed under the [MIT License](LICENSE). Contributions are accepted under the same license. Preserve applicable copyright and license notices.
