import hashlib

from api_test_helpers import json_response


def test_import_download_and_duplicate(api_client, imported_record):
    record = imported_record
    response = api_client.get(f"/records/{record['id']}/file")
    assert response.status_code == 200, response.text
    assert response.content == record["bytes"]
    assert hashlib.sha256(response.content).hexdigest() == record["sha256"]
    duplicate = json_response(
        api_client.post("/file/import", content=record["bytes"], headers={"content-type": "application/octet-stream"}))
    assert duplicate["record_id"] == record["id"]
    found = json_response(api_client.get("/records/search", params={"sha256": record["sha256"]}))
    assert found == {"found": True, "record_id": record["id"]}


def test_imported_record_info_and_metadata(api_client, imported_record):
    path = f"/records/{imported_record['id']}"
    for suffix in ("/info", "/metadata"):
        body = json_response(api_client.get(path + suffix))
        assert isinstance(body, dict)


def test_thumbnail_is_webp(api_client, imported_record):
    response = api_client.get(f"/records/{imported_record['id']}/thumbnail")
    assert response.status_code == 200, response.text
    assert response.headers["content-type"].split(";", 1)[0] == "image/webp"
    assert response.content[:4] == b"RIFF"
    assert response.content[8:12] == b"WEBP"
