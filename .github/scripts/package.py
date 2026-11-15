#!/usr/bin/env python3
"""Archive an installed prefix, including notices for the embedded libraries."""
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main():
    toolchain, stage_arg = sys.argv[1:]
    stage = Path(stage_arg).resolve()
    root = Path(__file__).resolve().parents[2]
    version = re.search(r'AC_INIT\(\[rbc\],\[([^]]+)\]\)',
                        (root / 'configure.ac').read_text())[1]
    if toolchain == 'windows-msvc':
        # The generic Nmake HTML installer predates Sphinx's asset directories.
        docs = stage / 'share' / f'rbc{version}' / 'doc'
        man = stage / 'share' / 'man' / 'mann'
        docs.mkdir(parents=True, exist_ok=True)
        man.mkdir(parents=True, exist_ok=True)
        for pattern in ('*.html', '*.js', '*.inv', '*.n', '.nojekyll'):
            for source in (root / 'docs').glob(pattern):
                shutil.copy2(source, docs / source.name)
                if source.suffix == '.n':
                    shutil.copy2(source, man / source.name)
        for name in ('_static', 'assets', 'images'):
            source = root / 'docs' / name
            if source.is_dir():
                shutil.copytree(source, docs / name, dirs_exist_ok=True)
        shutil.copy2(root / 'license.terms', docs / 'license.terms')
    notices = stage / 'share' / f'rbc{version}' / 'third-party'
    shutil.copytree(root / '_ci' / 'notices', notices, dirs_exist_ok=True)
    argparse = root / '_ci' / 'argparse'
    shutil.copy2(argparse / 'LICENSE', notices / 'argparse-LICENSE')
    revisions = '\n'.join(
        f'{name}: ' + subprocess.check_output(
            ['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
        for name, path in [('rbc', root), ('tcl', root / '_ci/tcl'),
                           ('tk', root / '_ci/tk'), ('argparse', argparse)])
    with (notices / 'BUILD.txt').open('a', encoding='utf-8') as manifest:
        manifest.write(revisions + '\n')
    output = root / '_ci' / 'package'
    output.mkdir(exist_ok=True)
    name = output / f'rbc{version}-{toolchain}-tcl9.1.0-static-cairo'
    archive = shutil.make_archive(str(name), 'gztar' if toolchain == 'linux-gcc' else 'zip', stage)
    print(f'Installation archive: {archive}')


if __name__ == '__main__':
    main()
