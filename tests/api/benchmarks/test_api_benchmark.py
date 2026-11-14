"""Normal HTTP operations beyond tags; setup and validation are not timed."""

import hashlib
from uuid import uuid4

import pytest

from api_test_helpers import READ_ENDPOINTS, assert_read_response, json_response


@pytest.mark.parametrize("path,root_type,expected", READ_ENDPOINTS, ids=[case[0] for case in READ_ENDPOINTS])
@pytest.mark.benchmark(group="api-read")
def test_read_endpoint(benchmark, api_client, pytestconfig, path, root_type, expected):
    response = None

    def request():
        nonlocal response
        response = api_client.get(path)

    def validate():
        assert_read_response(response, root_type, expected)

    benchmark.pedantic(request, teardown=validate, rounds=pytestconfig.getoption("--api-benchmark-rounds"),
                       warmup_rounds=1)


@pytest.mark.parametrize("size", [1, 10, 100, 1000], ids=lambda size: f"{size}-records")
@pytest.mark.benchmark(group="record-batch-creation")
def test_create_records(benchmark, api_client, pytestconfig, size):
    payload = None
    response = None

    def setup():
        nonlocal payload
        payload = {"sha256": [hashlib.sha256(uuid4().bytes).hexdigest() for _ in range(size)]}

    def request():
        nonlocal response
        response = api_client.post("/records/create", json=payload)

    def validate():
        ids = json_response(response)
        assert len(ids) == size
        assert len(set(ids)) == size
        assert all(type(value) is int and value > 0 for value in ids)

    benchmark.extra_info["records_per_request"] = size
    benchmark.pedantic(request, setup=setup, teardown=validate, rounds=pytestconfig.getoption("--api-benchmark-rounds"),
                       warmup_rounds=1)


@pytest.mark.parametrize("suffix", ["/file", "/info", "/metadata", "/notes", "/urls", "/tags", "/tags/active"])
@pytest.mark.benchmark(group="record-read")
def test_read_imported_record(benchmark, api_client, pytestconfig, imported_record, suffix):
    response = None

    def request():
        nonlocal response
        response = api_client.get(f"/records/{imported_record['id']}{suffix}")

    def validate():
        if suffix == "/file":
            assert response.status_code == 200, response.text
            assert response.content == imported_record["bytes"]
        else:
            assert isinstance(json_response(response), (dict, list))

    benchmark.pedantic(request, teardown=validate, rounds=pytestconfig.getoption("--api-benchmark-rounds"),
                       warmup_rounds=1)


@pytest.mark.parametrize("method", ["GET", "POST"])
@pytest.mark.benchmark(group="record-search")
def test_search_records(benchmark, api_client, pytestconfig, searchable_record, method):
    record = searchable_record
    response = None
    parameters = {"tag_ids": record["tag_id"], "tag_domains": record["domain_id"]}
    body = {"tag_ids": [record["tag_id"]], "tag_domains": [record["domain_id"]]}

    def request():
        nonlocal response
        response = api_client.get("/search", params=parameters) if method == "GET" else api_client.post("/search",
                                                                                                        json=body)

    def validate():
        result = json_response(response)
        assert (result if method == "GET" else result["record_ids"]) == [record["id"]]

    benchmark.pedantic(request, teardown=validate, rounds=pytestconfig.getoption("--api-benchmark-rounds"),
                       warmup_rounds=1)


@pytest.mark.parametrize("path,status", [("/download_sessions", 201), ("/subscription_groups", 201)])
@pytest.mark.benchmark(group="resource-creation")
def test_create_resource(benchmark, api_client, pytestconfig, path, status):
    payload = None
    response = None

    def setup():
        nonlocal payload
        payload = {"name": f"benchmark_{uuid4().hex}"}
        if path == "/subscription_groups":
            payload["enabled"] = False

    def request():
        nonlocal response
        response = api_client.post(path, json=payload)

    def validate_and_cleanup():
        created = json_response(response, status)
        try:
            assert created["name"] == payload["name"]
            assert created["id"] > 0
        finally:
            json_response(api_client.delete(f"{path}/{created['id']}"))

    benchmark.pedantic(request, setup=setup, teardown=validate_and_cleanup,
                       rounds=pytestconfig.getoption("--api-benchmark-rounds"), warmup_rounds=1)


@pytest.mark.benchmark(group="file-import")
def test_import_file(benchmark, api_client, pytestconfig, writable_cluster):
    from api_test_helpers import make_test_png

    payload = None
    response = None

    def setup():
        nonlocal payload
        payload = make_test_png()

    def request():
        nonlocal response
        response = api_client.post("/file/import", content=payload,
                                   headers={"content-type": "application/octet-stream"})

    def validate_and_cleanup():
        record = json_response(response)
        record_id = record["record_id"]
        try:
            assert record_id > 0
            found = json_response(
                api_client.get("/records/search", params={"sha256": hashlib.sha256(payload).hexdigest()}))
            assert found == {"found": True, "record_id": record_id}
        finally:
            json_response(api_client.delete(f"/records/{record_id}/file"))

    benchmark.pedantic(request, setup=setup, teardown=validate_and_cleanup,
                       rounds=pytestconfig.getoption("--api-benchmark-rounds"), warmup_rounds=1)
