# Endpoint parameter declarations

Native API routes declare accepted query names and JSON object fields in
`IDHANServer/src/httpapi/parameterDeclarations.cpp`. The lists describe names
consumed by the implementation, including supported aliases. They do not replace
the handler's type, value, required-field, or authorization checks.

Unknown names currently produce a server warning and the request continues
unchanged. For example, `GET /search?ordr=asc` warns about `ordr`, and a POST search
body containing `{"sort":{"ordr":"asc"}}` warns about `/sort/ordr`. Existing
validation still applies: an unknown **value** in `sort.by` returns 400 under the
strict sorting contract. Warnings contain the method, endpoint, and escaped field
names, without query or JSON values.

Each declaration stores `http::ParameterRules` in the route metadata. Its
`unknown` policy defaults to `UnknownParameterPolicy::warn`. Changing it to
`UnknownParameterPolicy::reject` makes the same checks return a plain-text 400
listing unknown names. There is no server setting that enables rejection yet.
Both policies have regression coverage.

## Query parameters

Query checks run after the route's existing pre-body validators, including
authentication, and before body consumption. Names are percent-decoded first.
Repeated allowed names remain allowed; duplicate-value behavior is still decided
by existing parameter accessors. Empty separators, including a trailing `&`, are
ignored. An actual unnamed field such as `?=value` remains an unknown name.

The application decorator adds `idhan_key` to every native query allowlist.
The registry also recognizes declared path captures because query fields and
path captures share `RequestParameters`. HEAD requests use the selected GET
route's rules.

## JSON fields

`readValidatedJsonBody` parses and checks declared object locations before the
handler reads the validated text. A `JsonParameterObject` contains a path and the
allowed member names at that path:

```cpp
http::ParameterRules rules {
    {},
    {
        { {}, { "sort", "tags" } },
        { { "sort" }, { "by", "order" } },
        { { "tags", "*" }, { "namespace", "subtag" } }
    }
};
```

An empty path selects the root object. A `*` segment visits array elements.
Warnings identify nested fields using JSON Pointer syntax, including array
indices and escaping for `/` and `~`. Missing objects or unexpected types are
left to the handler's existing validation. This check does not change the body.

Only declared object locations are inspected. Free-form secrets, layout documents,
and other application data remain unrestricted. Raw uploads and note text do not
pass through JSON-field checking. Compatibility routes and UI/static routes do
not receive native API parameter rules.

## Maintaining the lists

When adding a native method/path or changing the parameters a handler consumes,
update its declaration and OpenAPI contract together. All native route factories
pass through `declareEndpointParameters` before registration. A missing native
declaration throws during startup so a new endpoint cannot silently bypass the
policy. JSON endpoints must use `readValidatedJsonBody` for these checks.

At the HTTP-library level, absent `RouteMetadata::parameters` disables checking.
A present rules object with an empty query list allows no query names apart from
path captures; an empty JSON rule list means no JSON locations are inspected.
`RequestContext` borrows the rules from the sealed registry, whose lifetime spans
the request. Do not attach temporary rules to a running request.

Run `IDHANParameterTests` for decoding, nested fields, logging and rejection
policies. The API tests tagged `[parameters]` verify actual warning output and
accepted requests. Run `python3 tools/openapi/validate.py` to check the documented
contract and registered endpoint coverage.
