#!/usr/bin/env python3
"""CIA integrity and allowlisted LAN delivery; no console required."""
import contextlib
import functools
import hashlib
import http.server
import importlib.util
import io
from pathlib import Path
import tempfile
import threading
import urllib.error
import urllib.request

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

verify = load("verify_cia", "scripts/verify-cia.py").verify
handler = load("serve_cia", "scripts/serve-cia.py").CiaHandler
cia = Path("dist/RetroSync3DS.cia").resolve()
original = cia.read_bytes()
checks = 0

def check(condition):
    global checks
    checks += 1
    assert condition

with contextlib.redirect_stdout(io.StringIO()):
    verify(cia)
check(True)
with tempfile.TemporaryDirectory() as directory:
    for payload in (original[:64], original[:-1], original[:65536] + bytes([original[65536] ^ 1]) + original[65537:]):
        corrupt = Path(directory) / "corrupt.cia"
        corrupt.write_bytes(payload)
        try:
            verify(corrupt)
        except ValueError:
            check(True)
        else:
            check(False)

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(handler, cia=cia))
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = f"http://127.0.0.1:{server.server_port}"
try:
    with urllib.request.urlopen(base + "/RetroSync3DS.cia") as response:
        check(response.headers["Content-Type"] == "application/octet-stream")
        check(int(response.headers["Content-Length"]) == len(original))
        check(hashlib.sha256(response.read()).digest() == hashlib.sha256(original).digest())
    request = urllib.request.Request(base + "/RetroSync3DS.cia", method="HEAD")
    with urllib.request.urlopen(request) as response:
        check(int(response.headers["Content-Length"]) == len(original) and response.read() == b"")
    for path in ("/", "/config/settings.ini", "/../README.md", "/%2e%2e/README.md"):
        try:
            urllib.request.urlopen(base + path)
        except urllib.error.HTTPError as error:
            check(error.code == 404)
        else:
            check(False)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
print(f"PASS: {checks} CIA integrity and LAN serving checks (no console install)")
