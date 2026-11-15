#!/usr/bin/env python3
"""Build only the Cairo surfaces RBC uses, with static PIC dependencies."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
CI = ROOT / '_ci'
SOURCES = (
    ('cairo', '1.18.4', 'tar.xz',
     '445ed8208a6e4823de1226a74ca319d3600e83f6369f99b14265006599c32ccb'),
    ('pixman', '0.44.2', 'tar.gz',
     '6349061ce1a338ab6952b92194d1b0377472244208d47ff25bef86fc71973466'),
)


def meson(*args):
    subprocess.run([sys.executable, '-m', 'mesonbuild.mesonmain', *args], check=True)


def main():
    CI.mkdir(exist_ok=True)
    for name, version, extension, checksum in SOURCES:
        archive = CI / f'{name}-{version}.{extension}'
        if not archive.exists():
            urllib.request.urlretrieve(
                f'https://cairographics.org/releases/{archive.name}', archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
            raise RuntimeError(f'Checksum mismatch: {archive}')
        with tarfile.open(archive) as source:
            source.extractall(CI, filter='data')
    cairo = CI / 'cairo-1.18.4'
    pixman = cairo / 'subprojects' / 'pixman'
    shutil.copytree(CI / 'pixman-0.44.2', pixman, dirs_exist_ok=True)
    # Cairo's bundled wrap names an older release; use our verified source tree.
    (cairo / 'subprojects' / 'pixman.wrap').unlink(missing_ok=True)
    prefix = CI / 'cairo-static'
    build = CI / 'cairo-build'
    # No optional PNG, font, GLib, compression or export dependency chain.
    # RBC uses Cairo for paths/images; Tk draws text and RBC owns its exporters.
    options = [
        '--buildtype=release', '--default-library=static', '--libdir=lib',
        '--auto-features=disabled', '--force-fallback-for=pixman', '--wrap-mode=nodownload',
        '-Db_staticpic=true', '-Dtests=disabled', '-Dgtk_doc=false',
        '-Dpixman:tests=disabled', '-Dpixman:demos=disabled',
        '-Dpixman:sse2=enabled',
    ]
    if os.name == 'nt':
        # Cairo 1.18.4 targets Windows 2000 without DirectWrite, but its
        # Win32 font code uses ClearType constants hidden by that SDK target.
        # Raise the target in Meson itself to avoid conflicting /D definitions.
        meson_file = cairo / 'meson.build'
        meson_file.write_text(meson_file.read_text().replace(
            '_WIN32_WINNT_WIN2K', '_WIN32_WINNT_WIN7'))
        if not os.environ.get('MSYSTEM'):
            options += ['-Db_vscrt=md']
    else:
        options += ['-Dxlib=enabled']
        # This Xlib source uses FC_* constants even with Cairo's Fontconfig
        # backend disabled. Include the constants' header only in that file;
        # no Fontconfig calls are enabled and no extra library is linked.
        screen = cairo / 'src' / 'cairo-xlib-screen.c'
        screen.write_text(screen.read_text().replace(
            '#include "cairo-fontconfig-private.h"',
            '#include "cairo-fontconfig-private.h"\n#include <fontconfig/fontconfig.h>', 1))
    meson('setup', str(build), str(cairo), f'--prefix={prefix}', *options)
    meson('compile', '-C', str(build), '-j', '2')
    meson('install', '-C', str(build))
    notices = CI / 'notices'
    notices.mkdir(exist_ok=True)
    for source, name in [(cairo / 'COPYING', 'cairo-COPYING'),
                        (cairo / 'COPYING-LGPL-2.1', 'cairo-COPYING-LGPL-2.1'),
                        (cairo / 'COPYING-MPL-1.1', 'cairo-COPYING-MPL-1.1'),
                        (pixman / 'COPYING', 'pixman-COPYING')]:
        shutil.copy2(source, notices / name)
    if os.name == 'nt':
        shutil.copy2(meson_file, notices / 'cairo-meson.build')
    else:
        # Supply the modified MPL/LGPL source with the distributed binary.
        shutil.copy2(screen, notices / 'cairo-xlib-screen.c')
    (notices / 'BUILD.txt').write_text(
        'Cairo 1.18.4 and Pixman 0.44.2 are statically linked into RBC.\n'
        'Sources: https://cairographics.org/releases/\n'
        'Build recipe: .github/scripts/build-cairo.py in the RBC repository.\n'
        'Linux adds the Fontconfig constants header in cairo-xlib-screen.c;\n'
        'Windows raises Cairo\'s non-DirectWrite SDK target to Windows 7.\n'
        'The modified source/build file for this platform is supplied here.\n',
        encoding='utf-8')


if __name__ == '__main__':
    main()
