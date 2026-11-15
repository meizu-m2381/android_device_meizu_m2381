#!/usr/bin/env python3
"""Export current repo HEADs without requiring archived HAL worktrees to exist."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def export(root, output, exclude):
    env = dict(os.environ, REPO_TRACE='0')
    raw = subprocess.check_output(
        [sys.executable, '.repo/repo/repo', 'manifest', '-o', '-'], cwd=root, env=env)
    tree = ET.fromstring(raw)
    default = tree.find('default')
    default_revision = default.get('revision') if default is not None else None
    remotes = {r.get('name'): r.get('revision') for r in tree.findall('remote')}
    pattern = re.compile(exclude)
    projects = tree.findall('project')
    if not projects or any(p.findall('project') for p in projects):
        raise ValueError('Expected a nonempty, flat repo manifest')

    def resolve(project):
        path = Path(project.get('path', project.attrib['name']))
        if path.is_absolute() or '..' in path.parts:
            raise ValueError(f'Unsafe project path: {path}')
        gitdir = root / '.repo/projects' / (str(path) + '.git')
        # repo keeps project Git metadata here even when its worktree is archived.
        revision = subprocess.check_output(
            ['git', '--git-dir=' + str(gitdir), 'rev-parse', '--verify', 'HEAD^{commit}'],
            cwd=root, text=True).strip()
        if not re.fullmatch(r'[0-9a-f]{40}', revision):
            raise ValueError(f'Invalid HEAD for {path}')
        return project, revision

    # Resolve all projects before publishing anything; no tree moves or network use.
    with ThreadPoolExecutor(max_workers=8) as pool:
        resolved = list(pool.map(resolve, projects))
    for project, revision in resolved:
        remote = project.get('remote', default.get('remote') if default is not None else None)
        original = project.get('revision') or remotes.get(remote) or default_revision
        project.set('revision', revision)
        if original and original != revision:
            if not project.get('upstream'):
                project.set('upstream', original)
            if not project.get('dest-branch'):
                project.set('dest-branch', original)
        if pattern.search(ET.tostring(project, encoding='unicode')):
            tree.remove(project)
    ET.indent(tree, space='  ')
    data = ET.tostring(tree, encoding='utf-8', xml_declaration=True) + b'\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + '.', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        temporary.chmod(0o644)
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f'Exported {len(tree.findall("project"))} pinned projects to {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exclude', default='proprietary_')
    args = parser.parse_args()
    export(args.root.resolve(strict=True), args.output, args.exclude)
