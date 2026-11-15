# Run from the repository root in an x64 Visual Studio developer PowerShell.
$ErrorActionPreference = 'Stop'
if (!$env:RBC_CI_RENDERER) { $env:RBC_CI_RENDERER = 'static-cairo' }
if ($env:RBC_CI_RENDERER -notin @('static-cairo', 'native')) {
    throw "Unknown renderer variant: $env:RBC_CI_RENDERER"
}
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
$env:Path = "$prefix\bin;" + $env:Path
$tclsh = Join-Path $prefix 'bin/tclsh91.exe'
if ($env:RUNTIME_CACHE_HIT -ne 'true') {
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
    Push-Location "$root/_ci/argparse/win"
    try {
        Invoke-Nmake "TCLDIR=$tcl" "INSTALLDIR=$prefix"
        Invoke-Nmake "TCLDIR=$tcl" "INSTALLDIR=$prefix" install
    } finally { Pop-Location }
} else {
    Write-Host "Using cached Tcl/Tk and argparse runtime"
}
$rbcArgs = @("TCLDIR=$tcl", "TKDIR=$tk", "TCLSH=$tclsh",
             "OUT_DIR=$out", "TMP_DIR=$out\obj")
if ($env:RBC_CI_RENDERER -eq 'static-cairo') {
    $env:CC = 'cl'
    $env:CXX = 'cl'
    if ($env:CAIRO_CACHE_HIT -ne 'true') {
        Invoke-Native python "$root/.github/scripts/build-cairo.py"
    } else {
        Write-Host "Using cached static Cairo and Pixman"
    }

    # Meson names static archives lib*.a even with MSVC. They contain COFF
    # objects built with the matching CRT and can be passed directly to link.exe.
    $cairoArchives = @("$cairo\lib\libcairo.a", "$cairo\lib\libpixman-1.a")
    foreach ($archive in $cairoArchives) {
        if (!(Test-Path -LiteralPath $archive -PathType Leaf)) {
            throw "Cairo build did not install the static archive: $archive"
        }
    }
    $cairoLibs = (($cairoArchives | ForEach-Object { '"' + $_ + '"' }) -join ' ') + ' gdi32.lib msimg32.lib'
    $rbcArgs += @('CAIRO=1', 'CAIRO_STATIC=1', "CAIRODIR=$cairo", "CAIRO_LIBS=$cairoLibs")
} else {
    $rbcArgs += 'CAIRO=0'
}
# Tcl 9.1 derives BUILDDIRTOP from OUT_DIR's final component. With RBC's
# separate output directory, the default source-tree library paths would
# incorrectly point into Tcl/Tk's win/rbc-build directories. Keep source
# headers (RBC needs private APIs), but link the installed Tcl/Tk libraries.
$runtimeLibraries = [ordered]@{
    TCLSTUBLIB = 'tclstub.lib'
    TCLIMPLIB = 'tcl91.lib'
    TKSTUBLIB = 'tkstub.lib'
    TKIMPLIB = 'tcl9tk91.lib'
}
foreach ($entry in $runtimeLibraries.GetEnumerator()) {
    $library = Join-Path $prefix "lib/$($entry.Value)"
    if (!(Test-Path -LiteralPath $library -PathType Leaf)) {
        throw "Tcl/Tk installation is missing the library: $library"
    }
    $rbcArgs += "$($entry.Key)=$library"
}
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
        # An empty staging prefix has no include/tcl.h, so Tcl's Nmake
        # rules do not append /lib as they do for an installed Tcl prefix.
        Invoke-Nmake @rbcArgs "INSTALLDIR=$stage\lib" `
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
