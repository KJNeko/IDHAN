from pathlib import Path
import tempfile
import unittest
import urllib.error
import urllib.request

from http_fixture import http_fixture


class FixtureTests(unittest.TestCase):
    def test_file_body(self):
        with tempfile.TemporaryDirectory() as directory:
            body_file = Path(directory) / 'body.bin'
            body_file.write_bytes(b'\x00\xffgarbage')
            with http_fixture({'/file': body_file}) as url:
                with urllib.request.urlopen(url + '/file', timeout=2) as response:
                    self.assertEqual(response.read(), body_file.read_bytes())

    def test_unknown_path(self):
        with http_fixture({}) as url:
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(url + '/missing', timeout=2)
            self.assertEqual(error.exception.code, 404)
            error.exception.close()


if __name__ == '__main__':
    unittest.main()
