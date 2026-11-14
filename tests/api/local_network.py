"""Network boundary for the local-only API suite."""

import ipaddress
import socket

import httpx
import pytest


def require_loopback(host):
    if isinstance(host, bytes):
        host = host.decode("ascii")
    if host == "localhost":
        return
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        raise RuntimeError(f"API tests forbid non-loopback host: {host!r}") from None
    if not address.is_loopback:
        raise RuntimeError(f"API tests forbid non-loopback host: {host!r}")


def local_api_url(value):
    try:
        url = httpx.URL(value)
        if url.scheme != "http" or url.username or url.password:
            raise ValueError("Use an HTTP loopback URL without credentials")
        require_loopback(url.host)
        if url.path not in ("", "/") or url.query or url.fragment:
            raise ValueError("Use only the local server origin, without a path, query, or fragment")
    except (ValueError, RuntimeError, httpx.InvalidURL) as error:
        raise pytest.UsageError(f"Invalid --api-url: {error}") from error
    return str(url)


def install_network_guard(monkeypatch):
    original_getaddrinfo = socket.getaddrinfo
    original_connect = socket.socket.connect
    original_connect_ex = socket.socket.connect_ex
    original_sendto = socket.socket.sendto

    def getaddrinfo(host, *args, **kwargs):
        require_loopback(host)
        results = original_getaddrinfo(host, *args, **kwargs)
        for _, _, _, _, address in results:
            require_loopback(address[0])
        return results

    def check_address(sock, address):
        if sock.family not in (socket.AF_INET, socket.AF_INET6):
            raise RuntimeError("API tests allow only loopback TCP/UDP sockets")
        require_loopback(address[0])

    def connect(sock, address):
        check_address(sock, address)
        return original_connect(sock, address)

    def connect_ex(sock, address):
        check_address(sock, address)
        return original_connect_ex(sock, address)

    def sendto(sock, data, *args):
        check_address(sock, args[-1])
        return original_sendto(sock, data, *args)

    monkeypatch.setattr(socket, "getaddrinfo", getaddrinfo)
    monkeypatch.setattr(socket.socket, "connect", connect)
    monkeypatch.setattr(socket.socket, "connect_ex", connect_ex)
    monkeypatch.setattr(socket.socket, "sendto", sendto)
