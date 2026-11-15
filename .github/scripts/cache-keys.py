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
        # Include system libraries/headers: MSYS2 updates independently of
        # the runner image, and Linux packages can change between image updates.
        packages = (output('pacman', '-Q') if toolchain == 'windows-ucrt64'
                    else output('dpkg-query', '-W'))
        recipe = 'build-tea.sh'
    # Deliberately omit RBC_CI_RENDERER: both RBC variants use the same runtime.
    common = {
        'toolchain': toolchain,
        'arch': os.environ['RUNNER_ARCH'],
        'image': [os.environ.get('ImageOS'), os.environ.get('ImageVersion')],
        'compiler': compiler,
        'packages': packages,
        # Tcl config files and Cairo pkg-config files contain absolute paths.
        'workspace': str(root),
    }

    def key(kind, sources, files):
        inputs = dict(common)
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
