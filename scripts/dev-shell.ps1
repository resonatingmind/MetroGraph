# Loads the MSVC x64 build environment (cl, cmake, ninja) into the current PowerShell session.
# Usage, from the repo root:   . .\scripts\dev-shell.ps1
$ErrorActionPreference = 'Stop'

$installerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$vswhere = Join-Path $installerDir 'vswhere.exe'
# The dev-shell launcher shells out to vswhere by name, so it must be on PATH.
if ((Test-Path $vswhere) -and ($env:Path -notlike "*$installerDir*")) { $env:Path = "$installerDir;$env:Path" }
$vs = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) {
    # vswhere can miss Build Tools installs made by the command-line installer; fall back to the default path.
    $vs = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools'
}
$launcher = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path $launcher)) {
    throw "Visual Studio C++ Build Tools not found. Install the 'Desktop development with C++' workload."
}
& $launcher -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Write-Host "MSVC environment loaded: $((Get-Command cl).Source)"
