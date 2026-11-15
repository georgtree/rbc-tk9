#!/usr/bin/env python3
"""Reject shared Cairo/Pixman or MinGW runtimes in the RBC artifact."""
from pathlib import Path
import re
import subprocess
import sys


def main():
    toolchain, directory = sys.argv[1:]
    pattern = '*rbc*.so' if toolchain == 'linux-gcc' else '*rbc*.dll'
    libraries = [p for p in Path(directory).glob(pattern) if 'test' not in p.name]
    if not libraries:
        raise RuntimeError(f'No RBC shared library in {directory}')
    for library in libraries:
        if toolchain == 'windows-msvc':
            command = ['dumpbin', '/dependents', str(library)]
        elif toolchain == 'windows-ucrt64':
            command = ['objdump', '-p', str(library)]
        else:
            command = ['readelf', '-d', str(library)]
        output = subprocess.check_output(command, text=True)
        if toolchain == 'linux-gcc':
            imports = re.findall(r'\(NEEDED\).*?\[(.*?)\]', output)
        elif toolchain == 'windows-ucrt64':
            imports = re.findall(r'DLL Name:\s*(\S+)', output)
        else:
            imports = re.findall(r'^\s+(\S+\.dll)\s*$', output, re.M | re.I)
        if not imports:
            raise RuntimeError(f'Could not read imports from {library}')
        print(f'{library.name}: {", ".join(imports)}')
        forbidden = ('cairo', 'pixman', 'libgcc', 'libstdc++', 'libwinpthread')
        if any(any(name in item.lower() for name in forbidden) for item in imports):
            raise RuntimeError(f'Unexpected shared dependency in {library}')


if __name__ == '__main__':
    main()
