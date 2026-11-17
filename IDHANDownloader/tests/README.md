# Downloader HTTP fixture

Python standard library only. Map endpoint paths to body files:

```python
from http_fixture import http_fixture

with http_fixture({'/file': 'tests/downloader/fixtures/example.txt'}) as url:
    # Run the downloader against url + '/file'.
    pass
```

Edit the file to change what the endpoint sends. Files are read on each request
and sent byte-for-byte as the body of a 200 response with a Content-Length header.
File paths are relative to the process working directory, or can be absolute.
The server binds to loopback on a free port and stops on context exit.
Unknown endpoints return 404. Endpoint paths include query strings.

Run fixture checks from the repository root:

```sh
python3 -m unittest discover -s tests/downloader -v
```

The GTest direct-image test creates `DownloaderContext` directly, with no
command-line tester dependency. A test import sink captures bytes in memory;
GMock verifies that the import opens and finishes once with the expected URL,
filename, MIME type, and size, and is never aborted. The captured bytes must
match `fixtures/4.png` exactly.

A small Python runner starts the file server as a local HTTP proxy for
`http://example.com/4.png` and verifies that it receives exactly one request.
The fixture infers Content-Type from the file extension.

Build and run (adjust the build directory as needed):

```sh
cmake -S . -B build/cmake-check-downloader -DBUILD_IDHAN_TESTS=ON -DBUILD_IDHAN_DOWNLOADER=ON
cmake --build build/cmake-check-downloader --target IDHANDownloaderTests
ctest --test-dir build/cmake-check-downloader -R IDHANDownloader --output-on-failure
```

CTest starts and stops the Python fixture automatically and limits the test to
40 seconds. The test requires Python, GTest, and GMock, but no database.
