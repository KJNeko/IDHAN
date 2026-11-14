from uuid import uuid4

import pytest


@pytest.fixture
def tag_pair():
    # Tags have no delete endpoint; unique names isolate repeated test runs.
    return {"namespace": "pytest", "subtag": f"tag_{uuid4().hex}"}


@pytest.fixture
def created_tag(api_client, tag_pair):
    response = api_client.post("/tags/create", json=[tag_pair])
    assert response.status_code == 200, response.text
    tags = response.json()
    assert len(tags) == 1
    tag_id = tags[0]["tag_id"]
    assert type(tag_id) is int
    assert tag_id > 0
    return tag_id


def test_create_and_read_tag(api_client, tag_pair, created_tag):
    response = api_client.get(f"/tags/{created_tag}/info")
    assert response.status_code == 200, response.text
    tag = response.json()
    assert tag["tag_id"] == created_tag
    assert tag["namespace"]["text"] == tag_pair["namespace"]
    assert tag["subtag"]["text"] == tag_pair["subtag"]
    assert tag["storage_count"] == 0
    assert tag["display_count"] == 0


def test_creating_existing_tag_returns_same_id(api_client, tag_pair, created_tag):
    response = api_client.post("/tags/create", json=[tag_pair])
    assert response.status_code == 200, response.text
    assert response.json() == [{"tag_id": created_tag}]


def test_search_existing_tag(api_client, tag_pair, created_tag):
    response = api_client.get(
        "/tags/search", params={"tag": f"{tag_pair['namespace']}:{tag_pair['subtag']}"}
    )
    assert response.status_code == 200, response.text
    assert response.json() == {"found": True, "tag_id": created_tag}


def test_search_missing_tag(api_client, tag_pair):
    response = api_client.get(
        "/tags/search", params={"tag": f"{tag_pair['namespace']}:{tag_pair['subtag']}"}
    )
    assert response.status_code == 200, response.text
    assert response.json() == {"found": False}


@pytest.mark.parametrize(
    "payload",
    [
        {},
        [{"subtag": "missing_namespace"}],
        [{"namespace": "pytest"}],
        [{"namespace": 123, "subtag": "invalid_namespace"}],
        [{"namespace": "pytest", "subtag": 123}],
    ],
    ids=["object-root", "missing-namespace", "missing-subtag", "numeric-namespace", "numeric-subtag"],
)
def test_create_rejects_invalid_payload(api_client, payload):
    response = api_client.post("/tags/create", json=payload)
    assert response.status_code == 400, response.text


def test_autocomplete_rejects_excessive_limit(api_client):
    response = api_client.get("/tags/autocomplete", params={"tag": "pytest", "limit": 257})
    assert response.status_code == 400, response.text


@pytest.mark.parametrize("first,second", [("Example", "example"), ("caf\u00e9", "cafe\u0301"), ("CAFÉ", "café")])
def test_normalized_tags_share_id(api_client, first, second):
    namespace = f"pytest_{uuid4().hex}"
    response = api_client.post("/tags/create", json=[
        {"namespace": namespace.upper(), "subtag": first},
        {"namespace": namespace, "subtag": second},
    ])
    assert response.status_code == 200, response.text
    tags = response.json()
    assert len(tags) == 2
    assert tags[0] == tags[1]


def test_batch_preserves_order_and_duplicates(api_client, tag_pair):
    other = {**tag_pair, "subtag": tag_pair["subtag"] + "_other"}
    response = api_client.post("/tags/create", json=[tag_pair, other, tag_pair])
    assert response.status_code == 200, response.text
    tags = response.json()
    assert len(tags) == 3
    assert tags[0] == tags[2]
    assert tags[0] != tags[1]
    for item, expected in zip(tags, [tag_pair, other, tag_pair], strict=True):
        info = api_client.get(f"/tags/{item['tag_id']}/info")
        assert info.status_code == 200, info.text
        assert info.json()["subtag"]["text"] == expected["subtag"]


def test_invalid_batch_does_not_create_valid_prefix(api_client, tag_pair):
    response = api_client.post("/tags/create", json=[tag_pair, {"namespace": "pytest"}])
    assert response.status_code == 400, response.text
    search = api_client.get("/tags/search", params={"tag": f"{tag_pair['namespace']}:{tag_pair['subtag']}"})
    assert search.status_code == 200, search.text
    assert search.json() == {"found": False}


def test_concurrent_creation_returns_one_id(api_client, tag_pair):
    from concurrent.futures import ThreadPoolExecutor
    from threading import Barrier

    barrier = Barrier(4)

    def create_tag():
        barrier.wait(timeout=10)
        response = api_client.post("/tags/create", json=[tag_pair])
        assert response.status_code == 200, response.text
        return response.json()[0]["tag_id"]

    with ThreadPoolExecutor(max_workers=4) as executor:
        futures = [executor.submit(create_tag) for _ in range(4)]
        ids = [future.result() for future in futures]
    assert len(set(ids)) == 1


def test_autocomplete_includes_unused_tag(api_client, tag_pair, created_tag):
    query = f"{tag_pair['namespace']}:{tag_pair['subtag']}"
    response = api_client.get("/tags/autocomplete", params={
        "tag": query, "include_unused": "true", "limit": 1,
    })
    assert response.status_code == 200, response.text
    matches = response.json()
    assert len(matches) == 1
    assert matches[0]["tag_id"] == created_tag
    assert matches[0]["tag_text"] == query
    assert matches[0]["count"] == 0

    response = api_client.get("/tags/autocomplete", params={
        "tag": query, "include_unused": "false",
    })
    assert response.status_code == 200, response.text
    assert response.json() == []
