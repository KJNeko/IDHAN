import hashlib
from uuid import uuid4

import pytest

from api_test_helpers import json_response


def test_create_and_find_record(api_client, record):
    found = json_response(api_client.get("/records/search", params={"sha256": record["sha256"]}))
    assert found == {"found": True, "record_id": record["id"]}
    duplicate = json_response(api_client.post("/records/create", json={"sha256": record["sha256"]}))
    assert duplicate == {"record_id": record["id"]}


def test_create_record_batch(api_client):
    hashes = [hashlib.sha256(uuid4().bytes).hexdigest() for _ in range(3)]
    ids = json_response(api_client.post("/records/create", json={"sha256": hashes}))
    assert len(ids) == 3
    assert len(set(ids)) == 3
    for digest, record_id in zip(hashes, ids, strict=True):
        found = json_response(api_client.get("/records/search", params={"sha256": digest}))
        assert found == {"found": True, "record_id": record_id}


def test_find_missing_hash(api_client):
    digest = hashlib.sha256(uuid4().bytes).hexdigest()
    assert json_response(api_client.get("/records/search", params={"sha256": digest})) == {"found": False}


@pytest.mark.parametrize("value", ["", "abc", "g" * 64, 123, None, ["bad"]])
def test_invalid_record_hash(api_client, value):
    assert api_client.post("/records/create", json={"sha256": value}).status_code == 400


def test_notes_round_trip(api_client, record):
    path = f"/records/{record['id']}"
    assert json_response(api_client.get(path + "/notes")) == []
    text = "pytest note: café 日本語\nsecond line"
    notes = json_response(api_client.post(path + "/add_note", content=text.encode(),
                                          headers={"content-type": "text/plain; charset=utf-8"}))
    assert len(notes) == 1
    assert notes[0]["text"] == text
    try:
        assert json_response(api_client.get(path + "/notes")) == notes
        assert json_response(
            api_client.post(path + "/add_note", content=text.encode(), headers={"content-type": "text/plain"})) == notes
    finally:
        response = api_client.delete(path + f"/remove_note/{notes[0]['note_id']}")
        assert response.status_code == 200, response.text
    assert json_response(api_client.get(path + "/notes")) == []


def test_urls_round_trip(api_client, record):
    path = f"/records/{record['id']}/urls"
    urls = [f"http://127.0.0.1:9/pytest/{uuid4().hex}", f"http://127.0.0.1:9/pytest/other/{uuid4().hex}"]
    assert json_response(api_client.get(path)) == []
    try:
        for _ in range(2):
            json_response(api_client.post(path + "/add", json={"urls": urls}))
        assert set(json_response(api_client.get(path))) == set(urls)
    finally:
        response = api_client.post(path + "/remove", json={"urls": urls})
        assert response.status_code == 200, response.text
    assert json_response(api_client.get(path)) == []


def test_record_without_file(api_client, record):
    assert api_client.get(f"/records/{record['id']}/file").status_code == 404
