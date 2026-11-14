from uuid import uuid4

from api_test_helpers import json_response


def test_cluster_info_and_list(api_client, writable_cluster):
    cluster = writable_cluster
    fetched = json_response(api_client.get(f"/clusters/{cluster['cluster_id']}/info"))
    assert fetched["cluster_id"] == cluster["cluster_id"]
    assert fetched["path"] == cluster["path"]
    assert fetched["readonly"] is False
    assert fetched["size"]["used"] >= 0
    assert any(item["cluster_id"] == cluster["cluster_id"] for item in json_response(api_client.get("/clusters/list")))


def test_cluster_modify(api_client, writable_cluster):
    name = f"api_renamed_{uuid4().hex}"
    cluster_id = writable_cluster["cluster_id"]
    updated = json_response(api_client.patch(f"/clusters/{cluster_id}/modify", json={"name": name, "ratio": 2}))
    assert updated["name"] == name
    assert updated["ratio_number"] == 2
    fetched = json_response(api_client.get(f"/clusters/{cluster_id}/info"))
    assert fetched["name"] == name
    assert fetched["ratio_number"] == 2
