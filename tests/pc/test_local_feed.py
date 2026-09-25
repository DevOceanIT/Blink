import http.client
import json
import threading

from pc import local_feed, protocol


KEY = bytes(range(32))


def request(address, nonce, key=KEY, path=local_feed.PATH):
    conn = http.client.HTTPConnection(*address, timeout=2)
    mac = local_feed.request_mac(key, nonce) if key else "bad"
    conn.request("GET", path, headers={"X-Blink-Nonce": nonce,
                                        "X-Blink-Auth": mac})
    response = conn.getresponse()
    body = response.read()
    headers = dict(response.getheaders())
    conn.close()
    return response.status, headers, body


def usage():
    return protocol.usage(21.5, None, 43.0, None, [], age_s=37)


def test_feed_authenticates_frame_and_binds_response_to_nonce():
    calls = []
    feed = local_feed.FeedServer(lambda: calls.append(1) or usage(), KEY,
                                 host="127.0.0.1", port=0).start()
    try:
        nonce = "a" * 32
        status, headers, body = request(feed.address, nonce)
        assert status == 200
        assert local_feed.verify_response(
            KEY, nonce, headers["X-Blink-Nonce"], body,
            headers["X-Blink-Auth"])
        assert not local_feed.verify_response(
            KEY, nonce, "e" * 32, body, headers["X-Blink-Auth"])
        payload = json.loads(body)
        assert payload["usage"] == usage()
        assert payload["age_s"] == 37
        assert len(protocol.encode_checked(payload["usage"])[0]) <= 512
        assert calls == [1]
    finally:
        feed.close()


def test_feed_rejects_bad_auth_replay_and_unknown_path_without_polling():
    calls = []
    feed = local_feed.FeedServer(lambda: calls.append(1) or usage(), KEY,
                                 host="127.0.0.1", port=0).start()
    try:
        nonce = "b" * 32
        status, _, _ = request(feed.address, nonce, key=None)
        assert status == 401
        status, _, _ = request(feed.address, nonce)
        assert status == 200
        status, _, _ = request(feed.address, nonce)
        assert status == 409
        status, _, _ = request(feed.address, "c" * 32, path="/other")
        assert status == 404
        assert calls == [1]
    finally:
        feed.close()


def test_feed_rejects_bad_key_and_unavailable_source():
    import pytest

    with pytest.raises(ValueError):
        local_feed.FeedServer(lambda: None, b"short", host="127.0.0.1", port=0)

    feed = local_feed.FeedServer(lambda: None, KEY,
                                 host="127.0.0.1", port=0).start()
    try:
        status, headers, _ = request(feed.address, "d" * 32)
        assert status == 503
        assert "X-Blink-Auth" not in headers
    finally:
        feed.close()


def test_key_file_must_be_256_bit_hex(tmp_path):
    path = tmp_path / "feed.key"
    path.write_text(KEY.hex(), encoding="ascii")
    path.chmod(0o600)
    assert local_feed.read_key(path) == KEY
    path.write_text("00", encoding="ascii")
    path.chmod(0o600)
    import pytest
    with pytest.raises(ValueError):
        local_feed.read_key(path)
    path.write_text(KEY.hex(), encoding="ascii")
    path.chmod(0o644)
    with pytest.raises(PermissionError):
        local_feed.read_key(path)


def test_pairing_command_and_private_key_creation(tmp_path):
    import os
    import pytest

    path = tmp_path / "feed.key"
    key = local_feed.create_key_file(path, KEY)
    assert key == KEY
    assert path.read_text(encoding="ascii") == KEY.hex() + "\n"
    if os.name == "posix":
        assert path.stat().st_mode & 0o777 == 0o600
    with pytest.raises(FileExistsError):
        local_feed.create_key_file(path, bytes(reversed(range(32))))

    pair = local_feed.pairing_message("192.168.1.50", 8765, KEY)
    assert pair["t"] == "feed_pair"
    assert pair["host"] == "192.168.1.50"
    assert pair["port"] == 8765
    assert pair["key_hex"] == KEY.hex()
    with pytest.raises(ValueError):
        local_feed.pairing_message("example.local", 8765, KEY)


def test_locked_fetch_serializes_provider_reads():
    entered = threading.Event()
    release = threading.Event()
    count = []

    def fetch():
        count.append(1)
        entered.set()
        release.wait(1)
        return usage()

    shared = local_feed.LockedFetch(fetch)
    first = threading.Thread(target=shared)
    second = threading.Thread(target=shared)
    first.start()
    assert entered.wait(1)
    second.start()
    release.set()
    first.join(1)
    second.join(1)
    assert len(count) == 2
