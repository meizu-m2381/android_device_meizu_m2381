#!/usr/bin/env python3
"""Build a pinned, separately named VNDK33 helper without changing the snapshot."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from configure_blob_compat import atomic_write, sha256


def prepare(aosp, output):
    manifest = Path(__file__).resolve().parents[1] / 'configs/compat/media-helper.json'
    config = json.loads(manifest.read_text())
    source = aosp / config['source']
    if sha256(source) != config['sha256']:
        raise ValueError(f'Unknown VNDK33 media helper: {source}')
    tool = aosp / 'prebuilts/extract-tools/linux-x86/bin/patchelf-0_17_2'
    with tempfile.TemporaryDirectory(prefix='m2381-media-') as temporary:
        fixed = Path(temporary) / config['soname']
        fixed.write_bytes(source.read_bytes())
        subprocess.run([str(tool), '--set-soname', config['soname'],
                        '--add-needed', config['add_needed'], str(fixed)], check=True)
        if sha256(fixed) != config['fixed_sha256']:
            raise ValueError('Unexpected media helper patch output')
        atomic_write(output, fixed.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aosp', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.aosp.resolve(), args.output)


if __name__ == '__main__':
    main()
