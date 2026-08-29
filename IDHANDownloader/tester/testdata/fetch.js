export const manifest = {
    id: "test.fetch",
    version: 1,
};

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

export async function test(input) {
    const headers = new Headers({"X-IDHAN-Test": "fetch"});
    headers.append("X-IDHAN-Test", "callback");
    const request = new Request(new URL("/anything", input.url), {
        method: "POST",
        headers,
        body: new Uint8Array([65, 0, 66]),
    });
    const response = await fetch(request);
    const clone = response.clone();
    const bytes = await clone.arrayBuffer();
    const body = await response.json();

    assert(response instanceof Response, "fetch must return a Response");
    assert(response.ok && response.status === 200, `Unexpected fetch status ${response.status}`);
    assert(response.bodyUsed, "Reading JSON must consume the response body");
    assert(bytes.byteLength > 0, "Cloned response must retain its binary body");
    assert(body.method === "POST", `Expected POST, received ${body.method}`);
    assert(body.data.length === 3 && body.data.charCodeAt(1) === 0, "Typed-array request body was not preserved");
    assert(body.headers["X-Idhan-Test"] === "fetch, callback", "Headers append behavior was not preserved");
    assert(body.headers["User-Agent"].startsWith("IDHAN/"), "Fetch bypassed the IDHAN User-Agent");

    const missing = await fetch(new URL("/status/404", input.url));
    assert(missing.status === 404 && !missing.ok, "Fetch must resolve HTTP error responses");

    const redirectUrl = new URL("/redirect-to?url=%2Fget", input.url);
    const redirected = await fetch(redirectUrl);
    assert(redirected.redirected, "Fetch did not report a followed redirect");
    assert(new URL(redirected.url).pathname === "/get", `Unexpected redirect destination ${redirected.url}`);

    const manual = await fetch(redirectUrl, {redirect: "manual"});
    assert(manual.status === 302, `Expected a manual 302 response, received ${manual.status}`);
    assert(manual.headers.get("location") === "/get", "Manual redirect did not expose Location");

    return {complete: true};
}
