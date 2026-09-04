export const manifest = {
    id: "test.url",
    version: 1,
};

function expect(actual, expected, description) {
    if (actual !== expected) {
        throw new Error(`${description}: expected '${expected}', received '${actual}'`);
    }
}

export function test() {
    const url = new URL("../image file.jpg?x=one+two#part", "https://Example.COM:443/a/b/");

    expect(url.href, "https://example.com/a/image%20file.jpg?x=one+two#part", "relative URL");
    expect(url.origin, "https://example.com", "origin");
    expect(url.protocol, "https:", "protocol");
    expect(url.hostname, "example.com", "hostname");
    expect(url.pathname, "/a/image%20file.jpg", "pathname");
    expect(url.searchParams.get("x"), "one two", "decoded search parameter");

    const liveParameters = url.searchParams;
    liveParameters.set("page", "post");
    expect(url.search, "?x=one+two&page=post", "parameter mutation updates search");

    url.search = "?replacement=yes";
    expect(liveParameters.get("replacement"), "yes", "search updates existing parameter view");

    url.pathname = "/new path";
    url.hash = "fragment";
    expect(url.href, "https://example.com/new%20path?replacement=yes#fragment", "component setters");
    expect(url.toJSON(), url.href, "URL serialization");

    const parameters = new URLSearchParams("?b=2&a=1&a=3");
    expect(parameters.size, 3, "parameter count");
    expect(parameters.getAll("a").join(","), "1,3", "getAll");
    expect(parameters.has("b"), true, "has");
    parameters.append("c", "hello world");
    parameters.delete("b");
    parameters.sort();
    expect(parameters.toString(), "a=1&a=3&c=hello+world", "parameter serialization");

    return {complete: true};
}
