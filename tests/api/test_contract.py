import re

import pytest

from api_test_helpers import json_response


@pytest.fixture(scope="module")
def api_contract(api_client):
    return json_response(api_client.get("/openapi.json"))


def test_openapi_has_versioned_paths(api_contract):
    assert api_contract["openapi"].startswith("3.")
    assert api_contract["info"]["version"]
    assert "/records/create" in api_contract["paths"]
    assert "/file/import" in api_contract["paths"]
    assert "/search" in api_contract["paths"]


def test_openapi_references_resolve(api_contract):
    def visit(node):
        if isinstance(node, dict):
            if "$ref" in node:
                reference = node["$ref"]
                assert reference.startswith("#/"), reference
                resolved = api_contract
                for part in reference[2:].split("/"):
                    key = part.replace("~1", "/").replace("~0", "~")
                    assert key in resolved, reference
                    resolved = resolved[key]
            for value in node.values():
                visit(value)
        elif isinstance(node, list):
            for item in node:
                visit(item)

    visit(api_contract)


def test_openapi_operations_have_responses_and_path_parameters(api_contract):
    operation_ids = set()
    for path, operations in api_contract["paths"].items():
        for method, operation in operations.items():
            if method not in ("get", "post", "put", "patch", "delete", "head", "options"):
                continue
            assert operation["responses"], (method, path)
            if path in ("/tags/siblings/create", "/tags/siblings/remove"):
                assert "501" in operation["responses"]
            else:
                assert any(code.startswith("2") for code in operation["responses"]), (method, path)
            operation_id = operation.get("operationId")
            if operation_id:
                assert operation_id not in operation_ids, operation_id
                operation_ids.add(operation_id)
            parameters = {item["name"]: item for item in operation.get("parameters", []) if item.get("in") == "path"}
            for name in re.findall(r"\{([^}]+)\}", path):  # Matches placeholders such as {record_id}.
                assert name in parameters, (path, name)
                assert parameters[name]["required"] is True


@pytest.mark.parametrize("path", [
    "/records/0/info", "/records/0/notes", "/records/0/urls", "/records/0/tags",
    "/records/0/file", "/records/0/metadata", "/records/0/thumbnail",
    "/clusters/0/info", "/tags/0/info", "/tags/domain/0/info",
    "/relationships/0", "/download_sessions/0",
    "/download_sessions/0/errors", "/download_sessions/0/records",
    "/subscriptions/0/records",
])
def test_nonpositive_path_id_rejected(api_client, path):
    error = json_response(api_client.get(path), 400)
    assert isinstance(error["error"], str)
    assert error["error"]
