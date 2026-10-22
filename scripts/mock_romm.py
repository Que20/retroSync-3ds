#!/usr/bin/env python3
"""Loopback-only synthetic RomM demo: no real credentials, ROMs, or user saves."""
import argparse
import hashlib
import http.server
import json
from email.parser import BytesParser
from email.policy import default
from urllib.parse import parse_qs, urlsplit

ROM = bytearray(32768)
ROM[0xB2] = 0x96
ROM[0xBD] = (-sum(ROM[0xA0:0xBD]) - 0x19) & 255
ROM[1024:1033] = b"SRAM_V110"
ROM = bytes(ROM)  # Validated test header, not executable game software.
DEVICE = "00000000-0000-0000-0000-000000000001"

def digest(data):
    return hashlib.md5(data).hexdigest()

class Handler(http.server.BaseHTTPRequestHandler):
    data = {99: b"R" * 32768, 98: b"H" * 32768}
    saves = [dict(id=99, rom_id=100, user_id=1, file_name="Synthetic.srm", slot="autosave", emulator="mgba", file_size_bytes=32768, content_hash=digest(data[99]), updated_at="2026-10-22T10:00:00+00:00"),
             dict(id=98, rom_id=100, user_id=1, file_name="Historical.srm", slot=None, emulator=None, file_size_bytes=32768, content_hash=digest(data[98]), updated_at="2026-10-21T10:00:00+00:00")]
    acknowledgements = {}

    def log_message(self, *_):
        pass

    def respond(self, code, data):
        encoded = data if isinstance(data, bytes) else json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/octet-stream" if isinstance(data, bytes) else "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def authenticated(self):
        if self.headers.get("Authorization") in ("Bearer rmm_mock_token", "Bearer demo-access"):
            return True
        self.respond(401, {"detail": "Synthetic demo login required"})
        return False

    def do_GET(self):
        target = urlsplit(self.path)
        query = parse_qs(target.query)
        if target.path == "/api/heartbeat":
            return self.respond(200, {"SYSTEM": {"VERSION": "5.4.0-demo"}})
        if not self.authenticated():
            return
        try:
            if target.path == "/api/users/me":
                return self.respond(200, {"id": 1, "username": "demo"})
            if target.path == "/api/platforms":
                return self.respond(200, [{"id": 1, "slug": "gba", "name": "Game Boy Advance"}, {"id": 2, "slug": "gb", "name": "Game Boy"}])
            if target.path == "/api/roms":
                platform = int(query.get("platform_ids", ["1"])[0])
                offset = max(0, int(query.get("offset", ["0"])[0]))
                limit = max(1, min(20, int(query.get("limit", ["20"])[0])))
                search = query.get("search_term", [""])[0].casefold()
                items = [dict(id=100+i, platform_id=1, name=f"Synthetic Homebrew {i:02}", fs_name=f"Synthetic-{i:02}.gba", fs_size_bytes=len(ROM), summary="Synthetic demo library. Generated test bytes only; no commercial games or real save data.") for i in range(45)] if platform == 1 else []
                items = [item for item in items if search in item["name"].casefold()]
                return self.respond(200, dict(items=items[offset:offset+limit], offset=min(offset, len(items)), total=len(items), limit=limit))
            if target.path.startswith("/api/roms/"):
                parts = target.path.split("/");rid = int(parts[3])
                if not 100 <= rid < 145:
                    return self.respond(404, {})
                if len(parts) > 4 and parts[4] == "content":
                    return self.respond(200, ROM)
                return self.respond(200, dict(id=rid, platform_id=1, files=[dict(id=rid, is_top_level=True, file_name=f"Synthetic-{rid-100:02}.gba", file_size_bytes=len(ROM), md5_hash=digest(ROM))]))
            if target.path.startswith("/api/devices/"):
                return self.respond(200, dict(id=DEVICE, user_id=1, sync_enabled=True))
            if target.path == "/api/saves":
                rid = int(query.get("rom_id", ["100"])[0]);slot = query.get("slot", [None])[0]
                return self.respond(200, [s for s in self.saves if s["rom_id"] == rid and (slot is None or s["slot"] == slot)])
            if target.path.startswith("/api/saves/"):
                rid = int(target.path.split("/")[3]);row = next((s for s in self.saves if s["id"] == rid), None)
                if row is None:
                    return self.respond(404, {})
                return self.respond(200, self.data[rid] if target.path.endswith("/content") else row)
        except (ValueError, IndexError):
            return self.respond(422, {})
        self.respond(404, {"detail": "Unavailable in synthetic fixture"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > 256*1024:
            return self.respond(413, {})
        body = self.rfile.read(length)
        target = urlsplit(self.path);query = parse_qs(target.query)
        if target.path == "/api/token":
            form = parse_qs(body.decode())
            valid = form.get("username") == ["demo"] and form.get("password") == ["demo"]
            valid |= form.get("grant_type") == ["refresh_token"] and form.get("refresh_token") == ["demo-refresh"]
            return self.respond(200 if valid else 401, dict(access_token="demo-access", refresh_token="demo-refresh", token_type="bearer") if valid else {})
        if not self.authenticated():
            return
        if target.path == "/api/devices":
            return self.respond(201, {"device_id": DEVICE})
        if target.path.endswith("/downloaded"):
            rid = int(target.path.split("/")[3]);self.acknowledgements[rid] = json.loads(body)["content_hash"]
            return self.respond(200, next(s for s in self.saves if s["id"] == rid))
        if target.path == "/api/saves":
            rid = int(query["rom_id"][0]);slot = query["slot"][0]
            previous = [s for s in self.saves if s["rom_id"] == rid and s["slot"] == slot]
            latest = max(previous, key=lambda s:(s["updated_at"],s["id"])) if previous else None
            if latest and self.acknowledgements.get(latest["id"]) != latest["content_hash"]:
                return self.respond(409, {})
            message = BytesParser(policy=default).parsebytes(("Content-Type: "+self.headers["Content-Type"]+"\r\n\r\n").encode()+body)
            parts = [part for part in message.iter_parts() if part.get_param("name", header="content-disposition") == "saveFile"]
            if len(parts) != 1:
                return self.respond(422, {})
            data = parts[0].get_payload(decode=True)
            if not data or len(data) > 131072 or digest(data) != query.get("content_hash", [""])[0]:
                return self.respond(422, {})
            sid = max(self.data)+1;row = dict(id=sid, rom_id=rid, user_id=1, slot=slot, emulator=query.get("emulator", [None])[0], file_name=f"Synthetic-{sid}.sav", file_size_bytes=len(data), content_hash=digest(data), updated_at="2026-10-22T11:00:00+00:00")
            self.data[sid]=data;self.saves.append(row);self.acknowledgements[sid]=row["content_hash"]
            return self.respond(200, row)
        self.respond(404, {})

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    print(f"Synthetic RomM fixture on {args.bind}:{args.port}; demo login: demo / demo", flush=True)
    http.server.ThreadingHTTPServer((args.bind, args.port), Handler).serve_forever()
