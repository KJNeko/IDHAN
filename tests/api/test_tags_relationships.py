from uuid import uuid4

import pytest


@pytest.fixture
def relationship_tags(api_client):
    namespace = f"pytest_{uuid4().hex}"
    response = api_client.post("/tags/create", json=[
        {"namespace": namespace, "subtag": name} for name in ("a", "b", "c")
    ])
    assert response.status_code == 200, response.text
    return [tag["tag_id"] for tag in response.json()]


@pytest.fixture
def tag_domain(api_client):
    response = api_client.post("/tags/domain/create", json={"name": f"pytest_{uuid4().hex}"})
    assert response.status_code == 200, response.text
    domain = response.json()["tag_domain_id"]
    try:
        yield domain
    finally:
        response = api_client.delete(f"/tags/domain/{domain}/delete")
        assert response.status_code == 200, response.text


def relationships(api_client, domain, tag):
    response = api_client.get(f"/tags/{domain}/{tag}/relationships")
    assert response.status_code == 200, response.text
    return response.json()


def mutate(api_client, kind, action, domain, pairs, status=200):
    response = api_client.post(
        f"/tags/{kind}/{action}", params={"tag_domain_id": domain}, json=pairs
    )
    assert response.status_code == status, response.text


@pytest.mark.parametrize("kind,source_key,target_key,forward,reverse", [
    ("parents", "child_id", "parent_id", "parents", "children"),
    ("alias", "aliased_id", "alias_id", "aliases", "aliased"),
])
def test_relationship_create_duplicate_and_remove(
        api_client, tag_domain, relationship_tags, kind, source_key, target_key, forward, reverse
):
    source, target, _ = relationship_tags
    pair = [{source_key: source, target_key: target}]
    for _ in range(2):
        mutate(api_client, kind, "create", tag_domain, pair)
    assert relationships(api_client, tag_domain, source)[forward] == [target]
    assert relationships(api_client, tag_domain, target)[reverse] == [source]
    for _ in range(2):
        mutate(api_client, kind, "remove", tag_domain, pair)
    assert relationships(api_client, tag_domain, source)[forward] == []
    assert relationships(api_client, tag_domain, target)[reverse] == []


@pytest.mark.parametrize("kind,source_key,target_key,field", [
    ("parents", "child_id", "parent_id", "parents"),
    ("alias", "aliased_id", "alias_id", "aliases"),
])
def test_cycle_rejection_rolls_back_batch(
        api_client, tag_domain, relationship_tags, kind, source_key, target_key, field
):
    a, b, c = relationship_tags
    mutate(api_client, kind, "create", tag_domain, [{source_key: a, target_key: b}])
    mutate(api_client, kind, "create", tag_domain, [
        {source_key: b, target_key: c}, {source_key: c, target_key: a},
    ], status=409)
    assert relationships(api_client, tag_domain, a)[field] == [b]
    assert relationships(api_client, tag_domain, b)[field] == []
    assert relationships(api_client, tag_domain, c)[field] == []


def test_parent_relationship_isolated_by_domain(api_client, tag_domain, relationship_tags):
    child, parent, _ = relationship_tags
    other_response = api_client.post("/tags/domain/create", json={"name": f"pytest_{uuid4().hex}"})
    assert other_response.status_code == 200, other_response.text
    other = other_response.json()["tag_domain_id"]
    try:
        mutate(api_client, "parents", "create", tag_domain, [{"child_id": child, "parent_id": parent}])
        assert relationships(api_client, tag_domain, child)["parents"] == [parent]
        assert relationships(api_client, other, child)["parents"] == []
    finally:
        response = api_client.delete(f"/tags/domain/{other}/delete")
        assert response.status_code == 200, response.text


@pytest.mark.parametrize("action", ["create", "remove"])
def test_sibling_endpoints_are_disabled(api_client, action):
    response = api_client.post(f"/tags/siblings/{action}", json=[])
    assert response.status_code == 501, response.text
