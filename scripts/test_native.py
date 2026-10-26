"""Build/run portable C++ tests with an existing compiler; no package dependencies.

Usage: python scripts/test_lte_phase1.py --compiler g++
       python scripts/test_lte_phase1.py --compiler /path/to/zig --zig
"""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default='g++')
parser.add_argument('--zig', action='store_true')
args = parser.parse_args()
build = root / '.pio' / 'lte-host-tests'
build.mkdir(parents=True, exist_ok=True)
module = root / 'lib' / 'LTEUpdate' / 'src'
exe = build / ('test_phase1.exe' if os.name == 'nt' else 'test_phase1')
sources = ['LTEUpdateTypes.cpp', 'A7670Transport.cpp', 'ModemPower.cpp', 'LTEUpdateManager.cpp']
command = [args.compiler] + (['c++'] if args.zig else []) + [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(module),
    str(root / 'tests' / 'lte_phase1' / 'test_phase1.cpp'),
    *[str(module / source) for source in sources], '-o', str(exe)]
subprocess.run(command, cwd=root, check=True)
subprocess.run([str(exe)], cwd=root, check=True)

# Phase-1 isolation boundary: no direct flash/boot/restart API in the new module.
for source in module.glob('*'):
    text = source.read_text(encoding='utf-8')
    for forbidden in ('esp_ota_begin(', 'esp_ota_write(', 'esp_ota_set_boot_partition(',
                      'esp_partition_erase_range(', 'esp_restart(', 'Update.begin(', 'SPIFFS.format('):
        assert forbidden not in text, f'Forbidden Phase-1 operation: {source}: {forbidden}'
print('PASS: Phase-1 module contains no flash/boot/reboot operations')
