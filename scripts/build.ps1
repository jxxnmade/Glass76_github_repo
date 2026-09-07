<#
.SYNOPSIS
    Configure, build, validate and install Glass76.

.DESCRIPTION
    The one script you need to go from a clean checkout to a plug-in FL Studio
    can see. It only needs CMake and a Visual Studio C++ toolchain on PATH; the
    VST 3 SDK is located the same way CMakeLists.txt locates it (see
    docs/BUILDING.md), and -FetchSdk will clone it for you.

.PARAMETER Configuration
    Release (default) or Debug.

.PARAMETER SdkDir
    Path to a VST 3 SDK checkout. Overrides extern/vst3sdk and $env:VST3_SDK_DIR.

.PARAMETER FetchSdk
    Clone the pinned SDK into extern/vst3sdk if no SDK is found. ~1 GB.

.PARAMETER Clean
    Delete the build directory first.

.PARAMETER Test
    Build and run the offline DSP test host.

.PARAMETER Validate
    Run the SDK validator against the built bundle.

.PARAMETER Install
    Copy the bundle into the shared VST3 folder. Needs an elevated shell.

.EXAMPLE
    .\scripts\build.ps1 -Validate -Test

.EXAMPLE
    .\scripts\build.ps1 -FetchSdk -Install
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    [string] $SdkDir,
    [switch] $FetchSdk,
    [switch] $Clean,
    [switch] $Test,
    [switch] $Validate,
    [switch] $Install
)

$ErrorActionPreference = 'Stop'
$Root      = Split-Path -Parent $PSScriptRoot
$BuildDir  = Join-Path $Root 'build'
$BundleDir = Join-Path $BuildDir "VST3\$Configuration\Glass76.vst3"

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "!!  $text" -ForegroundColor Red; exit 1 }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail 'cmake is not on PATH. Install CMake 3.25 or newer.'
}

if ($Clean -and (Test-Path $BuildDir)) {
    Step "Removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

#--- Configure ---------------------------------------------------------------
$cfgArgs = @('-S', $Root, '-B', $BuildDir, '-A', 'x64')
if ($SdkDir)  { $cfgArgs += "-Dvst3sdk_SOURCE_DIR=$((Resolve-Path $SdkDir).Path)" }
if ($FetchSdk) { $cfgArgs += '-DGLASS76_FETCH_SDK=ON' }
if ($Test)     { $cfgArgs += '-DGLASS76_BUILD_TESTS=ON' }
if ($Validate) { $cfgArgs += '-DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON' }

Step "Configuring ($Configuration, x64)"
& cmake @cfgArgs
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }

#--- Build -------------------------------------------------------------------
Step "Building Glass76"
& cmake --build $BuildDir --config $Configuration --target Glass76
if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }

if (-not (Test-Path $BundleDir)) { Fail "Expected a bundle at $BundleDir but it is not there." }
Step "Built $BundleDir"

#--- Validate ----------------------------------------------------------------
if ($Validate) {
    Step 'Building the SDK validator'
    & cmake --build $BuildDir --config $Configuration --target validator
    if ($LASTEXITCODE -ne 0) { Fail 'Building the validator failed.' }

    $validator = Join-Path $BuildDir "bin\$Configuration\validator.exe"
    if (-not (Test-Path $validator)) { $validator = Join-Path $BuildDir 'bin\validator.exe' }
    Step 'Running the validator'
    & $validator $BundleDir
    if ($LASTEXITCODE -ne 0) { Fail 'The validator reported failures.' }
}

#--- Offline DSP tests -------------------------------------------------------
if ($Test) {
    Step 'Building the offline test host'
    & cmake --build $BuildDir --config $Configuration --target glass76_test
    if ($LASTEXITCODE -ne 0) { Fail 'Building the test host failed.' }

    $exe = Join-Path $BuildDir "bin\$Configuration\glass76_test.exe"
    if (-not (Test-Path $exe)) { $exe = Join-Path $BuildDir 'bin\glass76_test.exe' }
    Step 'Running the offline DSP tests'
    & $exe $BundleDir
    if ($LASTEXITCODE -ne 0) { Fail 'Offline DSP tests failed.' }
}

#--- Install -----------------------------------------------------------------
if ($Install) {
    $target = Join-Path ${env:CommonProgramFiles} 'VST3'
    $dest   = Join-Path $target 'Glass76.vst3'

    $dll = Join-Path $dest 'Contents\x86_64-win\Glass76.vst3'
    if (Test-Path $dll) {
        # A host that still has the DLL mapped makes the copy fail halfway.
        try { [IO.File]::Open($dll, 'Open', 'Write').Dispose() }
        catch { Fail 'Glass76 is loaded by another program. Close your DAW and try again.' }
    }

    Step "Installing to $dest"
    try {
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -Recurse -Force $BundleDir $dest
    } catch {
        Fail "Install failed: $($_.Exception.Message)`n    Run this script from an elevated PowerShell."
    }
    Write-Host ''
    Write-Host 'In FL Studio: Options > Manage plugins > Find installed plugins,' -ForegroundColor Yellow
    Write-Host 'with "Rescan previously verified plugins" ticked.' -ForegroundColor Yellow
}

Write-Host ''
Step 'Done.'
