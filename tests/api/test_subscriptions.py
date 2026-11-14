from uuid import uuid4

import pytest

from api_test_helpers import json_response


@pytest.fixture
def subscription_group(api_client):
    body = {"name": f"api_{uuid4().hex}", "generator": "gelbooru:search", "enabled": False}
    group = json_response(api_client.post("/subscription_groups", json=body), 201)
    try:
        yield group
    finally:
        json_response(api_client.delete(f"/subscription_groups/{group['id']}"))


def test_group_create_update_list(api_client, subscription_group):
    group = subscription_group
    assert group["enabled"] is False
    groups = json_response(api_client.get("/subscription_groups"))
    assert any(item["id"] == group["id"] for item in groups)
    name = f"api_renamed_{uuid4().hex}"
    updated = json_response(
        api_client.patch(f"/subscription_groups/{group['id']}", json={"name": name, "enabled": False}))
    assert updated["name"] == name
    assert updated["enabled"] is False


def test_subscription_create_update_delete(api_client, subscription_group):
    group = subscription_group
    subscription = json_response(
        api_client.post(f"/subscription_groups/{group['id']}/subscriptions", json={"tags": "pytest", "enabled": False}),
        201)
    try:
        assert subscription["group_id"] == group["id"]
        assert subscription["enabled"] is False
        updated = json_response(
            api_client.patch(f"/subscriptions/{subscription['id']}", json={"tags": "pytest_updated", "enabled": False}))
        assert updated["tags"] == "pytest_updated"
        listed = json_response(api_client.get("/subscriptions"))
        assert any(item["id"] == subscription["id"] for item in listed)
        records = json_response(api_client.get(f"/subscriptions/{subscription['id']}/records"))
        assert records["record_ids"] == []
    finally:
        json_response(api_client.delete(f"/subscriptions/{subscription['id']}"))
    assert all(item["id"] != subscription["id"] for item in json_response(api_client.get("/subscriptions")))
