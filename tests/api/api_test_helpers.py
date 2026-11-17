"""Shared HTTP assertions and read-only API cases."""

READ_ENDPOINTS = [
    ("/health", dict, {"status": "ok"}),
    ("/auth/verify", dict, {"authenticated": True}),
    ("/bandwidth", list, {}),
    ("/db/stats", dict, {}),
    ("/db/stats/sunburst", dict, {"name": "root"}),
    ("/clusters/list", list, {}),
    ("/download_sessions", list, {}),
    ("/download_sessions/options", dict, {}),
    ("/download_sessions/parsers", dict, {}),
    ("/downloader/cache", dict, {}),
    ("/downloader/hosts", list, {}),
    ("/downloader/secrets", dict, {}),
    ("/embeddings/models", list, {}),
    ("/jobs/status", dict, {}),
    ("/mime", dict, {}),
    ("/rate_limits", list, {}),
    ("/subscription_groups", list, {}),
    ("/subscriptions", list, {}),
    ("/tags/domain/list", list, {}),
    ("/hyapi/api_version", dict, {"is_idhan_instance": True}),
    ("/hyapi/get_services", dict, {}),
    ("/hyapi/client_info", dict, {}),
    ("/hyapi/manage_pages/get_pages", dict, {}),
    ("/hyapi/manage_popups/get_popups", dict, {}),
    ("/hyapi/manage_database/get_client_options", dict, {}),
]


def json_response(response, status=200):
    assert response.status_code == status, response.text
    assert response.headers["content-type"].split(";", 1)[0] == "application/json"
    response.content.decode("utf-8", errors="strict")
    return response.json()


def assert_read_response(response, root_type, expected):
    body = json_response(response)
    assert isinstance(body, root_type)
    for key, value in expected.items():
        assert body[key] == value
    return body


def wait_for_value(read, expected, timeout=10):
    """Wait for state maintained by a background job, retaining the last value."""
    import time

    deadline = time.monotonic() + timeout
    while True:
        value = read()
        if value == expected:
            return value
        if time.monotonic() >= deadline:
            raise AssertionError(f"Expected {expected!r}, last observed {value!r}")
        time.sleep(0.25)


def make_test_png():
    """Return a unique valid 2x2 RGBA PNG without third-party image libraries."""
    import struct
    import zlib
    from uuid import uuid4

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    pixels = uuid4().bytes
    payload = b"\x89PNG\r\n\x1a\n"
    payload += chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
    payload += chunk(b"IDAT", zlib.compress(b"\x00" + pixels[:8] + b"\x00" + pixels[8:]))
    return payload + chunk(b"IEND", b"")
