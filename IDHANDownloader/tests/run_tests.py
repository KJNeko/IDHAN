"""Run the GTest downloader suite against the local file server."""

import os
from pathlib import Path
import subprocess
import sys

from http_fixture import http_fixture


def main():
    fixtures = Path(__file__).resolve().parent / 'fixtures'
    requests = []
    url = 'http://example.com/4.png'
    with http_fixture({url: fixtures / '4.png'}, requests=requests) as proxy:
        environment = {key: value for key, value in os.environ.items()
                       if not key.lower().endswith('_proxy')}
        environment.update(http_proxy=proxy, no_proxy='',
                           IDHAN_DOWNLOADER_FIXTURES=str(fixtures))
        result = subprocess.run(sys.argv[1:], env=environment, timeout=30)
        if result.returncode:
            return result.returncode
        if requests != [url]:
            print(f'Expected one image request, received {requests!r}', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
