param([string]$OutputDirectory='build')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio C++ Build Tools with Windows SDK.' }
$dev = Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1'
& $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
    & rc.exe /nologo /I assets /fo "$OutputDirectory/app.res" src/app.rc
    if ($LASTEXITCODE) { throw 'Resource build failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX src/main.cpp src/display_names.cpp src/monitors.cpp src/ui.cpp src/calibration.cpp src/tray.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/BrightnessTray.exe" /link /SUBSYSTEM:WINDOWS "$OutputDirectory/app.res" user32.lib gdi32.lib shell32.lib comctl32.lib dxva2.lib dwmapi.lib uxtheme.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Build failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX tests/localization_tests.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/localization_tests.exe" /link "$OutputDirectory/app.res"
    if ($LASTEXITCODE) { throw 'Localization test build failed' }
    & ".\$OutputDirectory\localization_tests.exe"
    if ($LASTEXITCODE) { throw 'Localization tests failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc tests/brightness_tests.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/brightness_tests.exe"
    if ($LASTEXITCODE) { throw 'Test build failed' }
    & ".\$OutputDirectory\brightness_tests.exe"
    if ($LASTEXITCODE) { throw 'Tests failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc tests/calibration_layout_tests.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/calibration_layout_tests.exe"
    if ($LASTEXITCODE) { throw 'Calibration layout test build failed' }
    & ".\$OutputDirectory\calibration_layout_tests.exe"
    if ($LASTEXITCODE) { throw 'Calibration layout tests failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX tests/worker_tests.cpp src/monitors.cpp src/display_names.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/worker_tests.exe" /link user32.lib gdi32.lib dxva2.lib
    if ($LASTEXITCODE) { throw 'Worker test build failed' }
    & ".\$OutputDirectory\worker_tests.exe"
    if ($LASTEXITCODE) { throw 'Worker tests failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX tests/tray_tests.cpp src/tray.cpp src/ui.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/tray_tests.exe" /link user32.lib gdi32.lib shell32.lib comctl32.lib dwmapi.lib uxtheme.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Tray test build failed' }
    & ".\$OutputDirectory\tray_tests.exe"
    if ($LASTEXITCODE) { throw 'Tray tests failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX tests/display_names_tests.cpp src/display_names.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/display_names_tests.exe" /link user32.lib
    if ($LASTEXITCODE) { throw 'Name test build failed' }
    & ".\$OutputDirectory\display_names_tests.exe"
    if ($LASTEXITCODE) { throw 'Name tests failed' }
    & cl.exe /nologo /std:c++17 /O2 /MT /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX tests/display_names_probe.cpp src/display_names.cpp "/Fo:$OutputDirectory/" "/Fe:$OutputDirectory/display_names_probe.exe" /link user32.lib
    if ($LASTEXITCODE) { throw 'Name probe build failed' }
} finally { Pop-Location }

