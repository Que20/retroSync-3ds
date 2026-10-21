#!/usr/bin/env python3
"""Read-only synthetic RomM-shaped server for prototype/hardware browsing tests."""
import argparse
import http.server
import json
from urllib.parse import parse_qs, urlsplit


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass  # Do not log query strings or credentials.

    def respond(self, code, data):
        encoded = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        target = urlsplit(self.path)
        if target.path == "/api/heartbeat":
            self.respond(200, {"SYSTEM": {"VERSION": "5.4.0-mock"}})
            return
        if self.headers.get("Authorization") != "Bearer rmm_mock_token":
            self.respond(401, {"detail": "Mock token required"})
            return
        if target.path == "/api/platforms":
            self.respond(200, [{"id": 1, "slug": "gba", "name": "Game Boy Advance", "display_name": "Game Boy Advance"},
                               {"id": 2, "slug": "gb", "name": "Game Boy", "display_name": "Game Boy"}])
        elif target.path == "/api/roms":
            query = parse_qs(target.query)
            try:
                platform = int(query.get("platform_ids", ["1"])[0])
                offset = max(0, int(query.get("offset", ["0"])[0]))
                limit = max(1, min(20, int(query.get("limit", ["20"])[0])))
            except ValueError:
                self.respond(422, {"detail": "Invalid page"})
                return
            search = query.get("search_term", [""])[0].casefold()
            items = [{"id": 100 + i, "platform_id": platform, "name": f"Synthetic Homebrew {i:02}",
                      "fs_name": f"Synthetic-{i:02}.gba", "fs_size_bytes": 32768,
                      "summary": "Mock metadata. No ROM or save bytes are served."} for i in range(45)] if platform == 1 else []
            items = [item for item in items if search in item["name"].casefold()]
            self.respond(200, {"items": items[offset:offset + limit], "offset": min(offset, len(items)),
                               "total": len(items), "limit": limit})
        elif target.path == "/api/saves":
            self.respond(200, [{"id": 99, "file_name": "Synthetic.srm", "slot": "autosave", "emulator": "mock",
                               "file_size_bytes": 32768, "updated_at": "2026-10-21T10:00:00Z", "content_hash": None},
                              {"id": 98, "file_name": "Historical.srm", "slot": None, "emulator": None, "file_size_bytes": 32768}])
        else:
            self.respond(404, {"detail": "Not implemented by metadata-only mock"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    print(f"Synthetic HTTP metadata server on {args.bind}:{args.port}; token: rmm_mock_token", flush=True)
    http.server.ThreadingHTTPServer((args.bind, args.port), Handler).serve_forever()
