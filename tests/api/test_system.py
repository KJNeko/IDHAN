import pytest

from api_test_helpers import READ_ENDPOINTS, assert_read_response, json_response


@pytest.mark.parametrize("path,root_type,expected", READ_ENDPOINTS, ids=[case[0] for case in READ_ENDPOINTS])
def test_read_endpoint(api_client, path, root_type, expected):
    assert_read_response(api_client.get(path), root_type, expected)


def test_database_stats_are_nonnegative(api_client):
    stats = json_response(api_client.get("/db/stats"))
    for field in ("records", "tags", "clusters", "mappings"):
        assert type(stats[field]) is int
        assert stats[field] >= 0
    assert type(stats["mappings_estimated"]) is bool


def test_mime_registry_has_common_formats(api_client):
    types = json_response(api_client.get("/mime"))
    assert {"image/jpeg", "image/png", "video/mp4"} <= set(types.values())


def test_job_list_and_individual_status_agree(api_client):
    jobs = json_response(api_client.get("/jobs/status"))["jobs"]
    assert jobs
    for job in jobs:
        info = json_response(api_client.get(f"/jobs/{job['job_id']}/status"))
        assert info["job_id"] == job["job_id"]


def test_unknown_route_returns_not_found(api_client):
    assert api_client.get("/pytest-route-that-does-not-exist").status_code == 404


def test_missing_job_returns_not_found(api_client):
    error = json_response(api_client.get("/jobs/0/status"), 404)
    assert error["error"]
