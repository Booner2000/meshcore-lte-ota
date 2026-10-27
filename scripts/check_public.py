"""Conservative publication check for this source-only repository."""
from pathlib import Path
import re
import subprocess
from urllib.parse import urlsplit

root = Path(__file__).resolve().parents[1]
names = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root).decode().split('\0')
for name in sorted(set(filter(None, names))):
    path = root / name
    if not path.is_file():
        continue
    if path.suffix.lower() in {'.pem', '.key', '.bin', '.elf', '.db', '.sqlite', '.log'}:
        raise SystemExit('Non-source artifact: ' + name)
    if path.name in {'TrustedKey.h', 'TrustAnchor.h', 'LTEDeploymentConfig.h'}:
        raise SystemExit('Operator configuration: ' + name)
    text = path.read_text(encoding='utf-8')
    patterns = [r'-----BE' + r'GIN [^\n]*PRIVATE KEY', r'github_' + r'pat_', r'gh[pousr]_[A-Za-z0-9]{20,}',
                r'(?i)\b[0-9a-f]{64}\b', r'(?i)[A-Z]:[\\/]Users[\\/]', r'/' + r'home/[^/\s]+/']
    if any(re.search(pattern, text) for pattern in patterns):
        raise SystemExit('Review potential private material: ' + name)
    for url in re.findall(r'https?://[^\s"<>]+', text):
        host = urlsplit(url.rstrip(').,')).hostname or ''
        if host not in {'', 'github.com', 'docs.espressif.com', 'files.waveshare.com', '192.0.2.1', '::1'} and not host.endswith('.invalid'):
            raise SystemExit('Review non-example service address: ' + name)
print('PASS: publication source, configuration and address checks')
