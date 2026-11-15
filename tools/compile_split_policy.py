#!/usr/bin/env python3
"""Reproduce init's split SELinux compilation without changing any input."""
import argparse
import json
from pathlib import Path
import subprocess
import os
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--secilc', type=Path, required=True)
    for part in ('system', 'system_ext', 'product', 'vendor', 'odm'):
        parser.add_argument('--' + part.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    roots = {part: getattr(args, part) / 'etc/selinux'
             for part in ('system', 'system_ext', 'product', 'vendor', 'odm')}
    version = (roots['vendor'] / 'plat_sepolicy_vers.txt').read_text().strip()
    # init's SEPOLICY_VERSION is 30; platform policy API (e.g. 33.0) is separate.
    command = [str(args.secilc.resolve()),
               str(roots['system'] / 'plat_sepolicy.cil'),
               '-m', '-M', 'true', '-G', '-N', '-c', '30',
               str(roots['system'] / 'mapping' / (version + '.cil')),
               '-o', str(args.output / 'policy'), '-f', '/dev/null']
    optional = [
        roots['system'] / 'mapping' / (version + '.compat.cil'),
        roots['system_ext'] / 'system_ext_sepolicy.cil',
        roots['system_ext'] / 'mapping' / (version + '.cil'),
        roots['system_ext'] / 'mapping' / (version + '.compat.cil'),
        roots['product'] / 'product_sepolicy.cil',
        roots['product'] / 'mapping' / (version + '.cil'),
    ]
    command += [str(path) for path in optional if path.is_file()]
    for part, name in [('vendor', 'plat_pub_versioned.cil'),
                       ('vendor', 'vendor_sepolicy.cil')]:
        path = roots[part] / name
        if not path.is_file():
            raise FileNotFoundError(path)
        command.append(str(path))
    odm = roots['odm'] / 'odm_sepolicy.cil'
    if odm.is_file():
        command.append(str(odm))
    genfs_file = roots['vendor'] / 'genfs_labels_version.txt'
    # libselinux defaults to 202404 when the vendor version file is absent.
    genfs_version = genfs_file.read_text().strip() if genfs_file.is_file() else '202404'
    genfs = roots['system'] / ('plat_sepolicy_genfs_' + genfs_version + '.cil')
    if genfs.is_file():
        command.append(str(genfs))
    descriptor, temporary = tempfile.mkstemp(prefix='policy-', dir=args.output)
    os.close(descriptor)
    command[command.index('-o') + 1] = temporary
    try:
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            os.replace(temporary, args.output / 'policy')
    finally:
        Path(temporary).unlink(missing_ok=True)
    (args.output / 'compile.log').write_text(result.stdout + result.stderr)
    manifest = {'vendor_api': version, 'genfs_version': genfs_version,
                'command': command, 'returncode': result.returncode}
    (args.output / 'command.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))
    print((result.stdout + result.stderr)[:8000])
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
