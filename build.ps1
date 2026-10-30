<#
.SYNOPSIS
    Builds, tests and starts GravePrix.

.DESCRIPTION
    .\build.ps1 [msvc] [-Config Debug] [-Test] [-Run] [-Fresh] [-Jobs n]

      msvc     Visual Studio's compiler: build\msvc (the solution is there), the game in bin\msvc

    -Dir name       build in build\<name> instead (Release, nothing copied to bin): for a second worker that must
                    not lock bin\msvc\GravePrix.exe; the game is then build\<name>\Release\GravePrix.exe

    -Config Debug   the Debug configuration: bin\msvc-Debug is not used, the Debug exe stays in build\msvc\Debug
    -Test           run the tests afterwards (ctest)
    -Run            start the game from bin\msvc afterwards
    -Fresh          configure from scratch (cmake --fresh); nothing is deleted

    Linux (WSL, CI, Steam Deck): cmake --preset linux; cmake --build --preset linux

.EXAMPLE
    .\build.ps1                 # msvc, Release
    .\build.ps1 -Test -Run
#>
param(
    [Parameter(Position = 0)][ValidateSet("msvc")][string]$Preset = "msvc",
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [switch]$Test,
    [switch]$Run,
    [switch]$Fresh,
    [string]$Dir = "",
    # The machine is shared with other builds and with the user: at most 8 jobs.
    [int]$Jobs = 8
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

Push-Location $root
if ($Dir) {
    try {
        & cmake -S . -B "build/$Dir" -G "Visual Studio 18 2026" -A x64
        if ($LASTEXITCODE -ne 0) { throw "configure failed" }
        & cmake --build "build/$Dir" --config Release --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "build failed" }
    } finally {
        Pop-Location
    }
    Write-Host "game: $(Join-Path $root "build\$Dir\Release\GravePrix.exe")"
    return
}
try {
    $configure = @("--preset", $Preset)
    if ($Fresh) { $configure += "--fresh" }
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
    $buildPreset = if ($Config -eq "Debug") { "msvc-debug" } else { $Preset }
    & cmake --build --preset $buildPreset --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    if ($Test) {
        & ctest --preset $buildPreset
        if ($LASTEXITCODE -ne 0) { throw "tests failed" }
    }
} finally {
    Pop-Location
}

$exe = if ($Config -eq "Debug") { Join-Path $root "build\msvc\Debug\GravePrix.exe" } else { Join-Path $root "bin\$Preset\GravePrix.exe" }
Write-Host "game: $exe"
if ($Run) {
    if (-not (Test-Path $exe)) { throw "not found: $exe" }
    Start-Process $exe
}
