import pytest

from api_test_helpers import json_response


@pytest.mark.parametrize("path", [
    "/records/create", "/tags/create", "/tags/domain/create", "/clusters/add",
    "/download_sessions", "/download_sessions/check_url",
    "/subscription_groups", "/downloader/hosts", "/downloader/secrets",
    "/relationships/duplicates/add", "/relationships/alternatives/add",
    "/relationships/unrelated/add", "/relationships/clear",
])
def test_truncated_json_rejected(api_client, path):
    response = api_client.post(path, content=b'{"unfinished":', headers={"content-type": "application/json"})
    assert json_response(response, 400)["error"]


@pytest.mark.parametrize("suffix", ["/urls/add", "/urls/remove"])
def test_record_mutation_rejects_truncated_json(api_client, record, suffix):
    response = api_client.post(f"/records/{record['id']}{suffix}", content=b'{"urls":',
                               headers={"content-type": "application/json"})
    assert json_response(response, 400)["error"]


@pytest.mark.parametrize("path,params", [
    ("/records/search", {"sha256": "invalid"}),
    ("/search", {"by": "not_a_sort_mode"}),
    ("/relationships/duplicates/undecided", {"distance": 9}),
    ("/relationships/duplicates/undecided", {"limit": 33}),
])
def test_invalid_query_parameters(api_client, path, params):
    assert json_response(api_client.get(path, params=params), 400)["error"]
