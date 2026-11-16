#!/usr/bin/env python3
"""Add LineageOS libbase_shim to pinned M2381 blobs, preserving stock inputs.

Uses the same DT_NEEDED fixup as upstream extract_utils.blob_fixup.add_needed.
Only separate vendor/compat copies and their packaging rules are changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        temporary.chmod(0o644)
        temporary.replace(path)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def configure(aosp, device, vendor, apply=False):
    manifest = json.loads((device / 'configs/compat/libbase-fixups.json').read_text())
    tool = aosp / 'prebuilts/extract-tools/linux-x86/bin' / manifest['patchelf']
    needed = manifest['library']
    makefile = vendor / 'Android.mk'
    text = makefile.read_text()
    plan = []
    # Validate all inputs, output hashes and packaging ownership before writing.
    for entry in manifest['files']:
        relative = Path(entry['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'Invalid blob path: {relative}')
        source, output = vendor / 'proprietary' / relative, vendor / 'compat' / relative
        if sha256(source) != entry['sha256']:
            raise ValueError(f'Unknown original blob: {source}')
        if output.exists() and sha256(output) != entry['fixed_sha256']:
            raise ValueError(f'Conflicting compatibility copy: {output}')
        before = f'LOCAL_SRC_FILES := proprietary/{relative.as_posix()}\n'
        after = f'LOCAL_SRC_FILES := compat/{relative.as_posix()}\n'
        if text.count(before) + text.count(after) != 1:
            raise ValueError(f'Expected exactly one install rule: {relative}')
        if not apply and (not output.exists() or after not in text):
            raise ValueError(f'Blob compatibility missing: {relative}; run with --apply')
        plan.append((entry, source, output))
        text = text.replace(before, after)
    if apply:
        # Generate and check all outputs in temporary storage before installing any.
        with tempfile.TemporaryDirectory(prefix='m2381-libbase-') as temporary:
            outputs = []
            for index, (entry, source, output) in enumerate(plan):
                fixed = Path(temporary) / str(index)
                fixed.write_bytes(source.read_bytes())
                subprocess.run([str(tool), '--add-needed', needed, str(fixed)], check=True)
                if sha256(fixed) != entry['fixed_sha256']:
                    raise ValueError(f'Unexpected patchelf output: {source}')
                dependencies = subprocess.check_output([str(tool), '--print-needed', str(fixed)], text=True).splitlines()
                if dependencies.count(needed) != 1:
                    raise ValueError(f'Expected exactly one {needed} dependency: {source}')
                outputs.append((output, fixed.read_bytes()))
            for output, data in outputs:
                if not output.exists():
                    atomic_write(output, data)
            if makefile.read_text() != text:
                atomic_write(makefile, text.encode())
    print(f'LineageOS libbase shim: verified {len(plan)} pinned blob fixups')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aosp', type=Path, required=True)
    parser.add_argument('--device', type=Path)
    parser.add_argument('--vendor', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    configure(args.aosp, args.device or args.aosp / 'device/meizu/m2381',
              args.vendor or args.aosp / 'vendor/meizu/m2381', args.apply)


if __name__ == '__main__':
    main()
