#!/usr/bin/env python3
"""Loopback integration tests. No live RomM, credentials, games or save files."""
import contextlib
import http.server
import importlib.util
import json
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

PROBE = str(pathlib.Path(sys.argv[1]).resolve())
checks = 0
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        requests.append((self.path, self.headers.get("Authorization")))
        if self.headers.get("Authorization") != "Bearer rmm_mock_token":
            self.send_error(401)
            return
        codes = {"/api/unauthorized": 401, "/api/forbidden": 403,
                 "/api/absent": 404, "/api/method": 405, "/api/busy": 503}
        if self.path in codes:
            self.send_response(codes[self.path])
            self.end_headers()
            self.wfile.write(b"PRIVATE_SERVER_BODY_MUST_NOT_APPEAR")
            return
        if self.path == "/api/redirect":
            self.send_response(302)
            self.send_header("Location", "/api/credential-leak")
            self.end_headers()
            return
        self.send_response(200)
        self.end_headers()
        try:
            if self.path == "/api/large":
                self.wfile.write(b"X" * (512 * 1024 + 1))
            elif self.path == "/api/slow":
                for _ in range(40):
                    self.wfile.write(b"x" * 1024)
                    self.wfile.flush()
                    time.sleep(0.05)
            else:
                self.wfile.write(b'{"SYSTEM":{"VERSION":"5.4.0"}}')
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass


@contextlib.contextmanager
def server(tls=None, handler=Handler):
    s = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    s.daemon_threads = True
    if tls:
        s.socket = tls.wrap_socket(s.socket, server_side=True)
    thread = threading.Thread(target=s.serve_forever, daemon=True)
    thread.start()
    try:
        yield s.server_port
    finally:
        s.shutdown()
        s.server_close()
        thread.join()


def check(condition, message):
    global checks
    checks += 1
    assert condition, message


def probe(url, path, ca=None, cancel=False):
    cmd = [PROBE, url, path]
    if ca is not None or cancel:
        cmd.append(str(ca or ""))
    if cancel:
        cmd.append("cancel")
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=8, check=True)
    status, error, body = run.stdout.split("\n", 2)
    return int(status), error, body


with server() as port:
    url = f"http://127.0.0.1:{port}"
    status, error, body = probe(url, "/api/heartbeat")
    check(status == 200 and not error and '"VERSION":"5.4.0"' in body, "HTTP happy path")
    check(requests[-1][1] == "Bearer rmm_mock_token", "Bearer header")
    for path, code, expected in [("unauthorized", 401, "Authentication"),
                                 ("forbidden", 403, "Permission"),
                                 ("absent", 404, "unavailable"),
                                 ("method", 405, "unavailable"),
                                 ("busy", 503, "503")]:
        status, error, body = probe(url, f"/api/{path}")
        check(status == code and expected in error and not body, f"HTTP {code} and response redaction")
    status, error, body = probe(url, "/api/redirect")
    check(status == 302 and "Redirect refused" in error and not body, "No redirect following")
    check(not any(p == "/api/credential-leak" for p, _ in requests), "No credential redirect leak")
    status, error, body = probe(url, "/api/large")
    check("512 KiB" in error and not body, "Bounded response memory")
    start = time.monotonic()
    status, error, body = probe(url, "/api/slow", cancel=True)
    check("cancelled" in error and not body and time.monotonic() - start < 4, "Cancellation")
    status, error, body = probe(url, "//attacker.example/api/heartbeat")
    check("Invalid API path" in error, "Absolute API path injection rejected")

with tempfile.TemporaryDirectory(prefix="retrosync-tls-") as temp:
    root = pathlib.Path(temp)
    cert, key, ca = root / "cert.pem", root / "key.pem", root / "ca.pem"
    ca_key, csr, extensions = root / "ca.key", root / "server.csr", root / "server.ext"
    # A real CA/leaf chain avoids TLS-backend-specific self-signed leaf rules.
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(ca_key), "-out", str(ca), "-days", "1",
                    "-subj", "/CN=RetroSync test CA",
                    "-addext", "basicConstraints=critical,CA:TRUE",
                    "-addext", "keyUsage=critical,keyCertSign,cRLSign"],
                   check=True, capture_output=True)
    subprocess.run(["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(key), "-out", str(csr), "-subj", "/CN=localhost"],
                   check=True, capture_output=True)
    extensions.write_text("basicConstraints=critical,CA:FALSE\n"
                          "keyUsage=critical,digitalSignature,keyEncipherment\n"
                          "extendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n")
    subprocess.run(["openssl", "x509", "-req", "-in", str(csr), "-CA", str(ca),
                    "-CAkey", str(ca_key), "-CAcreateserial", "-days", "1",
                    "-extfile", str(extensions), "-out", str(cert)],
                   check=True, capture_output=True)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(cert, key)
    with server(tls) as port:
        status, error, body = probe(f"https://localhost:{port}", "/api/heartbeat", ca)
        check(status == 200 and not error and body, f"HTTPS with explicit trusted CA: status={status}, error={error}")
        status, error, body = probe(f"https://localhost:{port}", "/api/heartbeat")
        check("certificate verification failed" in error and not body, "Unknown CA fails closed")
        status, error, body = probe(f"https://127.0.0.1:{port}", "/api/heartbeat", ca)
        check("certificate verification failed" in error and not body, "Hostname mismatch fails closed")
        status, error, body = probe(f"https://localhost:{port}", "/api/heartbeat", pathlib.Path(temp) / "missing.pem")
        check("CA file missing or invalid" in error and not body, "Missing CA fails closed")

with socket.socket() as s:
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
status, error, body = probe(f"http://127.0.0.1:{port}", "/api/heartbeat")
check(status == 0 and "Network:" in error and not body, "Disconnected server")
spec = importlib.util.spec_from_file_location("mock_romm", "scripts/mock_romm.py")
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)
with server(handler=mock.Handler) as port:
    for path, expected in [("/api/platforms", 2),
                           ("/api/roms?platform_ids=1&limit=20&offset=0", 20),
                           ("/api/roms?platform_ids=1&limit=20&offset=20", 20),
                           ("/api/roms?platform_ids=1&limit=20&offset=40", 5),
                           ("/api/roms?platform_ids=1&search_term=04", 1),
                           ("/api/roms?platform_ids=2", 0),
                           ("/api/saves?rom_id=100", 2)]:
        status, error, body = probe(f"http://127.0.0.1:{port}", path)
        data = json.loads(body)
        check(status == 200 and not error and len(data["items"] if isinstance(data, dict) else data) == expected,
              f"Interactive mock catalog: {path}")
print(f"PASS: {checks} network checks (loopback mock HTTP/HTTPS; desktop curl)")
