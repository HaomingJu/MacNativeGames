#!/usr/bin/env python3
"""Reproduce the native (AArch64) rewrite of Paper Mario: The Thousand-Year Door's base modules.

Runs the local d2r rewriter (third_party/d2r/patch_nso.py) on every module of the verified ExeFS with
the 'native' ruleset (no game-specific cuts; Paper Mario's build ID matches none of the rewriter's
registered cuts). Output goes to a private directory on a disk with enough free space.

Usage:
    python3 rewrite.py <exefs-dir> --out <private-dir>
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
TOOL = GAME / 'third_party' / 'd2r' / 'patch_nso.py'
MODULES = ('main', 'sdk', 'subsdk0', 'rtld')


def sha256(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def verify_exefs(exefs):
    manifest = json.loads((exefs.parent / 'extraction-manifest.json').read_text())
    assert manifest['program']['title_id'] == '0100ECD018EBE000', 'manifest is not the supported base game'
    hashes = {item['name']: item['sha256'] for item in manifest['files']}
    pinned = json.loads((GAME / 'assets/expected-v1.0.0.json').read_text())['exefs_sha256']
    assert hashes == pinned, '不是已验证的 Paper Mario 1.0.0 本体模块'
    for name in (*MODULES, 'main.npdm'):
        assert sha256(exefs / name) == hashes.get(name), f'extracted input differs from manifest: {name}'
    return hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exefs', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    exefs, out = args.exefs.resolve(), args.out.resolve()
    expected = verify_exefs(exefs)
    out.mkdir(parents=True, exist_ok=True)

    report = {'title_id': '0100ECD018EBE000', 'tool_sha256': sha256(TOOL), 'modules': {}}
    for module in MODULES:
        result = subprocess.run([sys.executable, str(TOOL), str(exefs / module), str(out), module],
                                env=dict(os.environ, DM_RULESET='native', DM_PRIVATE_GATES='0'))
        entry = {'input_sha256': expected[module], 'exit_code': result.returncode}
        if result.returncode == 0:
            entry['outputs'] = {suffix: sha256(out / f'{module}.{suffix}')
                                for suffix in ('patched.bin', 'fixups.tsv', 'gates.tsv', 'sites.tsv', 'cuts.tsv', 'obs.tsv', 'params.tsv')}
        report['modules'][module] = entry
        print(f'{module}: {"converted" if result.returncode == 0 else "failed"}', flush=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(m['exit_code'] for m in report['modules'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
