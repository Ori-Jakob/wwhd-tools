<#
.SYNOPSIS
    Builds wwhd_tools.rpl and the rpl-loader plugin, then packs the release zip.

.DESCRIPTION
    Produces dist\WWHD-Tools-<version>.zip with this layout:

        Cemu\WWHD-Tools\rules.txt                       graphics pack
        Cemu\WWHD-Tools\patch_wwhd_tools.asm
        Cemu\WWHD-Tools\code\wwhd_tools.rpl
        Wii U\wiiu\environments\aroma\plugins\rpl_loader.wps
        Wii U\wiiu\rpl-loader\universal\wwhd_tools.rpl

    The same layout is produced by .github\workflows\build.yml; keep the two
    in step.

    Needs devkitPro with devkitPPC, wut, WUPS and the WUMS libraries
    installed (C:\devkitPro by default, or $env:DEVKITPRO), and a Python 3
    on PATH for the loader's rplname.py.

.PARAMETER Version
    Version string baked into wwhd_tools.rpl and used in the zip name.
    Defaults to the short git hash, or "dev" outside a git checkout.

.PARAMETER DevkitPro
    devkitPro root. Defaults to $env:DEVKITPRO, then C:\devkitPro.

.PARAMETER OutDir
    Where the staging tree and the zip go. Defaults to .\dist.

.PARAMETER NoBuild
    Skip both make invocations and pack whatever binaries are already there.

.PARAMETER Clean
    Run "make clean" in both projects before building.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Version 1.2.0
    .\build.ps1 -NoBuild
#>
[CmdletBinding()]
param(
    [string]$Version,
    [string]$DevkitPro,
    [string]$OutDir,
    [switch]$NoBuild,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$RepoRoot   = $PSScriptRoot
$LoaderDir  = Join-Path $RepoRoot 'external\wii-u-rpl-loader'
$PackDir    = Join-Path $RepoRoot 'Cemu\graphicspack'
$ToolRpl    = Join-Path $RepoRoot 'wwhd_tools.rpl'
$LoaderWps  = Join-Path $LoaderDir 'rpl_loader.wps'

if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'dist' }

# Runs a native command and returns its stdout as one string. Under
# $ErrorActionPreference = 'Stop', PowerShell 5.1 turns any stderr output of a
# native command into a terminating error, so the preference is relaxed for the
# call and stderr is dropped. Check $LASTEXITCODE afterwards.
function Invoke-Native([string]$exe, [string[]]$argv) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $exe @argv 2>&1
    } catch {
        $global:LASTEXITCODE = 1
        return ''
    } finally {
        $ErrorActionPreference = $prev
    }
    return (($out | Where-Object { $_ -is [string] }) -join "`n")
}

# ---------------------------------------------------------------- version --
if (-not $Version) {
    $Version = 'dev'
    $sha = Invoke-Native 'git' @('-C', $RepoRoot, 'rev-parse', '--short', 'HEAD')
    if ($LASTEXITCODE -eq 0 -and $sha.Trim()) { $Version = $sha.Trim() }
}
Write-Host "==> version $Version"

# -------------------------------------------------------------- toolchain --
if (-not $DevkitPro) {
    # The installer sets DEVKITPRO to the msys form (/opt/devkitpro), which is
    # not a Windows path, so only trust it when it actually resolves.
    $candidates = @()
    if ($env:DEVKITPRO) { $candidates += $env:DEVKITPRO }
    $candidates += 'C:\devkitPro'
    $DevkitPro = $candidates | Where-Object { Test-Path (Join-Path $_ 'msys2\usr\bin\make.exe') } | Select-Object -First 1
    if (-not $DevkitPro) { $DevkitPro = $candidates[-1] }
}
$DevkitPro = $DevkitPro.TrimEnd('\', '/')

function ConvertTo-PosixPath([string]$p) {
    # C:\devkitPro -> /c/devkitPro, the form the msys make and the wut rules want
    $p = $p -replace '\\', '/'
    if ($p -match '^([A-Za-z]):(.*)$') { return '/' + $Matches[1].ToLower() + $Matches[2] }
    return $p
}

$Make = Join-Path $DevkitPro 'msys2\usr\bin\make.exe'

if (-not $NoBuild) {
    foreach ($req in @(
        @{ Path = $Make;                                       What = 'msys2 make' },
        @{ Path = (Join-Path $DevkitPro 'devkitPPC\bin');      What = 'devkitPPC' },
        @{ Path = (Join-Path $DevkitPro 'wut\share\wut_rules');   What = 'wut' },
        @{ Path = (Join-Path $DevkitPro 'wups\share\wups_rules'); What = 'WUPS' },
        @{ Path = (Join-Path $DevkitPro 'wums\share\libmappedmemory.ld'); What = 'WUMS (libmappedmemory)' }
    )) {
        if (-not (Test-Path $req.Path)) {
            throw "$($req.What) not found at $($req.Path). Set -DevkitPro or `$env:DEVKITPRO."
        }
    }

    # Probe rather than trust Get-Command: on Windows "python3" is often the
    # Microsoft Store alias stub, which is on PATH but only prints an advert.
    $python = $null
    foreach ($name in @('python3', 'python', 'py')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        $major = Invoke-Native $cmd.Source @('-c', 'import sys; print(sys.version_info[0])')
        if ($LASTEXITCODE -eq 0 -and $major.Trim() -eq '3') { $python = $cmd; break }
    }
    if (-not $python) { throw 'No working Python 3 on PATH; the rpl build needs it for rplname.py.' }

    # Exported environment does not reliably reach make's recipes through the
    # msys layer, but command-line variables do, so everything goes on the
    # command line. PATH is converted by the msys runtime; the extra dirs give
    # the recipes the compiler, elf2rpl, rplexportgen, bin2s, sh and coreutils.
    $env:PATH = (@(
        (Join-Path $DevkitPro 'devkitPPC\bin'),
        (Join-Path $DevkitPro 'tools\bin'),
        (Join-Path $DevkitPro 'msys2\usr\bin'),
        $env:PATH
    ) -join ';')
    if (-not $env:TMP)  { $env:TMP  = [System.IO.Path]::GetTempPath() }
    if (-not $env:TEMP) { $env:TEMP = $env:TMP }

    $dkp = ConvertTo-PosixPath $DevkitPro
    $makeVars = @(
        "DEVKITPRO=$dkp",
        "DEVKITPPC=$dkp/devkitPPC",
        "PYTHON=$($python.Name)"
    )

    function Invoke-Make([string]$dir, [string[]]$makeArgs) {
        Push-Location $dir
        try {
            & $Make @makeArgs
            if ($LASTEXITCODE -ne 0) { throw "make failed in $dir (exit $LASTEXITCODE)" }
        } finally { Pop-Location }
    }

    if ($Clean) {
        Write-Host '==> make clean'
        Invoke-Make $LoaderDir (@('clean') + $makeVars)
        Invoke-Make $RepoRoot  (@('clean') + $makeVars)
    }

    Write-Host '==> building rpl_loader.wps'
    Invoke-Make $LoaderDir $makeVars

    Write-Host '==> building wwhd_tools.rpl'
    Invoke-Make $RepoRoot ($makeVars + @("VERSION=$Version"))
}

foreach ($f in @($ToolRpl, $LoaderWps, (Join-Path $PackDir 'rules.txt'), (Join-Path $PackDir 'patch_wwhd_tools.asm'))) {
    if (-not (Test-Path $f)) { throw "missing $f" }
}

# ------------------------------------------------------------------ stage --
$Stage = Join-Path $OutDir 'WWHD-Tools'
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }

$cemuPack    = Join-Path $Stage 'Cemu\WWHD-Tools'
$cemuCode    = Join-Path $cemuPack 'code'
$wiiuPlugins = Join-Path $Stage 'Wii U\wiiu\environments\aroma\plugins'
$wiiuRpls    = Join-Path $Stage 'Wii U\wiiu\rpl-loader\universal'

foreach ($d in @($cemuCode, $wiiuPlugins, $wiiuRpls)) {
    New-Item -ItemType Directory -Force $d | Out-Null
}

Copy-Item (Join-Path $PackDir '*') $cemuPack
Copy-Item $ToolRpl   $cemuCode
Copy-Item $LoaderWps $wiiuPlugins
Copy-Item $ToolRpl   $wiiuRpls

# -------------------------------------------------------------------- zip --
# Written with ZipArchive rather than Compress-Archive: PowerShell 5.1 emits
# backslash separators in entry names, which unzip on Linux and macOS turns
# into literal file names.
$ZipPath = Join-Path $OutDir "WWHD-Tools-$Version.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $prefix = (Resolve-Path $Stage).Path.TrimEnd('\') + '\'
    Get-ChildItem -Path $Stage -Recurse -File | Sort-Object FullName | ForEach-Object {
        $entry = $_.FullName.Substring($prefix.Length) -replace '\\', '/'
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $_.FullName, $entry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        Write-Host "   $entry"
    }
} finally {
    $zip.Dispose()
}

Write-Host "==> wrote $ZipPath"
