# Run from the repository root in an x64 Visual Studio developer PowerShell.
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$prefix = Join-Path $root '_ci/runtime'
$tcl = Join-Path $root '_ci/tcl'
$tk = Join-Path $root '_ci/tk'
$cairo = Join-Path $root '_ci/cairo-static'
$out = Join-Path $root '_ci/rbc-build'
function Invoke-Native {
    param([string]$Program, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Invoke-Nmake {
    Invoke-Native nmake /nologo /f makefile.vc @args
}
Push-Location "$tcl/win"
try {
    Invoke-Nmake "INSTALLDIR=$prefix" release
    Invoke-Nmake "INSTALLDIR=$prefix" install
} finally { Pop-Location }
Push-Location "$tk/win"
try {
    Invoke-Nmake "TCLDIR=$tcl" "INSTALLDIR=$prefix" release
    Invoke-Nmake "TCLDIR=$tcl" "INSTALLDIR=$prefix" install
} finally { Pop-Location }
$env:Path = "$prefix\bin;" + $env:Path
$tclsh = Join-Path $prefix 'bin/tclsh91.exe'
Push-Location "$root/_ci/argparse/win"
try {
    Invoke-Nmake "TCLDIR=$tcl" "INSTALLDIR=$prefix"
    Invoke-Nmake "TCLDIR=$tcl" "INSTALLDIR=$prefix" install
} finally { Pop-Location }
$env:CC = 'cl'
$env:CXX = 'cl'
Invoke-Native python "$root/.github/scripts/build-cairo.py"

# These are genuine static libraries produced above with the matching MSVC CRT.
$cairoLibs = '"' + "$cairo\lib\cairo.lib" + '" "' + "$cairo\lib\pixman-1.lib" + '" gdi32.lib msimg32.lib'
$rbcArgs = @("TCLDIR=$tcl", "TKDIR=$tk", "TCLSH=$tclsh",
             "OUT_DIR=$out", "TMP_DIR=$out\obj", 'CAIRO=1', 'CAIRO_STATIC=1',
             "CAIRODIR=$cairo", "CAIRO_LIBS=$cairoLibs")
Push-Location "$root/win"
try {
    Invoke-Nmake @rbcArgs "INSTALLDIR=$prefix"
    $env:TCLLIBPATH = '{' + $out.Replace('\', '/') + '} {' + "$root/_ci/tcllib/modules".Replace('\', '/') + '}'
    Invoke-Native $tclsh "$root/.github/scripts/smoke.tcl"
    Invoke-Nmake @rbcArgs "INSTALLDIR=$prefix" test
    Invoke-Nmake @rbcArgs "INSTALLDIR=$prefix" test-headers
} finally { Pop-Location }
Invoke-Native python "$root/.github/scripts/check-linkage.py" windows-msvc $out

if ($env:MAKE_DIST -eq 'true') {
    $version = [regex]::Match((Get-Content "$root/configure.ac" -Raw), 'AC_INIT\(\[rbc\],\[([^]]+)\]\)').Groups[1].Value
    if (!$version) { throw 'Cannot determine the RBC package version' }
    $stage = Join-Path $root "_ci/dist/rbc$version"
    New-Item -ItemType Directory -Force $stage | Out-Null
    Push-Location "$root/win"
    try {
        Invoke-Nmake @rbcArgs "INSTALLDIR=$stage" `
            "DOC_INSTALL_DIR=$stage\share\rbc$version\doc" `
            "DEMO_INSTALL_DIR=$stage\share\rbc$version\demos" install
    } finally { Pop-Location }
    Get-ChildItem "$prefix/lib/argparse*" -Directory | ForEach-Object {
        Copy-Item -Recurse $_.FullName "$stage/lib/"
    }
    $env:TCLLIBPATH = '{' + "$stage/lib".Replace('\', '/') + '}'
    Invoke-Native $tclsh "$root/.github/scripts/smoke.tcl"
    Invoke-Native python "$root/.github/scripts/package.py" windows-msvc $stage
}
