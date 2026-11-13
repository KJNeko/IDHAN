#!/usr/bin/env python3
"""Validate compiled OpenAPI JSON, local references, examples, and registered route coverage."""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
from urllib.request import urlopen

from jsonschema import Draft202012Validator
from jsonschema.validators import SPECIFICATIONS
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

ROOT = Path(__file__).resolve().parents[2]
OFFICIAL_SCHEMA = "https://spec.openapis.org/oas/3.2/schema-base/2026-08-30"
METHODS = {"get", "post", "put", "patch", "delete", "head", "options", "trace", "query"}
# UI entrypoints and static-file routes are outside the API contract.
UI_PATHS = {"/", "/status", "/index.html", "/api", "/assets/{filename}", "/plugins/{*path}",
            "/log/pretty"}


def official_registry(cache):
    cache.mkdir(parents=True, exist_ok=True)

    def retrieve(uri):
        path = cache / (hashlib.sha256(uri.encode()).hexdigest() + ".json")
        if not path.exists():
            if not uri.startswith("https://spec.openapis.org/"):
                raise ValueError(f"Unexpected schema resource: {uri}")
            with urlopen(uri, timeout=30) as response:
                contents = response.read()
            json.loads(contents)
            path.write_bytes(contents)
        return Resource.from_contents(json.loads(path.read_text()), default_specification=DRAFT202012)

    registry = Registry(retrieve=retrieve).with_resources(SPECIFICATIONS.items())
    schema = retrieve(OFFICIAL_SCHEMA).contents
    return schema, registry.with_resource(OFFICIAL_SCHEMA, Resource.from_contents(schema))


def registered_routes():
    routes = set()
    # Matches literal registrations and Endpoint annotations for URLs such as
    # http://localhost:16609/clusters/list.
    pattern = re.compile(r'proxygen::HTTPMethod::(\w+)\s*,\s*"([^"\n]+)"')
    # Matches function annotations such as Post{"/records/tags/add"} for
    # http://localhost:16609/records/tags/add.
    annotated = re.compile(r'http::(Get|Post|Put|Patch|Delete)\s*[{(]\s*"([^"\n]+)"')
    for source in (ROOT / "IDHANServer/src").rglob("*"):
        if source.suffix not in {".cpp", ".hpp", ".h", ".hh", ".hxx"}:
            continue
        text = source.read_text()
        routes.update((method.lower(), path) for method, path in pattern.findall(text))
        routes.update((method.lower(), path) for method, path in annotated.findall(text))
    # Compatibility routes are excluded from the published contract pending their rework.
    return {
        (method, path) for method, path in routes
        if path not in UI_PATHS and not path.startswith("/hyapi/")
    }


def check_contract(document):
    errors = []
    documented = set()
    operation_ids = set()
    for path, item in document["paths"].items():
        for method, operation in item.items():
            if method not in METHODS:
                continue
            documented.add((method, path))
            parameters = item.get("parameters", []) + operation.get("parameters", [])
            names = [p["name"] for p in parameters if p["in"] == "path"]
            # Matches placeholders such as {record_id} in http://localhost:16609/records/{record_id}.
            expected = set(re.findall(r"\{([^}]+)\}", path))
            if set(names) != expected or any(
                    p.get("required") is not True for p in parameters if p["in"] == "path"
            ):
                errors.append(f"{method.upper()} {path}: path parameters do not match placeholders")
            operation_id = operation.get("operationId")
            if operation_id in operation_ids:
                errors.append(f"Duplicate operationId: {operation_id}")
            if operation_id:
                operation_ids.add(operation_id)
    registered = registered_routes()
    # HEAD uses GET handlers throughout the HTTP server.
    comparable = {(m, p) for m, p in documented if m != "head"}
    errors += [f"Undocumented route: {m.upper()} {p}" for m, p in sorted(registered - comparable)]
    errors += [f"Unregistered documented route: {m.upper()} {p}" for m, p in sorted(comparable - registered)]
    errors += [f"HEAD has no GET route: {p}" for m, p in documented if m == "head" and ("get", p) not in registered]

    def validate_examples(schema, values, location):
        schema = copy.deepcopy(schema)
        if isinstance(schema, dict):
            schema["components"] = document["components"]
        validator = Draft202012Validator(schema)
        for name, example in values:
            for error in validator.iter_errors(example):
                errors.append(f"{location}/{name}: {error.message}")

    def schema_examples(schema, location):
        if not isinstance(schema, dict):
            return
        values = [(f"examples/{i}", value) for i, value in enumerate(schema.get("examples", []))]
        if "default" in schema:
            values.append(("default", schema["default"]))
        validate_examples(schema, values, location)
        for key in ("properties", "$defs", "dependentSchemas", "patternProperties"):
            for name, child in schema.get(key, {}).items():
                schema_examples(child, f"{location}/{key}/{name}")
        for key in ("allOf", "anyOf", "oneOf", "prefixItems"):
            for i, child in enumerate(schema.get(key, [])):
                schema_examples(child, f"{location}/{key}/{i}")
        for key in ("items", "additionalProperties", "contains", "not", "if", "then", "else"):
            schema_examples(schema.get(key), f"{location}/{key}")

    def examples(value, location):
        if isinstance(value, list):
            for i, child in enumerate(value):
                examples(child, f"{location}/{i}")
        elif isinstance(value, dict):
            if "schema" in value and isinstance(value["schema"], (dict, bool)):
                items = []
                if "example" in value:
                    items.append(("example", value["example"]))
                items += [(name, ex["value"]) for name, ex in value.get("examples", {}).items() if "value" in ex]
                validate_examples(value["schema"], items, location)
                schema_examples(value["schema"], f"{location}/schema")
            for key, child in value.items():
                if key not in {"example", "examples"}:
                    examples(child, f"{location}/{key}")

    examples(document, "")
    for name, schema in document["components"]["schemas"].items():
        schema_examples(schema, f"/components/schemas/{name}")
    return errors, len(documented)


def local_reference_errors(document):
    errors = []

    def visit(value, location):
        if isinstance(value, list):
            for index, item in enumerate(value):
                visit(item, f"{location}/{index}")
        elif isinstance(value, dict):
            reference = value.get("$ref")
            if isinstance(reference, str) and reference.startswith("#/"):
                target = document
                try:
                    for encoded in reference[2:].split("/"):
                        key = encoded.replace("~1", "/").replace("~0", "~")
                        target = target[int(key)] if isinstance(target, list) else target[key]
                except (KeyError, IndexError, ValueError, TypeError):
                    errors.append(f"{location}: unresolved reference {reference}")
            for key, item in value.items():
                visit(item, f"{location}/{key}")

    visit(document, "")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema-cache", type=Path, default=Path(tempfile.gettempdir()) / "idhan-openapi-schema-cache")
    parser.add_argument("--input", type=Path, required=True, help="Validate a compiled OpenAPI JSON document")
    parser.add_argument("--offline", action="store_true",
                        help="Check local references, examples and route coverage without downloading the official schema")
    args = parser.parse_args()
    document = json.loads(args.input.read_text())
    errors = local_reference_errors(document)
    if not args.offline:
        schema, registry = official_registry(args.schema_cache)
        validator = Draft202012Validator(schema, registry=registry)
        errors.extend(f"{'/'.join(map(str, e.absolute_path))}: {e.message}" for e in validator.iter_errors(document))
    contract_errors, count = check_contract(document)
    errors.extend(contract_errors)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    checks = "local refs, examples" if args.offline else "official schema, local refs, examples"
    print(f"OpenAPI {document['openapi']}: {checks}, and {count} operations validated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
