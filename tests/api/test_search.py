import pytest

from api_test_helpers import json_response


def test_get_search_finds_tagged_record(api_client, searchable_record):
    record = searchable_record
    result = json_response(
        api_client.get("/search", params={"tag_ids": record["tag_id"], "tag_domains": record["domain_id"]}))
    assert result == [record["id"]]


def test_post_search_returns_ids_and_hashes(api_client, searchable_record):
    record = searchable_record
    result = json_response(api_client.post("/search", json={
        "tag_ids": [record["tag_id"]], "tag_domains": [record["domain_id"]], "return": ["ids", "hashes"],
    }))
    assert result["record_ids"] == [record["id"]]
    assert result["hashes"] == [record["sha256"]]
    assert result["count"] == 1
    assert result["truncated"] is False


def test_search_offset_excludes_only_match(api_client, searchable_record):
    record = searchable_record
    result = json_response(api_client.post("/search", json={
        "tag_ids": [record["tag_id"]], "tag_domains": [record["domain_id"]], "offset": 1,
    }))
    assert result["record_ids"] == []
    assert result["count"] == 0


@pytest.mark.parametrize("field", ["offset", "limit"])
def test_search_rejects_negative_pagination(api_client, field):
    assert json_response(api_client.post("/search", json={field: -1}), 400)["error"]
