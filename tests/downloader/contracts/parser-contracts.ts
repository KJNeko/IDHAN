import E621 from "../premade/e621/index.js";
import Gelbooru from "../premade/gelbooru/index.js";
import HyShare from "../premade/hyshare/index.js";
import Pixiv from "../premade/pixiv/index.js";
import Pawchive from "../premade/pawchive/index.js";
import {bindParser} from "./sdk/runtime.js";
import type {NativeHost, ParserConstructor} from "./sdk/runtime.js";
import type {FileImport, ImportHandle, MetadataValue, RequestOptions, TextResponse} from "./sdk/session.js";

function check(condition: unknown, message: string): asserts condition {
    if (!condition) throw new Error(message);
}

class Host implements NativeHost {
	private readonly values = new Map<string, MetadataValue>();

	kv(operation: "get" | "set" | "delete", parser: string, key: string, value?: MetadataValue): unknown {
		const scoped = JSON.stringify([parser, key]);
		if (operation === "get") return this.values.get(scoped);
		if (operation === "delete") return this.values.delete(scoped);
		this.values.set(scoped, JSON.parse(JSON.stringify(value)) as MetadataValue);
	}

    private readonly parsers = new Map<string, object>();

    parserState(parser: string, origin: string, secrets: Readonly<Record<string, MetadataValue>>): object {
        const key = JSON.stringify([parser, origin, secrets]);
        let state = this.parsers.get(key);
        if (state === undefined) {
            state = {};
            this.parsers.set(key, state);
        }
        return state;
    }

    readonly requests: RequestOptions[] = [];
    readonly imports: FileImport[] = [];
	readonly importOutcomes: boolean[] = [];
    readonly follows: string[] = [];
    readonly followTags: Readonly<Record<string, readonly string[]>>[] = [];
    readonly cookies: Record<string, string> = {};

    constructor(
        private readonly bodies: unknown[],
        private readonly credentials: Record<string, MetadataValue> = {}
    ) {
    }

    secrets(): Record<string, MetadataValue> {
        return {...this.credentials};
    }

    setCookie(name: string, value: string): void {
        this.cookies[name] = value;
    }

    learnHost(): void {
    }

    async request(options: RequestOptions): Promise<TextResponse> {
        check(options.responseType === "text", "API requests must return text");
        check(this.bodies.length > 0, "Unexpected API request");
        this.requests.push(options);
        return {status: 200, url: options.url, headers: {}, body: JSON.stringify(this.bodies.shift())};
    }

	queueImport(options: FileImport | { request: { url: string }; requireFile: boolean }): ImportHandle {
        check("filename" in options, "Generator unexpectedly used file fallback");
        this.imports.push(options);
		const existing = this.importOutcomes.shift() ?? false;
		let completion: Promise<{ existing: boolean }> | undefined;
		return {
			get importFinished() {
				return completion ??= Promise.resolve({existing});
			}
		};
    }

	async importDirect(): Promise<void> {
		throw new Error("Unexpected direct import in parser contract fixture");
	}

    follow(options: { url: string; tags?: Readonly<Record<string, readonly string[]>> }): void {
        this.follows.push(options.url);
        this.followTags.push(options.tags ?? {});
    }
}

async function invoke(
    Parser: ParserConstructor,
    host: Host,
    url: string,
    operation: string,
    params: Record<string, string>,
    options: Record<string, unknown> = {}
): Promise<void> {
    await bindParser(Parser)({url, domain: new URL(url).origin, operation, params, options}, host);
}

async function e621Contract(): Promise<void> {
    const host = new Host([{
        post: {
            id: 1, file: {url: "https://files.example/file.png", size: 123},
            tags: {general: ["cat"], artist: ["example"]},
            relationships: {parent_id: 2, children: [1, 3]}, sources: []
        }
    }]);
    await invoke(E621, host, "https://e621.net/posts/1", "post", {id: "1"}, {followSiblings: true});
    check(host.imports.length === 1, "e621 did not import the file");
    check([...host.follows].sort().join(",") === "https://e621.net/posts/2,https://e621.net/posts/3", "e621 sibling options were lost");
    const gallery = new Host([{posts: [{id: 1}]}]);
    await invoke(E621, gallery, "https://e621.net/posts?tags=cat", "search", {tags: "cat"});
    check(gallery.follows[0] === "https://e621.net/posts/1", "e621 search did not follow its result");
}

async function gelbooruContract(): Promise<void> {
    const host = new Host([
        {
            post: {
                id: 1,
				created_at: "Sat Dec 14 04:00:44 -0600 2024",
                file_url: "/images/file.jpg?one=1&amp;two=2",
                tags: "artist&#039;s red_dress character_name series_name official_artwork fish&amp;chips &amp;lt; &#x1F600; &NotEqualTilde; &#128; &unknown;",
                source: "http://pixiv.net/member_illust.php?mode=big&amp;illust_id=123"
            }
        },
        {
            tag: [{name: "artist&apos;s", type: 1}, {name: "red_dress", type: 0}, {
                name: "character_name",
                type: 4
            }, {name: "series_name", type: 3}, {name: "official_artwork", type: 5}]
        }
    ], {apiKey: "fixture-key", userID: "123"});
    await invoke(Gelbooru, host, "https://gelbooru.com/index.php?page=post&s=view&id=1", "post", {id: "1"}, {followSource: true});
    check(host.imports.length === 1, "Gelbooru did not import the file");
    check(host.imports[0]?.request.url === "https://gelbooru.com/images/file.jpg?one=1&two=2",
        "Gelbooru must decode file URLs before requesting them");
	check(host.imports[0]?.postedAt === Date.parse("2024-12-14T10:00:44Z") / 1000,
		"Gelbooru must supply the source post date, preserving the source timezone");
    check(host.cookies.fringeBenefits === "yup", "Decorated cookies were not applied");
    check(new URL(host.requests[0]!.url).searchParams.get("api_key") === "fixture-key", "Domain credentials were not used");
    check(host.follows[0] === "http://pixiv.net/member_illust.php?mode=big&illust_id=123", "Gelbooru source option was ignored");
    check(host.followTags[0]?.["gelbooru.com"]?.join(",") === "creator:artist's", "Gelbooru must pass only creator tags to its source follow");
    const importedTags = host.imports[0]?.tags["gelbooru.com"] ?? [];
    check(host.imports[0]?.discoveredUrls?.[0] === host.follows[0], "Gelbooru stored an escaped source URL");
    check(importedTags.includes("fish&chips") && importedTags.includes("&lt;") && importedTags.includes("😀")
        && importedTags.includes("≂̸") && importedTags.includes("€") && importedTags.includes("&unknown;"),
        "Gelbooru must decode API entities exactly once");
    check(importedTags.includes("red_dress") && importedTags.includes("character:character_name")
        && importedTags.includes("copyright:series_name") && importedTags.includes("meta:official_artwork"),
        "Gelbooru must retain variant-specific tags on its own import");
}

async function gelbooruCacheContract(): Promise<void> {
	const post = {id: 1, file_url: "/images/file.jpg", tags: "artist general"};
	const host = new Host([
		{post}, {tag: [{name: "artist", type: 1}, {name: "general", type: 0}]},
		{post},
		{post: {...post, tags: "artist general new_tag"}}, {tag: [{name: "new_tag", type: 4}]},
		{post}
	], {apiKey: "fixture-key", userID: "123"});
	const url = "https://gelbooru.com/index.php?page=post&s=view&id=1";
	await invoke(Gelbooru, host, url, "post", {id: "1"});
	await invoke(Gelbooru, host, url, "post", {id: "1"});
	check(host.requests.length === 3, "Cached tag types must skip the second tag API request");
	await invoke(Gelbooru, host, url, "post", {id: "1"});
	check(new URL(host.requests[4]!.url).searchParams.get("names") === "new_tag",
		"Only uncached tags should be requested");
	check(host.imports[2]!.tags["gelbooru.com"]!.join(",") === "creator:artist,general,character:new_tag",
		"Cached and newly fetched tag types must both be applied");
	await invoke(Gelbooru, host, "https://other.example/index.php?page=post&s=view&id=1", "post", {id: "1"});
	check(Number(host.requests.length) === 6, "The same parser must reuse cached tags across site origins");
	check(host.imports[3]!.tags["other.example"]!.join(",") === "creator:artist,general",
		"Cross-origin cached tag types must be applied");
}

function gelbooruSearchPosts(count: number, highest: bigint = 5000n) {
	return Array.from({length: count}, (_, index) => ({
		id: String(highest - BigInt(index) * 3n), file_url: `/images/${index}.jpg`, tags: ""
	}));
}

async function gelbooruSubscriptionContract(): Promise<void> {
	const search = Gelbooru.metadata.handlers.find(handler => handler.id === 'search');
	check(search !== undefined, "Gelbooru search generator is missing");
	check(!('offset' in search.parameters), "Search generators must not expose a starting offset");
	const auth = {apiKey: "fixture-key", userID: "123"};
	const url = "https://gelbooru.com/index.php?page=post&s=list&tags=artist_name%20bed";
	const posts = gelbooruSearchPosts(27);
	const full = new Host([posts.slice(0, 25), posts.slice(25)], auth);
	full.importOutcomes.push(...Array<boolean>(27).fill(true));
	await invoke(Gelbooru, full, url, "search", {tags: "artist_name bed"});
	check(full.imports[0]?.postedAt === undefined, "Missing post dates must stay unknown");
	check(full.imports.length === 27, "Searches must exhaust every page even when every file is known");
	check(new URL(full.requests[0]!.url).searchParams.get("tags") === "artist_name bed sort:id:desc",
		"Original tag filters must be retained with descending ID order");
	check(new URL(full.requests[1]!.url).searchParams.get("tags") === `artist_name bed sort:id:desc id:<${posts[24]!.id}`,
		"The next batch must exclude the last ID from the preceding batch");
	check(full.requests.every(request => new URL(request.url).searchParams.get("pid") === "0"),
		"ID pagination must always request page zero");

	// Cross the API's 1000-post offset ceiling, with sparse IDs beyond Number's exact integer range.
	const deepPosts = gelbooruSearchPosts(1102, 9007199254749999n);
	const pages = Array.from({length: Math.ceil(deepPosts.length / 25)}, (_, page) => deepPosts.slice(page * 25, (page + 1) * 25));
	const deep = new Host(pages, auth);
	await invoke(Gelbooru, deep, url, "search", {tags: "artist_name id:>100 sort:score:desc"});
	check(deep.imports.length === deepPosts.length, "Searches must continue beyond 1000 posts");
	check(new Set(deep.imports.map(file => file.metadata.postId)).size === deepPosts.length,
		"ID boundaries must not duplicate or skip sparse post IDs");
	for (const [index, request] of deep.requests.entries()) {
		const query = new URL(request.url).searchParams;
		const boundary = index === 0 ? "" : ` id:<${deepPosts[index * 25 - 1]!.id}`;
		check(query.get("pid") === "0", "Deep searches must stay below the offset cap");
		check(query.get("tags") === `artist_name id:>100 sort:id:desc${boundary}`,
			"Deep pagination must preserve filters and use an exact exclusive ID boundary");
	}

	const limited = new Host([posts.slice(0, 25)], auth);
	await invoke(Gelbooru, limited, url, "search", {tags: ""}, {pageLimit: 1});
	check(limited.requests.length === 1 && limited.imports.length === 25, "pageLimit must still count fetched batches");

	const exact = new Host([posts.slice(0, 25), []], auth);
	await invoke(Gelbooru, exact, url, "search", {tags: ""});
	check(exact.requests.length === 2 && exact.imports.length === 25, "An empty page must end an exact full batch");

	const ignored = new Host([posts.slice(0, 25), posts.slice(0, 25)], auth);
	let rejected = false;
	try {
		await invoke(Gelbooru, ignored, url, "search", {tags: ""});
	} catch (error) {
		rejected = String(error).includes("strictly decreasing IDs");
	}
	check(rejected && ignored.imports.length === 25, "An ignored ID filter must fail before duplicate imports");
}

async function gelbooruSecretsContract(): Promise<void> {
    const credentials: Record<string, MetadataValue> = {};
    const post = {post: {id: 1, file_url: "/images/file.jpg", tags: "cat", source: ""}};
    const tags = {tag: [{name: "cat", type: 0}]};
    const bodies: unknown[] = [post, tags];
    const host = new Host(bodies, credentials);
    const url = "https://gelbooru.com/index.php?page=post&s=view&id=1";
    let missing = false;
    try {
        await invoke(Gelbooru, host, url, "post", {id: "1"});
    } catch (error) {
        missing = String(error).includes("Missing downloader secrets");
    }
    check(missing, "Gelbooru did not report missing credentials");
    check(host.requests.length === 0, "Gelbooru requested the API without credentials");

    credentials.apiKey = "fixture-added-key";
    credentials.userID = "123";
    await invoke(Gelbooru, host, url, "post", {id: "1"});
    check(host.imports.length === 1, "Gelbooru did not pick up newly added secrets");
    const first = new URL(host.requests[0]!.url);
    check(first.searchParams.get("api_key") === credentials.apiKey, "Gelbooru ignored the added API key");
    check(first.searchParams.get("user_id") === credentials.userID, "Gelbooru ignored the added user ID");

    credentials.apiKey = "fixture-rotated-key";
    credentials.userID = "456";
    bodies.push(post, tags);
    const previous = host.requests.length;
    await invoke(Gelbooru, host, url, "post", {id: "1"});
    const rotated = new URL(host.requests[previous]!.url);
    check(rotated.searchParams.get("api_key") === credentials.apiKey, "Gelbooru retained the old API key");
    check(rotated.searchParams.get("user_id") === credentials.userID, "Gelbooru retained the old user ID");

    delete credentials.apiKey;
    missing = false;
    try {
        await invoke(Gelbooru, host, url, "post", {id: "1"});
    } catch (error) {
        missing = String(error).includes("Missing downloader secrets");
    }
    check(missing, "Gelbooru kept using a removed API key");
}

async function e621SecretsContract(): Promise<void> {
    const credentials: Record<string, MetadataValue> = {};
    const host = new Host([{posts: []}, {posts: []}, {posts: []}], credentials);
    const url = "https://e621.net/posts?tags=cat";
    await invoke(E621, host, url, "search", {tags: "cat"});
    check(host.requests[0]!.headers?.Authorization === undefined, "Anonymous request used authentication");
    credentials.username = "user";
    credentials.apiKey = "pass";
    await invoke(E621, host, url, "search", {tags: "cat"});
    check(host.requests[1]!.headers?.Authorization === "Basic dXNlcjpwYXNz", "Added secrets did not refresh setup authentication");
    delete credentials.username;
    delete credentials.apiKey;
    await invoke(E621, host, url, "search", {tags: "cat"});
    check(host.requests[2]!.headers?.Authorization === undefined, "Removed secrets remained in setup authentication");
}

async function hyshareContract(): Promise<void> {
    const hash = "a".repeat(64);
    for (const hostname of ["sub.example.co.uk", "other.example"]) {
        const host = new Host([{hash, ext: ".png", tag_services_to_tags: {one: ["cat"], two: ["cat", "favorite"]}}]);
        await invoke(HyShare, host, `https://${hostname}/view/${hash}`, "post", {hash});
        check(host.imports.length === 1, "HyShare did not import the file");
        check(host.imports[0]!.tags[hostname]?.join(",") === "cat,favorite", "HyShare lost its hostname or tags");
    }
}

async function pixivContract(): Promise<void> {
    const host = new Host([{
        error: false, body: {
            illust_details: {
                id: 1,
                ugoira_meta: null,
                url_big: "https://i.pximg.net/img-original/img/1_p0.jpg",
                page_count: 2,
                title: "Example",
                author_details: {user_id: 2, user_name: "Artist", user_account: "artist"},
                tags: ["cat"]
            }
        }
    }]);
    await invoke(Pixiv, host, "https://www.pixiv.net/en/artworks/1", "post", {id: "1"});
    check(host.imports.length === 2, "Pixiv did not import every page");
    check(host.imports[1]!.request.url.endsWith("1_p1.jpg"), "Pixiv page URL was not expanded");
}

async function pawchiveContract(): Promise<void> {
    const host = new Host([{
        title: "Example", published: "2024-01-02T03:04:05Z",
        file: {name: "main.png", path: "/main.png"}, attachments: [{name: "extra.jpg", path: "/extra.jpg"}]
    }]);
    await invoke(Pawchive, host, "https://pawchive.pw/patreon/user/2/post/1", "post",
        {service: "patreon", creator: "2", id: "1"}, {importAttachments: true});
    check(host.imports.length === 2, "Pawchive attachments were not imported");
}

export async function runContract(name: string): Promise<void> {
    const contracts: Record<string, () => Promise<void>> = {
		e621: e621Contract, gelbooru: async () => {
			await gelbooruContract();
			await gelbooruCacheContract();
		}, hyshare: hyshareContract,
        pixiv: pixivContract, pawchive: pawchiveContract,
		"gelbooru-subscriptions": gelbooruSubscriptionContract,
        "gelbooru-secrets": gelbooruSecretsContract, "e621-secrets": e621SecretsContract
    };
    const contract = contracts[name];
    check(contract !== undefined, "Unknown parser contract");
    await contract();
}
