from api_test_helpers import json_response, wait_for_value


def test_metadata_rescan_job_completes(api_client, imported_record):
    queued = json_response(api_client.post("/jobs/metadata/rescan", json={"records": [imported_record["id"]]}))
    path = f"/jobs/{queued['job_id']}/status"
    wait_for_value(lambda: json_response(api_client.get(path))["completed"], True, timeout=30)
    completed = json_response(api_client.get(path))
    assert completed["job_id"] == queued["job_id"]
    assert not completed.get("error")
    assert completed["response"]["scanned_count"] == 1
    assert completed["response"]["failed_count"] == 0
    assert completed["response"]["failures"] == []
