param([string]$Installer)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if (!$Installer) { $Installer=(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'build/dist') -Filter '*-Setup-x64.exe' | Select-Object -First 1).FullName }
if (!$Installer) { throw 'No installer was found.' }
$target=[IO.Path]::GetFullPath((Join-Path $projectRoot 'build/installer-smoke'))
$allowed=[IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))+[IO.Path]::DirectorySeparatorChar
if (!$target.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Test directory must stay inside build.' }
if (Test-Path -LiteralPath $target) { throw 'Installer test directory already exists; inspect it before rerunning.' }
$profile=Join-Path $env:LOCALAPPDATA 'BrightnessTray/settings.ini'
$profileBefore=if (Test-Path -LiteralPath $profile) {(Get-FileHash -LiteralPath $profile).Hash}else {$null}
$arguments=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/LANG=en','/TASKS=',('/DIR="'+$target+'"'),('/LOG="'+(Join-Path $projectRoot 'build/install-smoke.log')+'"'))
$process=Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Installer exited with $($process.ExitCode)." }
foreach ($name in @('BrightnessTray.exe','LICENSE','README.md')) { if (!(Test-Path -LiteralPath (Join-Path $target $name))) { throw "Missing installed file: $name" } }
$uninstaller=Get-ChildItem -LiteralPath $target -Filter 'unins*.exe' | Select-Object -First 1
if (!$uninstaller) { throw 'Uninstaller was not created.' }
$process=Start-Process -FilePath $uninstaller.FullName -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0 -or (Test-Path -LiteralPath (Join-Path $target 'BrightnessTray.exe'))) { throw 'Uninstall verification failed.' }
$profileAfter=if (Test-Path -LiteralPath $profile) {(Get-FileHash -LiteralPath $profile).Hash}else {$null}
if ($profileAfter -ne $profileBefore) { throw 'Installer or uninstaller changed the existing user profile.' }
'PASS: per-user install, required files, uninstall, and existing profile preservation'
