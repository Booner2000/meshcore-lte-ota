"""Prepare a pinned upstream checkout. Never flashes hardware."""
import json
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
pin = json.loads((root / 'upstream.json').read_text())
target = root / 'build' / 'MeshCore'
if target.exists():
    raise SystemExit('build/MeshCore already exists; use a fresh checkout directory.')
module = root / 'lib' / 'LTEUpdate'
for name in ('LTEDeploymentConfig.h', 'TrustAnchor.h'):
    if not (module / 'src' / name).is_file():
        raise SystemExit('Configure lib/LTEUpdate/src/' + name + ' from its example first.')
target.parent.mkdir(exist_ok=True)
subprocess.run(['git', 'clone', '--no-checkout', pin['repository'], str(target)], check=True)
subprocess.run(['git', 'checkout', '--detach', pin['commit']], cwd=target, check=True)
subprocess.run(['git', 'apply', '--check', str(root / 'integration/meshcore.patch')], cwd=target, check=True)
subprocess.run(['git', 'apply', str(root / 'integration/meshcore.patch')], cwd=target, check=True)
shutil.copytree(module, target / 'lib/LTEUpdate')
variant = target / 'variants/xiao_s3_wio_lte'
variant.mkdir()
shutil.copy2(root / 'integration/platformio.ini', variant / 'platformio.ini')
print('Prepared pinned source. Build separately with PlatformIO; no upload is performed.')
