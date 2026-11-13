export const manifest = {
    cookies: [{name: "fringeBenefits", value: "yup", domain: "127.0.0.1", path: "/"}],
};

export async function gallery(input, idhan) {
    const response = await idhan.request({
        url: new URL("/gallery.json", input.url).href,
        responseType: "json",
    });

    idhan.follow({url: `${response.body.post}?source=one`});
    idhan.follow({url: `${response.body.post}?source=two`});
}

export function post(input, idhan) {
    const query = new URL(input.url).searchParams;
    idhan.queueImport({
        request: {url: new URL("/file.gif", input.url).href},
        filename: "fixture.gif",
        postedAt: query.has("postedAt") ? Number(query.get("postedAt")) : undefined,
        urls: [{url: input.url, type: "post"}],
        discoveredUrls: [new URL("/source", input.url).href],
        tags: query.has("multipleDomains")
            ? {"fixture-first": ["multi:first"], "fixture-second": ["multi:second"]}
            : query.has("domain") ? {[query.get("domain")]: ["fixture"]} : ["fixture"],
    });
}

export function failure() {
    throw new Error("deterministic parser failure");
}

export default class ApiFixture {
    static metadata = {
        formatVersion: 1,
        sdkVersion: "1.0.0",
        id: "fixture",
        version: 1,
        options: [],
        handlers: [{id: "echo", method: "echo", parameters: {query: {name: "Query", type: "string"}}}],
        discoveries: [],
        urlClasses: []
    };
}

export async function run(input, host) {
    const module = await import("./fixture.js");
    const handler = module[input.operation];
    if (typeof handler !== "function") throw new Error(`Missing fixture operation: ${input.operation}`);
    return handler(input, host);
}

export function echo(input) {
    if (input.params.query !== "  raw query, untouched  ") throw new Error("Generator parameters were changed");
}
