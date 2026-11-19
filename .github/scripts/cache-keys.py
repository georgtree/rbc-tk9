#!/usr/bin/env python3
"""Fingerprint dependency inputs without tying caches to RBC commits."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def output(*command):
    return subprocess.check_output(command, text=True).strip()


def msys_packages(cairo=False):
    """Fingerprint build inputs, not unrelated packages in the MSYS2 image."""
    names = ['bash', 'make']
    names += ['mingw-w64-ucrt-x86_64-' + name for name in (
        'gcc', 'binutils', 'headers', 'crt', 'cc-libs', 'libgcc', 'libstdc++',
        'libiconv', 'winpthreads', 'libwinpthread', 'zlib',
    )]
    if cairo:
        names += ['mingw-w64-ucrt-x86_64-' + name for name in
                  ('python', 'meson', 'ninja', 'pkgconf')]
    return output('pacman', '-Q', *names)


def linux_packages():
    """Track the compiler, system headers and libraries used by the Linux build."""
    gcc_major = output('gcc', '-dumpversion').split('.')[0]
    return output(
        'dpkg-query', '-W', 'binutils', 'make', 'pkgconf', f'gcc-{gcc_major}',
        f'libgcc-{gcc_major}-dev', 'libc6-dev', 'linux-libc-dev', 'zlib1g-dev',
        'libx11-dev', 'libxext-dev', 'libxrender-dev', 'libxft-dev', 'libxss-dev',
        'libxrandr-dev', 'libxcursor-dev', 'libxi-dev', 'x11proto-dev',
        'libxcb1-dev', 'libfontconfig-dev', 'libfreetype-dev', 'libfribidi-dev',
        'libharfbuzz-dev',
    )


def main():
    toolchain = sys.argv[1]
    root = Path(__file__).resolve().parents[2]
    if toolchain == 'windows-msvc':
        compiler = {name: os.environ[name] for name in
                    ('VCToolsVersion', 'WindowsSDKVersion')}
        packages = ''
        recipe = 'build-msvc.ps1'
    else:
        compiler = output('gcc', '--version')
        compiler += '\n' + output('gcc', '-dumpmachine')
        # MSYS2 updates independently of the runner image. Only changes to
        # build inputs should invalidate its dependency caches.
        packages = (msys_packages() if toolchain == 'windows-ucrt64'
                    else linux_packages())
        recipe = 'build-tea.sh'
    # Deliberately omit RBC_CI_RENDERER: both RBC variants use the same runtime.
    common = {
        'toolchain': toolchain,
        'arch': os.environ['RUNNER_ARCH'],
        'image_os': os.environ.get('ImageOS'),
        'compiler': compiler,
        'packages': packages,
        # Tcl config files and Cairo pkg-config files contain absolute paths.
        'workspace': str(root),
    }

    def key(kind, sources, files):
        inputs = dict(common)
        if toolchain == 'windows-ucrt64' and kind == 'cairo':
            inputs['packages'] = msys_packages(cairo=True)
        inputs['sources'] = {name: output('git', '-C', str(root / '_ci' / name),
                                         'rev-parse', 'HEAD') for name in sources}
        inputs['recipes'] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                             for name in files}
        digest = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
        return f'rbc-{kind}-v1-{toolchain}-{digest}'

    action = '.github/actions/build-rbc/action.yml'
    helper = '.github/scripts/cache-keys.py'
    runtime = key('runtime', ('tcl', 'tk', 'argparse'),
                  (action, helper, f'.github/scripts/{recipe}'))
    cairo = key('cairo', (), (action, helper, '.github/scripts/build-cairo.py'))
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
        stream.write(f'runtime={runtime}\ncairo={cairo}\n')


if __name__ == '__main__':
    main()
