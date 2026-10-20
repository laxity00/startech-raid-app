"""Build-time only: bind service to the binary compiled in this image."""
import hashlib
from pathlib import Path
binary = Path('/usr/local/bin/startech-raid-status')
service = Path('/app/server.py')
source = service.read_text(encoding='utf-8')
assert source.count('BUILD_BACKEND_SHA256') == 1
service.write_text(source.replace('BUILD_BACKEND_SHA256', hashlib.sha256(binary.read_bytes()).hexdigest()))
