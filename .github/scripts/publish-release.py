#!/usr/bin/env python3
"""Publish the six tested packages to the rolling 'latest' release."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def run(*args):
    subprocess.run(args, check=True)


def api(path):
    return json.loads(subprocess.check_output(['gh', 'api', path], text=True))


def latest_release(repository):
    # The tag endpoint only finds published releases. Include drafts so a
    # failed first publication can resume on the next manual run.
    pages = json.loads(subprocess.check_output(
        ['gh', 'api', f'repos/{repository}/releases', '--paginate', '--slurp'], text=True))
    return next((release for page in pages for release in page
                 if release['tag_name'] == 'latest'), None)


def packages(directory, sha):
    """Validate the complete matrix and its embedded build provenance."""
    pattern = re.compile(
        r'rbc(?P<version>[0-9.]+)-(?P<toolchain>linux-gcc|windows-ucrt64|windows-msvc)'
        r'-tcl9\.1\.0-(?P<variant>static-cairo|native)\.(?P<extension>tar\.gz|zip)')
    expected = {(toolchain, variant) for toolchain in
                ('linux-gcc', 'windows-ucrt64', 'windows-msvc')
                for variant in ('static-cairo', 'native')}
    found = set()
    versions = set()
    revisions = set()
    cairo_versions = set()
    archives = sorted(path for path in directory.iterdir() if path.name != 'SHA256SUMS.txt')
    for archive in archives:
        match = pattern.fullmatch(archive.name)
        if not match or not archive.is_file():
            raise ValueError(f'Unexpected release artifact: {archive.name}')
        version, toolchain, variant, extension = match.groups()
        pair = toolchain, variant
        if pair in found or extension != ('tar.gz' if toolchain == 'linux-gcc' else 'zip'):
            raise ValueError(f'Duplicate or invalid package: {archive.name}')
        found.add(pair)
        versions.add(version)
        manifest = f'share/rbc{version}/third-party/BUILD.txt'
        if extension == 'zip':
            with zipfile.ZipFile(archive) as source:
                text = source.read(manifest).decode('utf-8')
        else:
            with tarfile.open(archive) as source:
                text = source.extractfile('./' + manifest).read().decode('utf-8')
        lines = text.splitlines()
        if f'rbc: {sha}' not in lines or f'Renderer build: {variant}' not in lines:
            raise ValueError(f'Package provenance does not match this run: {archive.name}')
        deps = tuple(next(line for line in lines if line.startswith(name + ': '))
                     for name in ('tcl', 'tk', 'argparse'))
        revisions.add(deps)
        if variant == 'static-cairo':
            cairo_versions.add(next(line for line in lines if line.startswith('Cairo ')))
    if found != expected or len(versions) != 1 or len(revisions) != 1 or len(cairo_versions) != 1:
        raise ValueError('Expected six matching packages from one dependency/source revision')
    return archives, versions.pop(), revisions.pop(), cairo_versions.pop()


def main():
    repository = os.environ['GITHUB_REPOSITORY']
    sha = os.environ['GITHUB_SHA']
    directory = Path(sys.argv[1]).resolve()
    archives, version, revisions, cairo = packages(directory, sha)
    release = latest_release(repository)
    if release and release.get('immutable'):
        raise RuntimeError('The latest release is immutable; rolling releases must remain mutable')
    checksums = directory / 'SHA256SUMS.txt'
    checksums.write_text(''.join(
        f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n'
        for path in archives), encoding='utf-8')
    server = os.environ.get('GITHUB_SERVER_URL', 'https://github.com')
    run_url = f'{server}/{repository}/actions/runs/{os.environ["GITHUB_RUN_ID"]}'
    notes = (
        f'Rolling stable build of RBC {version}. Updated after all six builds and tests passed.\n\n'
        f'Commit: [{sha}]({server}/{repository}/commit/{sha})\n\n'
        f'Built by: [Build packages]({run_url})\n\n'
        f'Published: {datetime.now(timezone.utc).isoformat(timespec="seconds")}\n\n'
        'Linux GCC, Windows UCRT64 and Windows MSVC packages are supplied in both '
        '`static-cairo` and `native` variants. Both UCRT64 variants use static winpthreads.\n\n'
        'Use a matching Tcl/Tk 9.1.0 installation and Windows toolchain. '
        'Tcl/Tk is not bundled; argparse is included.\n\n'
        f'{cairo}\n\nDependency source revisions:\n\n```text\n'
        + '\n'.join(revisions) + '\n```\n\n'
        'This release and the `latest` tag are replaced on subsequent successful manual builds.\n')
    with tempfile.TemporaryDirectory() as temporary:
        notes_file = Path(temporary) / 'notes.md'
        notes_file.write_text(notes, encoding='utf-8')
        if release is None:
            run('gh', 'release', 'create', 'latest', '--repo', repository,
                '--target', sha, '--draft', '--title', 'Latest stable',
                '--notes-file', str(notes_file))
            release = latest_release(repository)
        assets = [*archives, checksums]
        run('gh', 'release', 'upload', 'latest', '--repo', repository,
            '--clobber', *(str(path) for path in assets))
        # Only remove old assets after every replacement has uploaded.
        release = api(f'repos/{repository}/releases/{release["id"]}')
        keep = {path.name for path in assets}
        for asset in release['assets']:
            if asset['name'] not in keep:
                run('gh', 'api', '--method', 'DELETE',
                    f'repos/{repository}/releases/assets/{asset["id"]}')
        run('git', 'push', 'origin', f'{sha}:refs/tags/latest', '--force')
        run('gh', 'release', 'edit', 'latest', '--repo', repository,
            '--draft=false', '--prerelease=false', '--latest', '--title', 'Latest stable',
            '--notes-file', str(notes_file))
    print(f'Published {server}/{repository}/releases/tag/latest')


if __name__ == '__main__':
    main()
