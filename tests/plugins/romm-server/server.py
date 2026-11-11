"""A stand-in RomM for the romm plugin's case: the responses recorded from
RomM 5.4.1 in tests/c/fixtures/romm, roms and saves made up to the sizes
those name, and uploads written down, on 127.0.0.1. With --tls it speaks
HTTPS with the given certificate and key.

  python3 -I server.py --fixtures DIR --port-file FILE --log FILE [--tls CERT KEY]

It writes the port it got to --port-file, and one line per request to
--log: the method, the path, and for an upload the file name and bytes.
Rom 1 is sent slowly, about 6 seconds, so a download can be cancelled.
PlayStation's roms have several files each, sent as a zip as RomM's nginx
sends one (stored, sizes after each file, with RomM's own <name>.m3u), and
rom 3 has the state recorded for rom 2 renamed as RetroArch names one.
"""

import argparse
import base64
import http.server
import io
import json
import os
import socketserver
import ssl
import sys
import time
import urllib.parse
import zipfile

USER, PASSWORD = "admin", "Pw-86-secret"


def content(seed, size):
    line = ("%s " % seed).encode()
    return (line * (size // len(line) + 1))[:size]


class Stream:
    """A file zipfile can't seek in, so it writes sizes after each file
    (data descriptors), as nginx's mod_zip does."""

    def __init__(self):
        self.buf = io.BytesIO()

    def write(self, data):
        return self.buf.write(data)

    def flush(self):
        pass


def rom_zip(rom, name):
    """The zip RomM serves for a rom of several files asked for as name."""
    top = rom["full_path"] + "/"
    stream = Stream()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_STORED) as z:
        inside = []
        for f in sorted(rom["files"], key=lambda f: f["file_name"]):
            path = f["full_path"][len(top):]
            inside.append(path)
            z.writestr(path, content("%s %s" % (rom["id"], path), f["file_size_bytes"]))
        # RomM's playlist: every file but a sheet's tracks
        sheets = any(p.lower().endswith(".cue") for p in inside)
        discs = [p for p in inside if not (sheets and p.lower().endswith(".bin"))]
        z.writestr(name + ".m3u", "\n".join(discs))
    return stream.buf.getvalue()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def note(self, text):
        with open(self.server.log_path, "a") as f:
            f.write(text + "\n")

    def send(self, status, body, kind="application/json", chunked=False):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
        else:
            self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if chunked:
            for i in range(0, len(body), 700):
                part = body[i:i + 700]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.wfile.write(body)
        self.close_connection = True

    def fixture(self, name):
        with open(os.path.join(self.server.fixtures, name), "rb") as f:
            return f.read()

    def rom3_states(self):
        """Rom 2's state as rom 3's (state 31), named as RetroArch names slot 1."""
        state = json.loads(self.fixture("states-rom2.json"))[0]
        state.update(id=31, rom_id=3, file_name="Mosaic Two Disc (USA).state1", emulator="pcsx_rearmed")
        return [state]

    def authorised(self):
        want = "Basic " + base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
        if self.headers.get("Authorization") == want:
            return True
        self.send(401, b'{"detail":"Unauthorized"}')
        return False

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        path = urllib.parse.unquote(url.path)
        self.note("GET %s" % self.path)
        if not self.authorised():
            return
        if path == "/api/platforms":
            # chunked, as RomM's nginx sends a large list
            return self.send(200, self.fixture("platforms.json"), chunked=True)
        if path == "/api/roms":
            if query.get("platform_ids") == ["1"]:
                return self.send(200, self.fixture("roms-gb.json"))
            if query.get("platform_ids") == ["4"]:
                return self.send(200, self.fixture("roms-psx.json"))
            return self.send(200, b'{"items":[],"total":0,"limit":500,"offset":0}')
        if path.startswith("/api/roms/") and "/content/" in path:
            rom_id = int(path.split("/")[3])
            roms = json.loads(self.fixture("roms-gb.json"))["items"] + json.loads(self.fixture("roms-psx.json"))["items"]
            rom = next((r for r in roms if r["id"] == rom_id), None)
            name = path.split("/content/", 1)[1]
            if not rom or name != rom["fs_name"]:
                return self.send(404, b"{}")
            if len(rom["files"]) > 1:
                return self.send(200, rom_zip(rom, name), "application/zip")
            body = content("rom %d" % rom_id, rom["fs_size_bytes"])
            if rom_id != 1:
                return self.send(200, body, "application/octet-stream")
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                for i in range(0, len(body), 512):
                    self.wfile.write(body[i:i + 512])
                    self.wfile.flush()
                    time.sleep(0.1)
            except OSError:
                self.note("rom 1 download stopped")
            self.close_connection = True
            return
        if path in ("/api/saves", "/api/states"):
            name = "saves-rom2.json" if path == "/api/saves" else "states-rom2.json"
            if query.get("rom_id") == ["2"]:
                return self.send(200, self.fixture(name))
            if query.get("rom_id") == ["3"] and path == "/api/states":
                return self.send(200, json.dumps(self.rom3_states()).encode())
            return self.send(200, b"[]")
        if (path.startswith("/api/saves/") or path.startswith("/api/states/")) and path.endswith("/content"):
            kind = path.split("/")[2]
            asset_id = int(path.split("/")[3])
            assets = json.loads(self.fixture("saves-rom2.json" if kind == "saves" else "states-rom2.json"))
            if kind == "states":
                assets += self.rom3_states()
            asset = next((a for a in assets if a["id"] == asset_id), None)
            if not asset:
                return self.send(404, b"{}")
            return self.send(200, content("%s %d" % (kind, asset_id), asset["file_size_bytes"]), "application/octet-stream")
        self.send(404, b"{}")

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        self.note("POST %s" % self.path)
        if not self.authorised():
            return
        if url.path != "/api/saves":
            return self.send(404, b"{}")
        kind = self.headers.get("Content-Type", "")
        boundary = kind.split("boundary=", 1)[1].encode() if "boundary=" in kind else b""
        parts = body.split(b"--" + boundary)
        name, data = "", b""
        for part in parts:
            head, _, rest = part.partition(b"\r\n\r\n")
            if b'name="saveFile"' in head:
                name = head.split(b'filename="', 1)[1].split(b'"', 1)[0].decode()
                data = rest[:-2] if rest.endswith(b"\r\n") else rest
        self.note("UPLOAD %s %s" % (name, data.decode(errors="replace")))
        query = urllib.parse.parse_qs(url.query)
        reply = {"id": 9, "rom_id": int(query.get("rom_id", ["0"])[0]), "file_name": name,
                 "file_size_bytes": len(data), "updated_at": "2026-11-09T09:00:00+00:00"}
        self.send(200, json.dumps(reply).encode())


class Server(http.server.ThreadingHTTPServer):
    def server_bind(self):
        # HTTPServer's looks up the host's name, which can hang without DNS
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--fixtures", required=True)
    p.add_argument("--port-file", required=True)
    p.add_argument("--log", required=True)
    p.add_argument("--tls", nargs=2)
    a = p.parse_args()
    server = Server(("127.0.0.1", 0), Handler)
    server.fixtures = a.fixtures
    server.log_path = a.log
    if a.tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(a.tls[0], a.tls[1])
        server.socket = context.wrap_socket(server.socket, server_side=True)
    with open(a.port_file + ".tmp", "w") as f:
        f.write(str(server.server_address[1]))
    os.replace(a.port_file + ".tmp", a.port_file)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    sys.exit(0)


if __name__ == "__main__":
    main()
