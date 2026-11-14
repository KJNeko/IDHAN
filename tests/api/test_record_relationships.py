import hashlib
from uuid import uuid4

import pytest

from api_test_helpers import json_response


@pytest.fixture
def record_pair(api_client):
    return json_response(api_client.post("/records/create", json={"sha256": [
        hashlib.sha256(uuid4().bytes).hexdigest() for _ in range(2)
    ]}))


@pytest.mark.parametrize("kind", ["duplicates", "alternatives", "unrelated"])
def test_relationship_add_and_clear(api_client, record_pair, kind):
    a, b = record_pair
    pair = {"record_id_a": a, "record_id_b": b}
    body = {"better_id": a, "worse_id": b} if kind == "duplicates" else ([a, b] if kind == "alternatives" else pair)
    json_response(api_client.post(f"/relationships/{kind}/add", json=body))
    try:
        if kind != "unrelated":
            left = json_response(api_client.get(f"/relationships/{a}"))
            right = json_response(api_client.get(f"/relationships/{b}"))
            assert b in left["inferior" if kind == "duplicates" else "alternatives"]
            assert a in right["superior" if kind == "duplicates" else "alternatives"]
    finally:
        result = json_response(api_client.post("/relationships/clear", json=pair))
        field = \
        {"duplicates": "duplicate_removed", "alternatives": "alternative_removed", "unrelated": "unrelated_removed"}[
            kind]
        assert result[field] is True
    for record_id in (a, b):
        result = json_response(api_client.get(f"/relationships/{record_id}"))
        assert result == {"inferior": [], "superior": [], "alternatives": []}
