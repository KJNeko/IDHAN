# Reflected HTTP routes

Native routes use C++26 function annotations. CMake requires reflection support;
the GCC toolchain uses `-std=c++26 -freflection`.

Create a header anywhere under `IDHANServer/src`, for example
`httpapi/example.hpp`. The build scans headers for route annotations and generates
`gen_routes.hpp`, which includes all matching headers. No route list or CMake
source entry is needed.

```cpp
#pragma once
#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi::examples
{

struct ExampleQuery
{
    [[= http::Minimum{1}]] TagDomainID tag_domain_id;
    std::optional<bool> enabled;
};

struct ExampleBody
{
    std::string name;
    std::optional<std::vector<RecordID>> records;
};

struct ExampleResponse
{
    std::string name;
};

[[= http::Post{"/example"}]]
inline http::HTTPTask<http::JsonResponse<ExampleResponse, 201>> example(
    http::Query<ExampleQuery> query,
    http::JsonBody<ExampleBody> body)
{
    co_return http::JsonResponse<ExampleResponse, 201> { { std::move(body.value.name) } };
}

}
```

Endpoints live in `idhan::httpapi` or a child namespace. Reflection recursively
finds annotated functions from the generated includes. The same sorted function
snapshot drives native registration and compile-time OpenAPI generation.
Header-only function definitions must be `inline`. Alternatively, keep an
annotated declaration and its request/response DTOs in the header and implement
the function in a `.cpp` file with external linkage. Route annotations in `.cpp`
files are rejected by the header generator.

Use `Get`, `Post`, `Put`, `Patch`, or `Delete`, or
`Endpoint{proxygen::HTTPMethod::GET, "/path"}`. Multiple route annotations on one
function create aliases. The dependency reference must match the object passed
to `reflectedRoutes`; it must outlive the registry. The server supplies its
database context and common authentication validator automatically.

## Typed requests

- `Query<T>` reads query parameters. Vectors consume repeated keys; optional
  scalars may be absent; repeated scalar keys are errors.
- `Path<T>` matches members to route placeholders and validates captures during
  route matching. Mismatched field names fail registration.
- `Headers<T>` reads declared headers, including repeated values into vectors.
- `JsonBody<T>` decodes owned values before invoking the handler. Supported types
  include strings, booleans, integers, floating-point values, optional values,
  vectors, variants, string-keyed `std::map`s, and aggregate request structs.
- `RequestBody` transfers the existing buffer or temporary upload to a handler
  that consumes raw bytes. It does not materialize temporary uploads in memory.
- `RequestContext` remains available for response headers, streaming, or custom
  behavior. It moves into the handler after argument decoding completes.

Declare query parameters on the handler as `http::Query<T>`; no `Parameters<T>`
annotation or per-field `RequestParameters` parsing is needed. The router decodes
and validates the complete struct before invoking the handler, returning HTTP 400
for malformed values. The same fields supply the OpenAPI query schema.

```cpp
struct FlagsQuery
{
    http::possiblyMissing<bool> scan_mime;
    http::possiblyMissing<std::size_t> concurrency;
};

http::HTTPTask<http::Response> scan(http::Query<FlagsQuery> query);

// Inside the handler:
const bool scan_mime = query.value.scan_mime.value_or(true);
const auto concurrency = query.value.concurrency.value_or(4);
```

`possiblyMissing<T>` inherits `value_or` from `std::optional<T>`. A supplied
`false` or zero is retained; the fallback is used only when the field is absent.

Only one body argument is allowed. Missing and JSON `null` `std::optional` fields both
produce `std::nullopt`, including when the member has a default initializer.
Non-optional fields with a default member initializer retain that default when
absent; other non-optional fields are required. Explicit `null` is invalid for
ordinary non-optional values. `JsonDocument` represents arbitrary JSON, including
`null`, and owns its serialized text.

`[[= http::Name{"namespace"}]]` supplies a wire name that differs from the C++
member name. `[[= http::Minimum{1}]]` requires IDs to be at least one;
`[[= http::Minimum{0}]]` allows zero for counts; `[[= http::Maximum{64}]]` rejects
larger values. Optional fields are checked when present, and array constraints
apply to every element. Application
checks such as record existence, matching array lengths, discriminator rules,
and precedence between fields remain named validation or handler logic.

Reflected structs, arrays, and variant alternatives generate nested unknown-field
rules. Unknown fields warn by default; submitted values are not logged. Dynamic
map keys and `JsonDocument` contents are open-ended. Variants try alternatives in
declaration order; use a custom codec when a discriminator selects an alternative.

## Custom parsing and validation

Specialize `http::JsonCodec<T>` with
`static std::expected<T, http::ParseError> decode(simdjson::dom::element, std::string path)`
for specialized JSON representations. Return owned values. A codec can also
provide `static std::vector<http::JsonParameterObject> rules(std::vector<std::string> path)`
when its wire shape differs from its reflected members.

Context-based handlers can retain `RequestContext` and their parser. Annotate the
request body with `[[= http::Input<Body>{}]]` and query parameters independently
with `[[= http::Parameters<QuerySchema>{}]]`. Neither annotation describes a
response. Omit `Input` for routes without a body; `Input<>` explicitly accepts an
arbitrary JSON document. Both input and output default to `http::JsonDocument`
and `application/json`:

```cpp
[[= http::Post{"/records/example"}]]
[[= http::Parameters<ExampleQuery>{}]]
[[= http::Input<ExampleBody>{}]]
[[= http::Output<ExampleResponse>{}]]
http::HTTPTask<http::Response> example(db::DBContext&, http::RequestContext);
```

Use `Output<>` for a JSON response without a concrete DTO. `Output<T>{Status, "media/type"}` describes other statuses or
media types. For example,
`Output<http::RequestBody>{200, "image/webp"}` describes thumbnail bytes and
`Output<void>{204}` describes an empty response. Multiple `Output` annotations
can describe alternative responses. `Input<T>{"media/type", Required}` describes
raw uploads or an optional body. These annotations describe the wire contract;
context-based handlers still set their actual response headers through
`Response::json`, `Response::bytes`, or their existing response builder.

Typed `JsonBody<T>`, `Query<T>`, `JsonResponse<T>`, and `EmptyResponse<Status>`
arguments and return types infer their contracts without these annotations.
Annotation values use object initialization: `Name{"namespace"}`,
`Get{"/records"}`, `Before{&validatorFactory}`, and
`Validation<validation::PositiveInteger<RecordID>>{"record_id"}`.
Only C++ types remain explicit template arguments on these annotations. Text,
paths, statuses, media types, and callback pointers are object members. Reusable
annotations can be declared as `inline constexpr` objects.

`JsonDocument` fields leave decoding to the existing parser. Parser-host mutation
handlers and `/records/tags/add` use typed arguments directly.

Group validators run before body reads. `[[= http::Public{}]]` removes the common
group validators for public routes. `[[= http::Before{&validatorFactory}]]` adds
an endpoint-specific pre-body validator, constructed with the group dependency.
Validate URL placeholders independently of request bodies and query parameters:

```cpp
[[= http::Get{"/records/{record_id}"}]]
[[= http::Validation< http::validation::PositiveInteger<RecordID> >{"record_id"}]]
```

Premade types are `validation::Integer<T>`, `validation::PositiveInteger<T>`, and
`validation::UUID`. Integer validators check the complete value and the range of
`T`; positive integers additionally reject zero and negatives. UUID validation
accepts the canonical 36-character hexadecimal shape, including uppercase hex.
Invalid values return 400 before the handler runs. The annotation must name an
existing placeholder. OpenAPI uses the same type, range, positivity, and UUID
format constraints. Database existence checks remain separate `Before` validators.
Attach shared annotations only once when a function has multiple route aliases.

UI/static and Hydrus compatibility routes retain their existing registration and
parameter policies. Native authentication query names are supplied by the server
group registration rather than hardcoded into the HTTP library.

## Typed responses

Handlers can return `HTTPTask<JsonResponse<T, Status>>`. `Status` defaults to 200;
the adapter serializes the value and produces an `application/json` response.

```cpp
struct CreatedRecord
{
    RecordID record_id;
    std::optional<std::string> filename;
};

using CreateResult = std::variant<
    http::JsonResponse<CreatedRecord, 201>,
    http::Response>;

// Inside an annotated handler returning HTTPTask<CreateResult>:
http::JsonResponse<CreatedRecord, 201> response { { record_id, std::nullopt } };
response.headers.add("Location", "/records/" + std::to_string(record_id));
co_return response;
```

`JsonResponse::value` is the payload; `headers` uses Proxygen's header type and
preserves repeated headers. `EmptyResponse<204>` represents a bodyless response.
Variants allow different typed status/payload alternatives, including existing
`Response` values for errors, files, and streams. Errors use the shared JSON error object.

Serialization supports aggregate structs, wire-name annotations, nested vectors,
maps, variants, optionals, and enum names. A disengaged `std::optional` emits JSON `null`;
a disengaged `http::possiblyMissing` omits its object field. Non-finite numbers, unknown enum values, invalid
UTF-8, and invalid `JsonDocument` contents produce a server error rather than
invalid JSON. `serializeJson(value)` also exposes serialization independently of
HTTP as an owned string in `std::expected`.

A custom `JsonCodec<T>::encode(builder, value)` can append one JSON value and
return `std::expected<void, ParseError>`. Output encoding and input decoding can
be supplied independently. Response values should own their data through
serialization; prefer `std::string` to views into handler-local storage.

See [the source-generated OpenAPI plan](source-openapi-plan.md) for the next step.

`Response::error(status, message)` serializes an `ErrorResponse` object as
`application/json`: `{"error":"message"}`. Shared authentication and validation
errors use this format too. Explicit error declarations use
`Output<http::ErrorResponse>{404}` (or the corresponding status).

For response fields, `http::possiblyMissing<T>` controls presence, while
`std::optional<T>` controls nullability:

```cpp
struct ExampleResponse
{
    http::possiblyMissing<std::string> nickname;
    std::optional<std::string> description;
    http::possiblyMissing<std::optional<std::string>> note;
};
```

An empty `nickname` or `note` is omitted; an empty `description` is emitted as
`"description": null`. `note.emplace(std::nullopt)` emits `"note": null`.
OpenAPI marks omittable fields as optional without implicitly allowing null.
`http::possiblyMissing<T>` accepts missing input fields, but an explicit null must be
supported by `T` (for example, `std::optional<T>` or `JsonDocument`). Existing
`std::optional<T>` input fields continue to accept both missing and null values.
An absent `http::possiblyMissing` is only meaningful for object members, including map
entries; serializing one as the root value or an array item returns an error.

Group endpoints in the documentation with `[[= http::Group{"Tags"}]]` on their
function declarations. This emits the OpenAPI operation's `tags` list, which the
API viewer uses to group routes. For example:

```cpp
[[= http::Get{"/tags/info"}]]
[[= http::Group{"Tags"}]]
http::HTTPTask<http::Response> getTagInfoFromQuery(db::DBContext&, http::Query<GetTagInfoFromQueryQuery>);
```

Multiple `Group` annotations place an endpoint in multiple groups; repeated names
are emitted once. Groups are documentation metadata and do not change URL paths
or authentication. Named `inline constexpr http::Group` objects can be reused.
