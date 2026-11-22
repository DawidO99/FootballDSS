<#
.SYNOPSIS
    Builds FootballDSS in Release and packs a portable ZIP into dist/.

.DESCRIPTION
    P03 spike (docs/adr/ADR-006-spike-results.md). Steps:
      1. MSVC x64 environment (found with vswhere) unless cl.exe is already on PATH.
      2. cmake --preset release against -QtDir (--fresh if the existing build used another Qt kit),
         build the FootballDSS target.
      3. Copy FootballDSS.exe to dist/FootballDSS-<version>-win64/.
      4. windeployqt --release --qmldir ui (Qt DLLs, plugins, QML modules).
      5. MSVC runtime DLLs next to the exe (app-local), so no VC++ Redistributable is needed.
      6. Check that the key files are there, list them and zip the folder.
    Works in Windows PowerShell 5.1 and PowerShell 7.

.PARAMETER QtDir
    Qt kit for MSVC, e.g. C:\Qt\6.12.0\msvc2022_64.

.PARAMETER OutDir
    Output folder, relative to the repository root. Default: dist.

.PARAMETER SkipBuild
    Package the existing build/release/FootballDSS.exe without configuring or building.
    Fails if that build used a different Qt kit than -QtDir.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -QtDir C:\Qt\6.12.0\msvc2022_64

    The default execution policy on Windows blocks .\scripts\package-windows.ps1; -File with
    -ExecutionPolicy Bypass runs it without changing the machine's policy.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtDir,
    [string]$OutDir = 'dist',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot

# Native tools do not throw on failure; stop on a non-zero exit code.
function Invoke-Native {
    param([Parameter(Mandatory = $true)][string]$Exe, [string[]]$Arguments = @())
    Write-Host ">> $Exe $($Arguments -join ' ')" -ForegroundColor Cyan
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

function Assert-Command {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][string]$Hint)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name not found on PATH. $Hint"
    }
}

function Enter-MsvcEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        Write-Host 'MSVC environment already active.'
        return
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio 2022 (or newer) with "Desktop development with C++".'
    }
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw 'No Visual Studio installation with the x64 C++ tools found.'
    }
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    Write-Host "MSVC environment: $vsPath"
}

function Get-ProjectVersion {
    $cmakeLists = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
    $match = [regex]::Match($cmakeLists, 'project\(\s*FootballDSS\s+VERSION\s+([0-9.]+)')
    if (-not $match.Success) {
        throw 'Cannot read the project version from CMakeLists.txt.'
    }
    return $match.Groups[1].Value
}

# CMake paths use forward slashes; a backslash can act as an escape character.
function ConvertTo-CMakePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $Path.Replace('\', '/').TrimEnd('/')
}

# Qt6_DIR from a CMake cache; $null when the file or the entry does not exist.
function Get-CachedQt6Dir {
    param([Parameter(Mandatory = $true)][string]$CacheFile)
    if (-not (Test-Path -LiteralPath $CacheFile)) {
        return $null
    }
    $entry = Select-String -LiteralPath $CacheFile -Pattern '^Qt6_DIR:[^=]*=(.+)$' | Select-Object -First 1
    if (-not $entry) {
        return $null
    }
    return $entry.Matches[0].Groups[1].Value.Trim()
}

# The trailing slash keeps msvc2022_64_static from matching msvc2022_64. Qt6_DIR-NOTFOUND never matches.
function Test-Qt6DirInKit {
    param(
        [Parameter(Mandatory = $true)][string]$Qt6Dir,
        [Parameter(Mandatory = $true)][string]$Kit
    )
    $kitPrefix = (ConvertTo-CMakePath $Kit) + '/'
    return (ConvertTo-CMakePath $Qt6Dir).StartsWith($kitPrefix, [StringComparison]::OrdinalIgnoreCase)
}

$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe not found in $QtDir\bin. Pass the MSVC kit, e.g. C:\Qt\6.12.0\msvc2022_64."
}
# Absolute, because the script changes directory and compares the kit with paths in the CMake cache.
$QtDir = (Resolve-Path -LiteralPath $QtDir).Path
$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'

Push-Location $repo
try {
    Enter-MsvcEnvironment

    $exe = Join-Path $repo 'build\release\FootballDSS.exe'
    $cacheFile = Join-Path $repo 'build\release\CMakeCache.txt'
    $cachedQt6Dir = Get-CachedQt6Dir $cacheFile
    $otherKitCached = $cachedQt6Dir -and -not (Test-Qt6DirInKit $cachedQt6Dir $QtDir)
    if ($SkipBuild) {
        if ($otherKitCached) {
            throw "build\release was configured with Qt6_DIR=$cachedQt6Dir, not $QtDir. Run without -SkipBuild."
        }
    }
    else {
        Assert-Command 'cmake' 'Use CMake 3.28+ (Visual Studio or Qt Tools\CMake_64\bin).'
        Assert-Command 'ninja' 'Use Ninja from Visual Studio or Qt Tools\Ninja.'
        $configureArgs = @('--preset', 'release', "-DCMAKE_PREFIX_PATH=$(ConvertTo-CMakePath $QtDir)")
        if ($otherKitCached) {
            Write-Host "build\release used Qt6_DIR=$cachedQt6Dir; reconfiguring from scratch."
            $configureArgs += '--fresh'
        }
        Invoke-Native 'cmake' $configureArgs

        $configuredQt6Dir = Get-CachedQt6Dir $cacheFile
        if (-not $configuredQt6Dir -or -not (Test-Qt6DirInKit $configuredQt6Dir $QtDir)) {
            throw "CMake configured Qt6_DIR=$configuredQt6Dir, which is outside $QtDir."
        }
        Invoke-Native 'cmake' @('--build', '--preset', 'release', '--target', 'FootballDSS')
    }
    $qtVersion = & (Join-Path $QtDir 'bin\qtpaths.exe') --qt-version
    Write-Host "Qt kit: $QtDir (Qt $qtVersion)"

    if (-not (Test-Path $exe)) {
        throw "$exe not found. Run without -SkipBuild first."
    }

    $version = Get-ProjectVersion
    $name = "FootballDSS-$version-win64"
    $outRoot = Join-Path $repo $OutDir
    $stage = Join-Path $outRoot $name
    $zip = Join-Path $outRoot "$name.zip"
    if (Test-Path $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination $stage

    # --no-compiler-runtime: windeployqt would add the vc_redist installer; a portable ZIP
    # carries the runtime DLLs instead (below).
    Invoke-Native $windeployqt @(
        '--release',
        '--qmldir', (Join-Path $repo 'ui'),
        '--no-compiler-runtime',
        (Join-Path $stage 'FootballDSS.exe')
    )

    if (-not $env:VCToolsRedistDir) {
        throw 'VCToolsRedistDir is not set; the MSVC environment is incomplete.'
    }
    $crt = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
        Select-Object -First 1
    if (-not $crt) {
        throw "No Microsoft.VC*.CRT folder in $env:VCToolsRedistDir\x64."
    }
    Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $stage
    Write-Host "MSVC runtime: $($crt.FullName)"

    # Files the app cannot start or show charts without.
    $required = @(
        'FootballDSS.exe',
        'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6Graphs.dll',
        'platforms\qwindows.dll',
        'qml\QtQuick\Controls\Basic',
        'qml\QtQuick\Layouts',
        'qml\QtGraphs',
        'qml\QtQuick\Dialogs',
        'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll'
    )
    # windeployqt adds Qt6Sql.dll once the app links Qt6::Sql (P05); it cannot open a database
    # without the driver plugin.
    if (Test-Path (Join-Path $stage 'Qt6Sql.dll')) {
        $required += 'Qt6Sql.dll', 'sqldrivers\qsqlite.dll'
    }
    $missing = @($required | Where-Object { -not (Test-Path (Join-Path $stage $_)) })
    if ($missing.Count -gt 0) {
        throw "Package is incomplete, missing: $($missing -join ', ')"
    }
    Write-Host 'Package contains:'
    $required | ForEach-Object { Write-Host "  $_" }
    # Reported only: QtQml can pull it in by itself, so it does not prove that the app links it (P07a).
    $networkState = if (Test-Path (Join-Path $stage 'Qt6Network.dll')) { 'present' } else { 'not deployed' }
    Write-Host "  Qt6Network.dll: $networkState"

    if (Test-Path $zip) {
        Remove-Item -LiteralPath $zip -Force
    }
    Compress-Archive -Path $stage -DestinationPath $zip
    $sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
    Write-Host ''
    Write-Host "Package: $zip ($sizeMb MB)" -ForegroundColor Green
    Write-Host 'Next: copy the ZIP to a Windows machine without Qt, unzip, run FootballDSS.exe,'
    Write-Host 'press Ctrl+Shift+K for the component gallery with the load chart and fill in the'
    Write-Host 'checklist in ADR-006.'
}
finally {
    Pop-Location
}
