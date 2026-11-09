param(
    [string]$BuildDirectory='build/release',
    [string]$OutputDirectory='build/dist',
    [string]$InnoCompiler
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$versionText=Get-Content -LiteralPath (Join-Path $projectRoot 'src/version.h') -Raw
$version=[regex]::Match($versionText,'#define BRIGHTNESSTRAY_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"').Groups[1].Value
if (!$version) { throw 'Could not read the application version.' }
$buildPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
$executable=Join-Path $buildPath 'BrightnessTray.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Build BrightnessTray before packaging.' }
if ((Get-Item -LiteralPath $executable).VersionInfo.ProductVersion -ne $version) { throw 'Executable version does not match src/version.h.' }
if (!$InnoCompiler) {
    $compilerCandidates=@(
        (Join-Path $projectRoot 'build/tools/InnoSetup/ISCC.exe'),
        'C:\Program Files\Inno Setup 7\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
    )
    $InnoCompiler=$compilerCandidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
}
if (!$InnoCompiler) { throw 'Install Inno Setup 6/7 or supply -InnoCompiler.' }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$portable=Join-Path $outputPath "BrightnessTray-$version-Portable-x64.zip"
Compress-Archive -LiteralPath $executable,(Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'README.md') -DestinationPath $portable -Force
& $InnoCompiler "/DAppVersion=$version" "/DBuildDir=$buildPath" "/DDistDir=$outputPath" (Join-Path $projectRoot 'installer/BrightnessTray.iss')
if ($LASTEXITCODE) { throw 'Installer compilation failed.' }
$installer=Join-Path $outputPath "BrightnessTray-$version-Setup-x64.exe"
$checksums=@($installer,$portable) | ForEach-Object { $hash=Get-FileHash -LiteralPath $_ -Algorithm SHA256; $hash.Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($_) }
[IO.File]::WriteAllLines((Join-Path $outputPath 'SHA256SUMS.txt'),$checksums,[Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath $installer,$portable,(Join-Path $outputPath 'SHA256SUMS.txt') | Select-Object Name,Length
