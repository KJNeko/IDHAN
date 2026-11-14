from uuid import uuid4

import pytest

from api_test_helpers import json_response, wait_for_value


@pytest.fixture
def record_tags(api_client):
    namespace = f"api_{uuid4().hex}"
    return [item["tag_id"] for item in json_response(api_client.post("/tags/create", json=[
        {"namespace": namespace, "subtag": name} for name in ("child", "parent", "alias")
    ]))]


def domain_tags(api_client, record_id, domain, active=False):
    suffix = "/active" if active else ""
    groups = json_response(api_client.get(f"/records/{record_id}/tags{suffix}"))
    return {tag for group in groups if group["tag_domain_id"] == domain for tag in group["tag_ids"]}


def test_add_remove_tags_and_counts(api_client, imported_record, isolated_tag_domain, record_tags):
    record = imported_record
    domain = isolated_tag_domain
    tag = record_tags[0]
    path = f"/records/{record['id']}/tags"
    for _ in range(2):
        json_response(api_client.post(path + "/add", params={"tag_domain_id": domain}, json=[tag]))
    assert domain_tags(api_client, record["id"], domain) == {tag}
    wait_for_value(
        lambda: json_response(api_client.get(f"/tags/{tag}/info", params={"tag_domains": domain}))["storage_count"], 1,
        timeout=75)
    for _ in range(2):
        json_response(api_client.post(path + "/remove", params={"tag_domain_id": domain}, json=[tag]))
    assert domain_tags(api_client, record["id"], domain) == set()
    wait_for_value(
        lambda: json_response(api_client.get(f"/tags/{tag}/info", params={"tag_domains": domain}))["storage_count"], 0,
        timeout=75)


@pytest.mark.parametrize("kind", ["parents", "alias"])
def test_resolved_tags_follow_relationship_changes(api_client, imported_record, isolated_tag_domain, record_tags, kind):
    record = imported_record
    domain = isolated_tag_domain
    child, parent, _ = record_tags
    path = f"/records/{record['id']}/tags"
    json_response(api_client.post(path + "/add", params={"tag_domain_id": domain}, json=[child]))
    pair = {"child_id": child, "parent_id": parent} if kind == "parents" else {"aliased_id": child, "alias_id": parent}
    json_response(api_client.post(f"/tags/{kind}/create", params={"tag_domain_id": domain}, json=[pair]))
    assert domain_tags(api_client, record["id"], domain) == {child}
    assert domain_tags(api_client, record["id"], domain, active=True) == (
        {child, parent} if kind == "parents" else {parent})
    json_response(api_client.post(f"/tags/{kind}/remove", params={"tag_domain_id": domain}, json=[pair]))
    assert domain_tags(api_client, record["id"], domain, active=True) == {child}


def test_search_by_tag_and_domain(api_client, imported_record, isolated_tag_domain, record_tags):
    record = imported_record
    domain = isolated_tag_domain
    tag = record_tags[0]
    json_response(api_client.post(f"/records/{record['id']}/tags/add", params={"tag_domain_id": domain}, json=[tag]))
    found = json_response(
        api_client.get("/search", params={"tag_ids": tag, "tag_domains": domain, "include_deleted": "true"}))
    assert found == [record["id"]]
    other = json_response(
        api_client.get("/search", params={"tag_ids": record_tags[1], "tag_domains": domain, "include_deleted": "true"}))
    assert other == []
