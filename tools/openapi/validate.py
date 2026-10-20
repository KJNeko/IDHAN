#!/usr/bin/env python3
"""Validate the API description, local references, examples, and registered route coverage."""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
from urllib.parse import urldefrag, urljoin
from urllib.request import urlopen

import yaml
from jsonschema import Draft202012Validator
from jsonschema.validators import SPECIFICATIONS
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

ROOT = Path(__file__).resolve().parents[2]
API = ROOT / "docs/api"
OFFICIAL_SCHEMA = "https://spec.openapis.org/oas/3.2/schema-base/2026-08-30"
METHODS = {"get", "post", "put", "patch", "delete", "head", "options", "trace", "query"}
# UI entrypoints and static-file routes are outside the API contract.
UI_PATHS = {"/api", "/api/{*path}", "/assets/{filename}", "/plugins/{*path}"}


class UniqueKeyLoader(yaml.SafeLoader):
    pass


def unique_mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result:
            raise ValueError(f"Duplicate YAML key {key!r} at line {key_node.start_mark.line + 1}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueKeyLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique_mapping)


def read_yaml(path):
    return yaml.load(path.read_text(), Loader=UniqueKeyLoader)


def pointer(document, fragment):
    if not fragment:
        return document
    if not fragment.startswith("/"):
        raise ValueError(f"Unsupported local reference fragment: {fragment}")
    for field in fragment[1:].split("/"):
        field = field.replace("~1", "/").replace("~0", "~")
        document = document[int(field)] if isinstance(document, list) else document[field]
    return document


def bundle():
    """Inline path/parameter refs and register file schemas as local components, preserving recursion."""
    documents = {p.resolve(): read_yaml(p) for p in API.rglob("*.yaml")}
    schemas = {p: p.stem for p in documents if p.parent == (API / "schemas").resolve()}

    def resolve(value, source, trail=()):
        if isinstance(value, list):
            return [resolve(item, source, trail) for item in value]
        if not isinstance(value, dict):
            return value
        if "$ref" in value:
            url = urljoin(source.as_uri(), value["$ref"])
            target, fragment = urldefrag(url)
            if not target.startswith("file://"):
                raise ValueError(f"External API reference requires an explicit local copy: {url}")
            from urllib.request import url2pathname

            path = Path(url2pathname(target[len("file://"):])).resolve()
            if path not in documents:
                raise ValueError(f"Missing reference target: {path}")
            selected = pointer(documents[path], fragment)
            siblings = {k: resolve(v, source, trail) for k, v in value.items() if k != "$ref"}
            if path in schemas:
                suffix = fragment if fragment else ""
                return {"$ref": f"#/components/schemas/{schemas[path]}{suffix}", **siblings}
            if url in trail:
                raise ValueError(f"Circular non-schema reference: {url}")
            return {**resolve(selected, path, (*trail, url)), **siblings}
        return {key: resolve(item, source, trail) for key, item in value.items()}

    index = (API / "index.yaml").resolve()
    result = resolve(documents[index], index)
    result["components"]["schemas"] = {
        name: resolve(documents[path], path) for path, name in schemas.items()
    }
    return result


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
    # Literal route registrations, e.g. add(HTTPMethod::GET, "/clusters/list", ...).
    pattern = re.compile(r'proxygen::HTTPMethod::(\w+)\s*,\s*"([^"\n]+)"')
    for source in (ROOT / "IDHANServer/src/httpapi").glob("*.cpp"):
        # Parameter declarations describe existing endpoints; they do not register routes.
        if source.name == "parameterDeclarations.cpp":
            continue
        routes.update((method.lower(), path) for method, path in pattern.findall(source.read_text()))
    server = (ROOT / "IDHANServer/src/ServerContext.cpp").read_text()
    # Dynamic aliases such as recordFileRoute(db, "/records/{record_id}").
    for path in re.findall(r'record(?:File|Info)Route\([^\n]*?"([^"]+)"', server):
        routes.add(("get", path))
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema-cache", type=Path, default=Path(tempfile.gettempdir()) / "idhan-openapi-schema-cache")
    parser.add_argument("--bundle", type=Path, help="Optionally write a standalone JSON description")
    args = parser.parse_args()
    document = bundle()
    schema, registry = official_registry(args.schema_cache)
    validator = Draft202012Validator(schema, registry=registry)
    errors = [f"{'/'.join(map(str, e.absolute_path))}: {e.message}" for e in validator.iter_errors(document)]
    contract_errors, count = check_contract(document)
    errors.extend(contract_errors)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    if args.bundle:
        args.bundle.write_text(json.dumps(document, indent=2) + "\n")
    print(f"OpenAPI {document['openapi']}: official schema, local refs, examples, and {count} operations validated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
