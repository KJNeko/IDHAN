# API description

`index.yaml` is the OpenAPI 3.2.1 entrypoint, served at `/api/index.yaml`.
The interactive documentation is available at `/api`.
Path items, parameters, and schemas use relative file references so the
description can be served directly or bundled into one document.

The description covers the registered native API. Compatibility endpoints are
excluded pending their rework. WebUI entrypoints and static assets are outside
this API contract. Diagnostic payloads and parser-specific metadata allow
additional fields.

Native authentication accepts a raw hexadecimal key through `idhan_key`,
`Authorization`, `X-API-Key`, or `IDHAN-API-Key`. `Authorization` has no Bearer
prefix. A nonempty query key takes precedence; otherwise headers are checked
in the order listed above. Missing or malformed native credentials return 400;
an unrecognized key returns 401. Error bodies are plain text.

Unknown URL query parameters and declared JSON request fields currently produce
server warnings while requests continue through normal validation. This includes
unknown fields in declared nested objects. Free-form maps, such as secrets and
layout contents, remain open-ended. See [endpoint parameter declarations](../api-parameters.md)
for the declaration lists and the future rejection policy.

Download-session and rate-limit streams document named SSE events and their JSON
payloads separately from REST responses. See the [SSE client guide](../api-sse.md)
for framing, empty-data handling, and reconnect behavior.

Response field presence and arbitrary JSON job results are described in the
[response contract guide](../api-response-contracts.md), including separate
contracts for known job producers.

## Validation

From the repository root, install the validator dependencies in your Python
environment and run:

```sh
python3 -m pip install -r tools/openapi/requirements.txt
python3 tools/openapi/validate.py
```

The first validation downloads the pinned official OpenAPI 3.2 schema and its
dependencies from `spec.openapis.org`. Later validations reuse the temporary
schema cache; `--schema-cache PATH` selects a persistent cache for CI.

Validation checks duplicate YAML keys, every local reference, the bundled
description against the official schema, examples and defaults against their
JSON Schemas, required path parameters, operation IDs, and registered method/path
coverage. Coverage is extracted from literal route registrations and the current
record-file/info alias registrations. Update that extraction if registration
patterns change. HEAD requests reuse GET handlers; only explicitly described
HEAD operations appear separately. Schema validation cannot prove runtime
behavior, so changes to handlers still need a contract review.

To create a standalone description for other tools:

```sh
python3 tools/openapi/validate.py --bundle /tmp/idhan-openapi.json
```

Specification references:

- [OpenAPI 3.2.1](https://spec.openapis.org/oas/v3.2.1.html)
- [Official schema with JSON Schema validation](https://spec.openapis.org/oas/3.2/schema-base/2026-08-30)
