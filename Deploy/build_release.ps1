<#
.SYNOPSIS
    Packs GRAVE PRIX for handing out: dist\GravePrix-<version>-windows-x64.zip and, through WSL, the Linux
    archive (for the Steam Deck as well).

.DESCRIPTION
    .\Deploy\build_release.ps1 [-NoBuild] [-NoLinux]

    Builds the Release (build.ps1), checks the data (--validate), stages the game with its data and assets, the
    player's readme, the credits and the licence in work\release and zips them into dist. The Linux archive comes from
    Deploy/build_linux.sh in WSL (GCC 12, glibc 2.35 and newer, the Steam Deck included); without WSL it is skipped.

    -NoBuild   pack what bin\msvc holds
    -NoLinux   only the Windows archive

.EXAMPLE
    .\Deploy\build_release.ps1
#>
param(
    [switch]$NoBuild,
    [switch]$NoLinux
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\( GravePrix VERSION ([0-9.]+)').Matches[0].Groups[1].Value
Write-Host "GRAVE PRIX $version"

if (-not $NoBuild) {
    & (Join-Path $root "build.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
$exe = Join-Path $root "bin\msvc\GravePrix.exe"
& $exe --validate --data (Join-Path $root "data") | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { throw "the data has errors" }

$name = "GravePrix-$version-windows-x64"
$stage = Join-Path $root "work\release\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
foreach ($dir in "data", "assets") {
    & robocopy (Join-Path $root $dir) (Join-Path $stage $dir) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copying $dir failed" }
}
Copy-Item (Join-Path $root "docs\README-player.txt") (Join-Path $stage "README.txt")
Copy-Item (Join-Path $root "docs\CREDITS.txt") $stage
Copy-Item (Join-Path $root "LICENSE") $stage
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
# Windows' own tar (bsdtar) zips faster than Compress-Archive.
& (Join-Path $env:SystemRoot "System32\tar.exe") -a -c -f $zip -C (Join-Path $root "work\release") $name
if ($LASTEXITCODE -ne 0) { throw "zip failed" }
Write-Host ("{0}  {1:N0} MB" -f $zip, ((Get-Item $zip).Length / 1MB))

if (-not $NoLinux) {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        Write-Host "no WSL: no Linux archive"
    } else {
        $drive = $root.Substring(0, 1).ToLower()
        $wslRoot = "/mnt/$drive" + ($root.Substring(2) -replace '\\', '/')
        & wsl.exe -d Ubuntu-22.04 -- bash "$wslRoot/Deploy/build_linux.sh"
        if ($LASTEXITCODE -ne 0) { throw "Linux build failed" }
        $tgz = Join-Path $dist "GravePrix-$version-linux-x86_64.tar.gz"
        Write-Host ("{0}  {1:N0} MB" -f $tgz, ((Get-Item $tgz).Length / 1MB))
    }
}
