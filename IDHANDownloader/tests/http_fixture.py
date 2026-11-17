"""Serve files as HTTP response bodies for downloader tests."""

from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import mimetypes
import threading


class _Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.server.requests is not None:
            self.server.requests.append(self.path)

        path = self.server.routes.get(self.path)
        if path is None:
            self.send_error(404)
            return

        body = Path(path).read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', mimetypes.guess_type(str(path))[0] or 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


@contextmanager
def http_fixture(routes, requests=None):
    """Map URL paths to local files and yield the temporary server's base URL."""
    with ThreadingHTTPServer(('127.0.0.1', 0), _Handler) as server:
        server.routes = routes
        server.requests = requests
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield f'http://127.0.0.1:{server.server_address[1]}'
        finally:
            server.shutdown()
            thread.join()
