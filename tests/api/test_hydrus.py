import pytest

from api_test_helpers import json_response


@pytest.mark.parametrize("path", [
    "/hyapi/api_version", "/hyapi/client_info", "/hyapi/manage_pages/get_pages",
    "/hyapi/manage_popups/get_popups", "/hyapi/manage_database/get_client_options",
])
def test_hydrus_versioned_responses(api_client, path):
    body = json_response(api_client.get(path))
    assert type(body["version"]) is int
    assert body["version"] > 0
    assert type(body["hydrus_version"]) is int
    assert body["hydrus_version"] > 0


def test_hydrus_identifies_idhan(api_client):
    body = json_response(api_client.get("/hyapi/api_version"))
    assert body["is_idhan_instance"] is True
    assert body["idhan_api_version"] > 0


def test_hydrus_lists_service_keys(api_client):
    body = json_response(api_client.get("/hyapi/get_services"))
    for group in ("all_known_files", "all_known_tags"):
        assert body[group]
        for service in body[group]:
            assert bytes.fromhex(service["service_key"])
            assert service["name"]
