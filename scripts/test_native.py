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
for source in [module / name for name in sources]:
    text = source.read_text(encoding='utf-8')
    for forbidden in ('esp_ota_begin(', 'esp_ota_write(', 'esp_ota_set_boot_partition(',
                      'esp_partition_erase_range(', 'esp_restart(', 'Update.begin(', 'SPIFFS.format('):
        assert forbidden not in text, f'Forbidden Phase-1 operation: {source}: {forbidden}'
print('PASS: portable manager has no direct flash/boot/reboot operations')
install_exe = build / ('test_install.exe' if os.name == 'nt' else 'test_install')
install_command = [args.compiler] + (['c++'] if args.zig else []) + [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-DLTE_REAL_OTA=1', '-I', str(module),
    str(root / 'tests/lte_install/test_install.cpp'),
    *[str(module / source) for source in sources], '-o', str(install_exe)]
subprocess.run(install_command, cwd=root, check=True)
subprocess.run([str(install_exe)], cwd=root, check=True)

http_exe = build / ('test_http.exe' if os.name == 'nt' else 'test_http')
http_command = [args.compiler] + (['c++'] if args.zig else []) + [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(module),
    str(root / 'tests/lte_http/test_http.cpp'),
    *[str(module / source) for source in sources], str(module/'A7670BodyReader.cpp'), '-o', str(http_exe)]
subprocess.run(http_command, cwd=root, check=True)
subprocess.run([str(http_exe)], cwd=root, check=True)

endpoint_exe = build / ('test_endpoint.exe' if os.name == 'nt' else 'test_endpoint')
endpoint_command = [args.compiler] + (['c++'] if args.zig else []) + [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(module),
    str(root/'tests/lte_http/test_endpoint.cpp'), str(module/'UpdateEndpoint.cpp'), '-o', str(endpoint_exe)]
subprocess.run(endpoint_command,cwd=root,check=True)
subprocess.run([str(endpoint_exe)],cwd=root,check=True)

# Guard the distinct trust boundaries even when building on a host without ESP APIs.
subprocess.run([__import__("sys").executable, str(root/"scripts/test_update_policy.py")], check=True)
