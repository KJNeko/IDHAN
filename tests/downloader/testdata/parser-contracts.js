import * as e621 from "./e621.js";
import * as gelbooru from "./gelbooru.js";
import * as hyshare from "./hyshare.js";
import * as pawchive from "./pawchive.js";
import * as pixiv from "./pixiv.js";
import {filenameFromUrl} from "parser";
import {idSchema, recordsSchema, requestJson, z} from "validation";

function equal(actual, expected, description) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${description}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
}

async function rejects(action, message) {
    try {
        await action();
    } catch (error) {
        if (!error.message.includes(message)) throw error;
        return;
    }
    throw new Error(`Expected rejection containing: ${message}`);
}

class Host {
    constructor(responses = [], secrets = {}) {
        this.responses = responses;
        this.secrets = secrets;
        this.requests = [];
        this.imports = [];
        this.follows = [];
    }

    async request(options) {
        equal(options.responseType, "text", "API response type");
        this.requests.push(options);
        if (this.responses.length === 0) throw new Error(`Unexpected request: ${options.url}`);

        const response = this.responses.shift();
        return {status: 200, url: options.url, ...response};
    }

    import(options) {
        this.imports.push(options);
    }

    follow(options) {
        this.follows.push(options);
    }

    secret(name) {
        return this.secrets[name] ?? null;
    }
}

function json(body, options = {}) {
    return {body: JSON.stringify(body), ...options};
}

function checkImport(options, source, file, domain) {
    equal(options.request.url, file, "File request URL");
    equal(options.request.referer, source, "File request referer");
    equal(options.urls, [{url: source, type: "post"}, {url: file, type: "file"}], "Typed source/file URLs");
    equal(Object.keys(options.tags), [domain], "Tag domain");
}

export async function e621Contract() {
    const host = new Host([json({
        post: {
            id: 1,
            file: {url: "https://static.example/file.png", size: 123},
            tags: {general: ["cat"], artist: ["example"]},
            relationships: {parent_id: 2, children: [1, 3]},
            sources: ["https://source.example/1", "https://source.example/1"],
        }
    })]);
    await e621.post({url: "https://e621.net/posts/1", flags: {"e621.followSiblings": true}}, host);
    equal(host.imports.length, 1, "e621 imports");
    checkImport(host.imports[0], "https://e621.net/posts/1", "https://static.example/file.png", "e621");
    equal(host.imports[0].filename, "file.png", "e621 filename");
    equal(host.imports[0].tags.e621, ["cat", "artist:example"], "e621 tag namespaces");
    equal(host.imports[0].discoveredUrls, ["https://source.example/1"], "e621 source deduplication");
    equal(host.imports[0].metadata.expectedSize, 123, "e621 metadata");
    equal(host.follows.map((entry) => entry.url), ["https://e621.net/posts/2", "https://e621.net/posts/3"], "e621 siblings");

    const gallery = new Host([json({posts: [{id: 1}]})]);
    await e621.gallery({url: "https://e621.net/posts?tags=cat"}, gallery);
    equal(new URL(gallery.requests[0].url).searchParams.get("tags"), "cat", "e621 gallery query");
    equal(gallery.follows, [{url: "https://e621.net/posts/1", type: "post"}], "e621 gallery follows");
}

export async function gelbooruContract() {
    const host = new Host([
        json({post: {id: 1, file_url: "/images/file.jpg", tags: "cat blue_eyes", source: "https://source.example/1"}}),
        json({tag: [{name: "cat", type: 0}, {name: "blue_eyes", type: 4}]}),
    ], {"gelbooru.apiKey": "fixture-key", "gelbooru.userID": "123"});
    await gelbooru.post({
        url: "https://gelbooru.com/index.php?page=post&s=view&id=1",
        flags: {"gelbooru.followSource": true},
    }, host);
    equal(host.imports.length, 1, "Gelbooru imports");
    checkImport(host.imports[0], "https://gelbooru.com/index.php?page=post&s=view&id=1", "https://gelbooru.com/images/file.jpg", "gelbooru");
    equal(host.imports[0].tags.gelbooru, ["cat", "character:blue_eyes"], "Gelbooru tag categories");
    equal(host.follows, [{url: "https://source.example/1"}], "Gelbooru source follow");
    equal(host.requests[0].sensitiveQuery, ["api_key", "user_id"], "Gelbooru credential redaction");

    const denied = new Host([{status: 403, body: "Forbidden"}], host.secrets);
    await rejects(() => gelbooru.post({url: "https://gelbooru.com/index.php?id=1", flags: {}}, denied), "HTTP 403");
    equal(denied.imports.length, 0, "Gelbooru failure must not import");
}

export async function pixivContract() {
    const host = new Host([json({
        error: false, body: {
            illust_details: {
                id: 1, ugoira_meta: null, url_big: "https://i.pximg.net/original/1_p0.png", page_count: 2,
                tags: ["landscape", "landscape"], title: "Example",
                author_details: {user_id: 2, user_name: "Example artist", user_account: "artist"},
            }
        }
    })]);
    await pixiv.post({url: "https://www.pixiv.net/en/artworks/1"}, host);
    equal(host.imports.length, 2, "Pixiv page count");
    for (let page = 0; page < 2; ++page) {
        checkImport(host.imports[page], "https://www.pixiv.net/artworks/1", `https://i.pximg.net/original/1_p${page}.png`, "pixiv");
        equal(host.imports[page].filename, `1_p${page}.png`, "Pixiv page filename");
        equal(host.imports[page].metadata.page, page, "Pixiv page metadata");
        equal(host.imports[page].tags.pixiv, ["landscape", "creator:Example artist"], "Pixiv tags");
    }

    const artist = new Host([json({error: false, body: {illusts: {1: null}, manga: [2]}})]);
    await pixiv.user({url: "https://www.pixiv.net/users/2"}, artist);
    equal(artist.follows.map((entry) => entry.url), ["https://www.pixiv.net/artworks/1", "https://www.pixiv.net/artworks/2"], "Pixiv artist follows");
}

export async function hyshareContract() {
    const hash = "a".repeat(64);
    const source = `https://testing.mydomain.com/view/${hash}`;
    const host = new Host([json({hash, ext: ".png", tag_services_to_tags: {one: ["cat"], two: ["cat", "favorite"]}}, {
        url: `${source}/data.json`,
    })]);
    await hyshare.post({url: `https://original.example/view/${hash}`}, host);
    checkImport(host.imports[0], source, `https://testing.mydomain.com/file/${hash}`, "testing.mydomain.com");
    equal(host.imports[0].tags["testing.mydomain.com"], ["cat", "favorite"], "HyShare tag deduplication");

    const gallery = new Host([json({hashes: [hash]}, {url: "https://redirected.example/gallery/cats/data.json"})]);
    await hyshare.gallery({url: "https://original.example/gallery/cats?ignored=1"}, gallery);
    equal(gallery.follows, [{url: `https://redirected.example/view/${hash}`, type: "post"}], "HyShare redirect base");

    const discovery = new Host([json({hyshare: "1"})]);
    await hyshare.checkWellKnown({url: `https://testing.mydomain.com/view/${hash}`}, discovery);
    equal(discovery.requests[0].url, "https://testing.mydomain.com/.well-known/hyshare", "HyShare discovery root");

    const mismatch = new Host([json({hash: "b".repeat(64), ext: ".png"})]);
    await rejects(() => hyshare.post({url: source}, mismatch), "instead of");
    equal(mismatch.imports.length, 0, "HyShare hash mismatch must not import");
}

export async function pawchiveContract() {
    globalThis.pawchiveResponses = [{
        title: "Example", published: new Date("2024-01-02T03:04:05Z"),
        file: {name: "main.png", path: "/main.png"}, attachments: [{name: "extra.jpg", path: "/extra.jpg"}],
    }];
    globalThis.pawchiveCalls = [];
    const host = new Host();
    await pawchive.route({
        url: "https://pawchive.pw/patreon/user/2/post/1",
        flags: {"pawchive.importAttachments": true}
    }, host);
    equal(host.imports.length, 2, "pawchive attachments");
    checkImport(host.imports[0], "https://pawchive.pw/patreon/user/2/post/1", "https://file.pawchive.pw/data/main.png?f=main.png", "pawchive");
    equal(host.imports[1].filename, "extra.jpg", "pawchive original attachment filename");
    equal(host.imports[1].metadata.attachment, 0, "pawchive attachment index");
    equal(host.imports[0].metadata.published, "2024-01-02T03:04:05.000Z", "pawchive date metadata");
    equal(host.imports[0].tags.pawchive, ["service:patreon", "creator:2"], "pawchive tags");

    globalThis.pawchiveResponses = [Array.from({length: 50}, (_, id) => ({id: String(id)})), []];
    globalThis.pawchiveCalls = [];
    const creator = new Host();
    await pawchive.route({url: "https://pawchive.pw/patreon/user/2", flags: {}}, creator);
    equal(creator.follows.length, 50, "pawchive creator follows");
    equal(globalThis.pawchiveCalls.map((entry) => entry.at(-1).o), [0, 50], "pawchive pagination offsets");
}

export async function helpersContract() {
    const schema = recordsSchema("post", z.object({id: idSchema}));
    for (const value of [[{id: 1}], {post: {id: 1}}, {post: [{id: 1}]}]) {
        equal(schema.parse(value), [{id: 1}], "Record envelope normalization");
    }
    for (const value of [null, {}, {post: null}, {post: []}]) {
        equal(schema.parse(value), [], "Empty record envelopes");
    }
    equal(schema.safeParse({post: 42}).success, false, "Invalid record envelope");

    const source = new URL("https://source.example/post/1");
    const file = new URL("https://files.example/a%20b.png?signature=secret");
    equal(filenameFromUrl(file), "a%20b.png", "URL filename excludes query");

    await rejects(() => requestJson(new Host([{
        status: 404,
        body: "missing"
    }]), z.object({}), "Fixture API", {url: source.href}), "HTTP 404");
    await rejects(() => requestJson(new Host([{body: "<html>failure</html>"}]), z.object({}), "Fixture API", {url: source.href}), "unparseable body");
}
