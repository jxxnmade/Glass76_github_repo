<#
.SYNOPSIS
    Compile installer/Glass76.nsi into a signed-shaped Windows installer .exe.

.DESCRIPTION
    Reads the version out of CMakeLists.txt so the installer, the Apps &
    features entry and the plug-in binary never drift apart, then hands
    makensis the bundle to package.

    Requires NSIS 3 (https://nsis.sourceforge.io). It is found on PATH, in the
    registry, or in the usual Program Files location.

.PARAMETER Configuration
    Which build configuration's bundle to package. Release by default.

.PARAMETER BundleDir
    Package this bundle instead of build\VST3\<Configuration>\Glass76.vst3.

.PARAMETER Build
    Build the plug-in first, by calling scripts\build.ps1.

.PARAMETER OutDir
    Where to write the installer. Defaults to build\installer.

.EXAMPLE
    .\installer\build_installer.ps1 -Build
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    [string] $BundleDir,
    [string] $OutDir,
    [switch] $Build
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "!!  $text" -ForegroundColor Red; exit 1 }

#--- Find makensis -----------------------------------------------------------
function Find-MakeNsis {
    $onPath = Get-Command makensis -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    foreach ($key in 'HKLM:\SOFTWARE\NSIS', 'HKLM:\SOFTWARE\WOW6432Node\NSIS') {
        try {
            $dir = (Get-ItemProperty -Path $key -ErrorAction Stop).'(default)'
            if ($dir -and (Test-Path (Join-Path $dir 'makensis.exe'))) {
                return (Join-Path $dir 'makensis.exe')
            }
        } catch { }
    }
    foreach ($dir in "${env:ProgramFiles(x86)}\NSIS", "${env:ProgramFiles}\NSIS") {
        $exe = Join-Path $dir 'makensis.exe'
        if (Test-Path $exe) { return $exe }
    }
    return $null
}

$makensis = Find-MakeNsis
if (-not $makensis) {
    Fail @"
makensis.exe was not found.

Install NSIS 3: https://nsis.sourceforge.io/Download
  winget install NSIS.NSIS
or                    choco install nsis
"@
}
Step "Using $makensis"

#--- Version -----------------------------------------------------------------
# Single source of truth is project(... VERSION x.y.z.w) in CMakeLists.txt.
$cmakeLists = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch '(?ms)project\s*\(\s*Glass76.*?VERSION\s+([0-9]+(?:\.[0-9]+)*)') {
    Fail 'Could not read the project version out of CMakeLists.txt.'
}
$parts = @($Matches[1].Split('.'))
while ($parts.Count -lt 4) { $parts += '0' }
$version4 = ($parts[0..3]) -join '.'
$version  = ($parts[0..2]) -join '.'
Step "Version $version (file version $version4)"

#--- Build the plug-in if asked ----------------------------------------------
if ($Build) {
    & (Join-Path $Root 'scripts\build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { Fail 'The plug-in build failed.' }
}

#--- Locate the bundle -------------------------------------------------------
if (-not $BundleDir) {
    $BundleDir = Join-Path $Root "build\VST3\$Configuration\Glass76.vst3"
}
if (-not (Test-Path (Join-Path $BundleDir 'Contents\x86_64-win\Glass76.vst3'))) {
    Fail @"
No plug-in bundle at:
    $BundleDir

Build it first:
    .\scripts\build.ps1
or re-run this script with -Build.
"@
}
$BundleDir = (Resolve-Path $BundleDir).Path
Step "Packaging $BundleDir"

#--- Compile -----------------------------------------------------------------
if (-not $OutDir) { $OutDir = Join-Path $Root 'build\installer' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outFile = Join-Path (Resolve-Path $OutDir).Path "Glass76-$version-win64.exe"

$nsi = Join-Path $PSScriptRoot 'Glass76.nsi'
$icon = Join-Path $PSScriptRoot 'assets\Glass76.ico'
if (-not (Test-Path $icon)) {
    Step 'assets\Glass76.ico is missing; regenerating it'
    & python (Join-Path $PSScriptRoot 'assets\make_icon.py')
    if (-not (Test-Path $icon)) { Fail 'Icon generation failed. Install Pillow, or restore the .ico.' }
}

Step 'Running makensis'
& $makensis `
    "/DVERSION=$version" `
    "/DVERSION4=$version4" `
    "/DBUNDLE_DIR=$BundleDir" `
    "/DOUTFILE=$outFile" `
    $nsi
if ($LASTEXITCODE -ne 0) { Fail "makensis failed (exit $LASTEXITCODE)." }
if (-not (Test-Path $outFile)) { Fail "makensis reported success but $outFile is not there." }

$size = [math]::Round((Get-Item $outFile).Length / 1MB, 2)
Write-Host ''
Step "Installer written: $outFile ($size MB)"
Write-Host ''
Write-Host 'It is unsigned, so Windows SmartScreen will warn on first run.' -ForegroundColor Yellow
Write-Host 'To sign it:  signtool sign /fd SHA256 /tr <timestamp-url> /td SHA256 /f <cert.pfx> "' -NoNewline -ForegroundColor DarkGray
Write-Host "$outFile`"" -ForegroundColor DarkGray
