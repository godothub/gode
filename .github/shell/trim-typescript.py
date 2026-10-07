#!/usr/bin/env python3
"""Keep the embedded compiler, standard library declarations, and licenses."""
import argparse
from pathlib import Path


def trim(package):
    package = Path(package)
    required = ('package.json', 'lib/typescript.js', 'LICENSE.txt', 'ThirdPartyNoticeText.txt')
    for name in required:
        if not (package / name).is_file():
            raise FileNotFoundError(f'Missing TypeScript package file: {package / name}')
    for path in package.rglob('*'):
        if not path.is_file():
            continue
        name = path.relative_to(package).as_posix()
        if name in required or (path.parent == package / 'lib' and path.name.startswith('lib.') and path.name.endswith('.d.ts')):
            continue
        path.unlink()
    for path in sorted(package.rglob('*'), key=lambda item: len(item.parts), reverse=True):
        if path.is_dir() and not any(path.iterdir()):
            path.rmdir()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    trim(parser.parse_args().package)
