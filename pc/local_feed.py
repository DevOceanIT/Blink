"""Authenticated LAN snapshot feed for paired Blink displays.

The board receives the same bounded usage frame as USB. Authentication is
per-board HMAC; provider credentials and provider APIs never enter this path.
"""
import hashlib
import hmac
import http.server
import json
import os
import re
import secrets
import stat
import threading
import time
from collections import OrderedDict

from pc import protocol

PATH = "/v1/usage"
PORT = 8765
MAX_NONCES = 256
NONCE_RE = re.compile(r"^[0-9a-f]{32,64}$")


def request_mac(key: bytes, nonce: str) -> str:
    """MAC the fixed endpoint and nonce so credentials cannot cross routes."""
    message = ("GET\n" + PATH + "\n" + nonce).encode("ascii")
    return hmac.new(key, message, hashlib.sha256).hexdigest()


def response_mac(key: bytes, nonce: str, body: bytes) -> str:
    return hmac.new(key, nonce.encode("ascii") + b"\n" + body,
            hashlib.sha256).hexdigest()


def pairing_message(host: str, port: int, key: bytes) -> dict:
    """USB command the WiFi firmware accepts exactly once per board."""
    import ipaddress

    address = ipaddress.ip_address(host)
    if address.version != 4:
        raise ValueError("feed host must be an IPv4 address")
    if not isinstance(port, int) or not 1 <= port <= 65535:
        raise ValueError("feed port must be between 1 and 65535")
    if not isinstance(key, bytes) or len(key) != 32:
        raise ValueError("feed key must be exactly 32 bytes")
    from pc.version import PROTO_VERSION
    return {"t": "feed_pair", "v": PROTO_VERSION, "host": str(address),
            "port": port, "key_hex": key.hex()}


def verify_response(key: bytes, nonce: str, echoed_nonce: str,
                    body: bytes, supplied_mac: str) -> bool:
    """Verify nonce echo and exact response bytes before parsing the JSON."""
    return (hmac.compare_digest(nonce, echoed_nonce) and
            hmac.compare_digest(response_mac(key, nonce, body), supplied_mac))


def read_key(path):
    """Read a 32-byte key encoded as 64 lowercase/uppercase hex characters."""
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags)
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or (os.name == "posix" and
                                          info.st_mode & 0o077):
        os.close(fd)
        raise PermissionError("feed key file must be private and regular")
    with os.fdopen(fd, "r", encoding="ascii") as stream:
        value = stream.read().strip()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", value):
        raise ValueError("feed key must be 32 bytes encoded as 64 hex characters")
    return bytes.fromhex(value)


def create_key_file(path, key=None):
    """Create one private, no-overwrite 256-bit key file; return its bytes."""
    key = key or secrets.token_bytes(32)
    if not isinstance(key, bytes) or len(key) != 32:
        raise ValueError("feed key must be exactly 32 bytes")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="ascii") as stream:
            stream.write(key.hex() + "\n")
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        try:
            os.unlink(path)
        except OSError:
            pass
        raise
    return key


class LockedFetch:
    """Serialize provider reads shared by the USB loop and LAN requests."""

    def __init__(self, fetch):
        self._fetch = fetch
        self._lock = threading.Lock()
        self._local = threading.local()

    def __call__(self):
        with self._lock:
            result = self._fetch()
            accessor = getattr(self._fetch, "session_pair", None)
            self._local.pair = tuple(accessor()) if accessor else None
            return result

    @property
    def session_pair(self):
        return self._session_pair

    def _session_pair(self):
        return getattr(self._local, "pair", None) or ("", 0)


class FeedServer:
    """Small threaded server; fetch is called only for authenticated requests."""

    def __init__(self, fetch, key, host="0.0.0.0", port=8765,
                 clock=time.monotonic):
        if not isinstance(key, bytes) or len(key) != 32:
            raise ValueError("feed key must be exactly 32 bytes")
        self._fetch = fetch
        self._key = key
        self._clock = clock
        self._seen = OrderedDict()
        self._lock = threading.Lock()
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            server_version = "BlinkFeed/1"
            sys_version = ""

            def log_message(self, _fmt, *_args):
                # Request headers and URLs are not useful in the service log.
                return

            def _reply(self, status, body=b"", mac=None, nonce=None):
                self.send_response(status)
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                if mac:
                    self.send_header("X-Blink-Auth", mac)
                if nonce:
                    self.send_header("X-Blink-Nonce", nonce)
                self.end_headers()
                if body:
                    self.wfile.write(body)

            def do_GET(self):
                if self.path != PATH:
                    self._reply(404)
                    return
                nonce = self.headers.get("X-Blink-Nonce", "")
                supplied = self.headers.get("X-Blink-Auth", "")
                if not NONCE_RE.fullmatch(nonce) or not hmac.compare_digest(
                        request_mac(owner._key, nonce), supplied):
                    self._reply(401)
                    return
                with owner._lock:
                    now = owner._clock()
                    while owner._seen and next(iter(owner._seen.values())) < now - 120:
                        owner._seen.popitem(last=False)
                    if nonce in owner._seen:
                        self._reply(409)
                        return
                    owner._seen[nonce] = now
                    while len(owner._seen) > MAX_NONCES:
                        owner._seen.popitem(last=False)
                try:
                    msg = owner._fetch()
                    if msg is None:
                        self._reply(503)
                        return
                    wire, why = protocol.encode_checked(msg)
                    if why:
                        self._reply(503)
                        return
                    usage = protocol.decode(wire.rstrip(b"\n").decode("utf-8"))
                    # The checked NDJSON is the shared contract with USB.
                    # Preserve its source-age field as a first-class value.
                    age = usage.get("age_s", -1)
                    body = json.dumps({"usage": usage, "age_s": age},
                                      separators=(",", ":"),
                                      ensure_ascii=False).encode("utf-8")
                except Exception:
                    self._reply(503)
                    return
                self._reply(200, body,
                            response_mac(owner._key, nonce, body), nonce)

        self._httpd = http.server.ThreadingHTTPServer((host, port), Handler)
        self._httpd.daemon_threads = True
        self._thread = None

    @property
    def address(self):
        return self._httpd.server_address

    def serve_forever(self):
        self._httpd.serve_forever(poll_interval=0.25)

    def start(self):
        if self._thread is None:
            self._thread = threading.Thread(target=self.serve_forever,
                                            name="blink-local-feed", daemon=True)
            self._thread.start()
        return self

    def close(self):
        self._httpd.shutdown()
        self._httpd.server_close()
        if self._thread is not None:
            self._thread.join(timeout=2)
            self._thread = None
