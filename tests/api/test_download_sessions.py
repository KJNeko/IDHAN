from uuid import uuid4

import pytest

from api_test_helpers import json_response


@pytest.fixture
def download_session(api_client):
    name = f"api_{uuid4().hex}"
    session = json_response(api_client.post("/download_sessions", json={"name": name}), 201)
    try:
        yield session
    finally:
        json_response(api_client.delete(f"/download_sessions/{session['id']}"))


def test_empty_session_lifecycle(api_client, download_session):
    session = download_session
    path = f"/download_sessions/{session['id']}"
    fetched = json_response(api_client.get(path))
    assert fetched["id"] == session["id"]
    assert fetched["name"] == session["name"]
    assert any(item["id"] == session["id"] for item in json_response(api_client.get("/download_sessions")))
    assert json_response(api_client.get(path + "/records"))["record_ids"] == []
    assert json_response(api_client.get(path + "/errors"))["errors"] == []


@pytest.mark.parametrize("name", [""])
def test_session_rejects_empty_name(api_client, name):
    assert api_client.post("/download_sessions", json={"name": name}).status_code == 400
