import json

import pytest


@pytest.mark.parametrize("field", ["idhan_server_version", "idhan_api_version"])
def test_version(api_client, field):
    response = api_client.get("/version")
    assert response.status_code == 200, response.text
    assert response.headers["content-type"].split(";", 1)[0] == "application/json"

    body = response.content.decode("utf-8", errors="strict")
    version = json.loads(body)[field]
    for component in ("major", "minor", "patch"):
        assert type(version[component]) is int
        assert version[component] >= 0

    expected = f"{version['major']}.{version['minor']}.{version['patch']}"
    assert version["string"] in (expected, f"{expected}-tainted")
