import socket

import pytest

from local_network import local_api_url


@pytest.mark.parametrize("value", [
    "http://example.com:16609", "http://192.168.1.40:16609", "http://0.0.0.0:16609",
    "http://169.254.169.254", "http://[::ffff:192.168.1.1]", "https://127.0.0.1:16609",
    "http://user:password@127.0.0.1:16609", "http://127.0.0.1:16609/path",
])
def test_remote_or_ambiguous_api_url_rejected(value):
    with pytest.raises(pytest.UsageError):
        local_api_url(value)


@pytest.mark.parametrize("value", ["http://127.0.0.1:16609", "http://localhost:16609", "http://[::1]:16609"])
def test_loopback_api_url_allowed(value):
    assert local_api_url(value) == value


def test_external_dns_blocked_before_resolution():
    with pytest.raises(RuntimeError, match="non-loopback"):
        socket.getaddrinfo("example.com", 80)


@pytest.mark.parametrize("method", ["connect", "connect_ex"])
def test_external_connections_blocked_before_connect(method):
    with socket.socket() as sock:
        with pytest.raises(RuntimeError, match="non-loopback"):
            getattr(sock, method)(("192.0.2.1", 80))


def test_external_udp_blocked_before_send():
    with socket.socket(type=socket.SOCK_DGRAM) as sock:
        with pytest.raises(RuntimeError, match="non-loopback"):
            sock.sendto(b"blocked", ("192.0.2.1", 80))
