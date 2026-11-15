#!/usr/bin/env bash
set -euo pipefail

export RBC_CI_RENDERER=${RBC_CI_RENDERER:-static-cairo}
case $RBC_CI_RENDERER in
    static-cairo|native) ;;
    *) echo "Unknown renderer variant: $RBC_CI_RENDERER" >&2; exit 1 ;;
esac

root=$(pwd)
prefix="$root/_ci/runtime"
platform=unix
toolchain=linux-gcc
static_runtime=()
if [[ ${MSYSTEM:-} == UCRT64 ]]; then
    platform=win
    toolchain=windows-ucrt64
    static_runtime=(LDFLAGS=-static-libgcc)
fi
native_path() {
    if [[ $platform == win ]]; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
mkdir -p _ci/{tcl-build,tk-build,argparse-build,rbc-build}
export PATH="$prefix/bin:$PATH"
export LD_LIBRARY_PATH="$prefix/lib:${LD_LIBRARY_PATH:-}"

if [[ ${RUNTIME_CACHE_HIT:-false} != true ]]; then
    cd "$root/_ci/tcl-build"
    bash "$root/_ci/tcl/$platform/configure" --prefix="$prefix" "${static_runtime[@]}"
    make -j2
    make install
    cd "$root/_ci/tk-build"
    bash "$root/_ci/tk/$platform/configure" --with-tcl="$prefix/lib" --prefix="$prefix" "${static_runtime[@]}"
    make -j2
    make install

    cd "$root/_ci/argparse-build"
    bash "$root/_ci/argparse/configure" --with-tcl="$prefix/lib" \
        --prefix="$prefix" --exec-prefix="$prefix" "${static_runtime[@]}"
    make -j2
    make install
else
    echo "Using cached Tcl/Tk and argparse runtime"
fi

cd "$root"
cairo_flags=(--disable-cairo)
if [[ $RBC_CI_RENDERER == static-cairo ]]; then
    if [[ ${CAIRO_CACHE_HIT:-false} != true ]]; then
        python .github/scripts/build-cairo.py
    else
        echo "Using cached static Cairo and Pixman"
    fi
    export PKG_CONFIG_PATH="$root/_ci/cairo-static/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    cairo_flags=(--enable-cairo-static)
fi
cd "$root/_ci/rbc-build"
# Static winpthreads is independent of the optional Cairo renderer.
if [[ $platform == win ]]; then cairo_flags+=(--enable-winpthreads-static); fi
bash "$root/configure" --with-tcl="$prefix/lib" --with-tk="$prefix/lib" \
    --prefix="$prefix" --exec-prefix="$prefix" "${cairo_flags[@]}"
make -j2

# Keep the build package first, and install argparse where Tcl finds it normally.
export TCLLIBPATH="{$(native_path "$PWD")} {$(native_path "$root/_ci/tcllib/modules")}"
tclsh="$prefix/bin/tclsh9.1"
if [[ $platform == win ]]; then tclsh="$prefix/bin/tclsh91.exe"; fi
if [[ $platform == unix ]]; then
    # Stable mapped GUI windows and focus without a physical display.
    xvfb-run -a -s '-screen 0 1920x1080x24 -dpi 96' \
        "$tclsh" "$root/.github/scripts/smoke.tcl"
    xvfb-run -a -s '-screen 0 1920x1080x24 -dpi 96' make test TCLLIBPATH="$TCLLIBPATH"
else
    "$tclsh" "$(native_path "$root/.github/scripts/smoke.tcl")"
    make test TCLLIBPATH="$TCLLIBPATH"
fi
make test-headers TCLLIBPATH="$TCLLIBPATH"

# Verify the link really is static; don't silently ship dependency DLLs.
python "$root/.github/scripts/check-linkage.py" "$toolchain" "$PWD"
if [[ ${MAKE_DIST:-false} == true ]]; then
    make dist DIST_ROOT="$root/_ci/dist"
    stage=$(find "$root/_ci/dist" -mindepth 1 -maxdepth 1 -type d -name 'rbc*' -print -quit)
    # Ship the separately loadable argparse package beside RBC.
    for package in "$prefix/lib/"argparse*; do cp -R "$package" "$stage/lib/"; done
    export TCLLIBPATH="{$(native_path "$stage/lib")}"
    if [[ $platform == unix ]]; then
        xvfb-run -a "$tclsh" "$root/.github/scripts/smoke.tcl"
    else
        "$tclsh" "$(native_path "$root/.github/scripts/smoke.tcl")"
    fi
    python "$root/.github/scripts/package.py" "$toolchain" "$stage"
fi
