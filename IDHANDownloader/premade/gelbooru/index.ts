import {decode} from "he";
import {bindParser} from "../packages/sdk/runtime.js";
import * as validation from "../packages/sdk/validation.js";
import {applyOptions, generator, option, parser} from "../packages/sdk/decorators.js";
import type {GeneratorInput, ParserMetadata} from "../packages/sdk/parser.js";
import type {DomainContext, Session} from "../packages/sdk/session.js";
import type {BoundURLClass, ParameterDefinitions, ParameterValues} from "../packages/sdk/url-class.js";

const POST_PAGE_SIZE = 100;
const TAG_BATCH_SIZE = 100;

// Matches decimal IDs, e.g. id=123 in https://gelbooru.com/index.php?page=post&s=view&id=123.
const decimalId = /^\d+$/;

export const parameters = {
	search: {
		tags: {
			name: "Tags",
			type: "string",
			optional: true,
			default: ""
		},

	},
	post: {
		// Decimal ID in https://gelbooru.com/index.php?page=post&s=view&id=123.
		id: {
			name: "Post ID",
			type: "string",
			pattern: "^\\d+$"
		},
	},
} as const satisfies Record<string, ParameterDefinitions>;

type SearchParams = ParameterValues<typeof parameters.search>;

type PostParams = ParameterValues<typeof parameters.post>;

interface Credentials {
	apiKey: string;
	userId: string;
}

interface Post {
	created_at?: string | null;
	id: string | number;
	file_url: string;
	source?: string | null;
	tags?: string;
}

interface Tag {
	name: string;
	type: string | number;
}

// API responses can be bare arrays, wrapped singleton/arrays, or null/empty envelopes.
type APIRecords<T> = T[] | Partial<Record<"post" | "tag", T | T[] | null>> | null;

function records<T>(body: APIRecords<T>, member: "post" | "tag"): T[] {
	if (Array.isArray(body)) {
		return body;
	}

	const entries = body?.[member];

	if (entries == null) {
		return [];
	}

	return Array.isArray(entries) ? entries : [entries];
}

const namespaces: Readonly<Partial<Record<number, string>>> = {
	1: "creator",
	2: "series",
	3: "copyright",
	4: "character",
	5: "meta",
};

const parserId = "gelbooru";

// Whitespace between the sources in a post's source field, e.g. the gap in "https://x.com/a/status/1 https://www.pixiv.net/en/artworks/2".
const SOURCE_SEPARATOR = /\s+/;

export const urlClasses = [
	{
		id: "gelbooru",
		version: 1,
		hosts: [
			{
				host: "gelbooru.com",
				lane: {concurrency: 1},
			},
			{
				host: "img4.gelbooru.com",
				requestRate: {requests: 2, seconds: 1},
				lane: {concurrency: 2},
				retry: [{code: 404, count: 3, wait: 10}]
			},
			{host: "img3.gelbooru.com", pointsTo: "img4.gelbooru.com"},
			{host: "img2.gelbooru.com", pointsTo: "img4.gelbooru.com"},
			{host: "img1.gelbooru.com", pointsTo: "img4.gelbooru.com"}
		],
		sortQueryParameters: true,
		requestRate: {requests: 2, seconds: 1},
		parser: parserId,
		url_patterns: [
			{
				name: "Tag search",
				generator: "search",
				patterns: [
					{
						pathname: "/index.php",
						query: {
							page: "post",
							s: "list",
							tags: ":tags"
						}
					}
				],
			},
			{
				name: "All posts",
				generator: "search",
				patterns: [
					{
						pathname: "/index.php",
						query: {
							page: "post",
							s: "list"
						},
					}
				],
			},
			{
				name: "Post",
				generator: "post",
				patterns: [
					{
						pathname: "/index.php",
						query: {
							page: "post",
							s: "view",
							id: ":id"
						}
					}
				],
				// The search a post was opened from, e.g. tags=wolf in https://gelbooru.com/index.php?page=post&s=view&id=1&tags=wolf.
				removeQuery: ["tags"],
			},
		],
		generators: [
			{
				id: "search",
				handler: "search",
				name: "Tag search",
				params: parameters.search
			},
			{
				id: "post",
				handler: "post",
				name: "Post",
				params: parameters.post
			},
		],
	}
] as const satisfies readonly BoundURLClass<typeof parameters>[];

function credentials(context: Session): Credentials {
	validation.requiredSecrets(context.secrets, ["apiKey", "userID"]);
	return {
		apiKey: validation.nonempty(context.secrets.apiKey, "Secrets.apiKey"),
		userId: validation.nonempty(context.secrets.userID, "Secrets.userID")
	};
}

function apiUrl(
	site: URL,
	auth: Credentials,
	section: "post" | "tag",
	params: Readonly<Record<string, string>>,
): URL {
	const url = new URL("/index.php", site);
	url.search = new URLSearchParams(
		{
			page: "dapi",
			s: section,
			q: "index",
			json: "1",
			...params,
			api_key: auth.apiKey,
			user_id: auth.userId,
		}
	).toString();

	return url;
}

function postUrl(site: URL, id: string): URL {
	const url = new URL("/index.php", site);
	url.search = new URLSearchParams(
		{
			page: "post",
			s: "view",
			id
		}
	).toString();

	return url;
}

async function requestApi<Output>(
	context: Session,
	url: URL,
	description: string,
): Promise<Output> {
	const response = await context.request(
		{
			url: url.href,
			responseType: "text",
		}
	);

	if (response.status < 200 || response.status >= 300) {
		throw new Error(`${description} returned HTTP ${response.status}`);
	}


	if (response.body.trim() === "") {
		throw new Error(`${description} returned an empty body; check the API key and user ID.`);
	}

	let body: unknown;

	try {
		body = JSON.parse(response.body);
	} catch {
		throw new Error(`${description} returned invalid JSON`);
	}

	const member = url.searchParams.get("s") === "tag" ? "tag" : "post";
	const envelope = body === null || Array.isArray(body) ? null : validation.object(body, description);

	if (envelope?.success === false || envelope?.error != null) {
		throw new Error(`${description} reported an API error`);
	}

	const entries = Array.isArray(body) ? body : envelope?.[member];

	for (const entry of entries == null ? [] : Array.isArray(entries) ? entries : [entries]) {
		const record = validation.object(entry, description);

		if (member === "tag") {
			record.name = decode(validation.nonempty(record.name, "tag.name"));
			validation.numeric(record.type, "tag.type");
		} else {
			validation.decimalId(record.id, "post.id");
			record.file_url = decode(validation.nonempty(record.file_url, "post.file_url"));
			const source = validation.nullish(record.source, validation.string, "post.source");

			if (source != null) record.source = decode(source);

			if (record.tags !== undefined) record.tags = decode(validation.string(record.tags, "post.tags"));
		}
	}

	return body as Output;
}

function postTimestamp(post: Post): number | undefined {
	if (post.created_at == null) return undefined;

	const raw = validation.string(post.created_at, "post.created_at");
	// Matches Gelbooru's source timestamp, for example "Sat Dec 14 04:00:44 -0600 2024".
	const rubyDate = /^[A-Za-z]{3} ([A-Za-z]{3}) (\d{1,2}) (\d{2}:\d{2}:\d{2}) ([+-]\d{2})(\d{2}) (\d{4})$/;
	const match = rubyDate.exec(raw);

	if (match === null) return validation.postedAt(raw, "post.created_at");

	const months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
	const month = months.indexOf(match[1]!);
	if (month === -1) throw new Error("Invalid month in post.created_at");

	return validation.postedAt(`${match[6]}-${String(month + 1).padStart(2, "0")}-${match[2]!.padStart(2, "0")}T${match[3]}${match[4]}:${match[5]}`, "post.created_at");
}

function tagNames(post: Post): string[] {
	// Matches whitespace separating tag names, e.g. "cat blue_eyes".
	return (post.tags ?? "").split(/\s+/).filter(Boolean);
}

function postSearchTags(tags: string, before: bigint | undefined): string {
	// Matches whitespace separating search terms, e.g. "cat sort:score".
	const filters = tags.split(/\s+/).filter(term => term !== "" && !term.toLowerCase().startsWith("sort:"));
	filters.push("sort:id:desc");

	if (before !== undefined) {
		filters.push(`id:<${before}`);
	}

	return filters.join(" ");
}

function olderPostCursor(posts: readonly Post[], before: bigint | undefined): bigint | undefined {
	let cursor = before;

	for (const post of posts) {
		const id = BigInt(validation.decimalId(post.id, "post.id"));

		if (cursor !== undefined && id >= cursor) {
			throw new Error("Gelbooru search did not return strictly decreasing IDs; cannot safely continue pagination");
		}

		cursor = id;
	}

	return cursor;
}

async function tagTypes(
	context: Session,
	site: URL,
	auth: Credentials,
	names: readonly string[],
): Promise<Map<string, number>> {
	const types = new Map<string, number>();
	const missing: string[] = [];

	for (const name of names) {
		const cached = context.kv.get<number>("tag-type:" + name);

		if (cached === undefined) {
			missing.push(name);
			continue;
		}

		types.set(name, cached);
	}

	for (let index = 0; index < missing.length; index += TAG_BATCH_SIZE) {
		const url = apiUrl(
			site,
			auth,
			"tag",
			{
				names: missing.slice(index, index + TAG_BATCH_SIZE).join(" "),
				limit: String(TAG_BATCH_SIZE),
			}
		);
		const body = await requestApi<APIRecords<Tag>>(context, url, "Gelbooru tag API");

		for (const tag of records(body, "tag")) {
			types.set(tag.name, Number(tag.type));
			context.kv.set("tag-type:" + tag.name, Number(tag.type));
		}
	}

	return types;
}

function namespacedTags(post: Post, types: ReadonlyMap<string, number>): string[] {
	return tagNames(post).map(
		(name) => {
			const type = types.get(name);
			const namespace = type === undefined ? undefined : namespaces[type];

			return namespace === undefined ? name : `${namespace}:${name}`;
		}
	);
}

function creatorTags(post: Post, types: ReadonlyMap<string, number>): string[] {
	return tagNames(post).filter(name => types.get(name) === 1).map(name => `creator:${name}`);
}

async function importPosts(
	context: Session,
	site: URL,
	auth: Credentials,
	posts: readonly Post[],
	followSource: boolean,
): Promise<void> {
	const names = [...new Set(posts.flatMap(tagNames))];
	const types = await tagTypes(
		context,
		site,
		auth,
		names
	);

	for (const post of posts) {
		const id = String(post.id);
		const source = postUrl(site, id);
		const file = new URL(post.file_url, site);
		const discoveredUrls = [...new Set((post.source ?? "").split(SOURCE_SEPARATOR).filter(url => URL.canParse(url)))];

		if (followSource) {
			for (const url of discoveredUrls) {
				await context.follow(
					{
						url,
						tags: {[site.hostname]: creatorTags(post, types)}
					}
				);
			}
		}

		const options =
			{
				request: {
					url: file.href,
					referer: source.href
				},
				postedAt: postTimestamp(post),
				filename: file.pathname.slice(file.pathname.lastIndexOf("/") + 1),
				urls: [
					{
						url: source.href,
						type: "post"
					},
					{
						url: file.href,
						type: "file"
					}
				],
				discoveredUrls,
				tags: {
					[site.hostname]: namespacedTags(post, types)
				},
				metadata: {
					site: "gelbooru",
					host: site.hostname,
					postId: id
				},
			} as const;

		context.queueImport(options);
	}
}

@parser(
	{
		id: parserId,
		version: 1,
		cookies: [
			{
				name: "fringeBenefits",
				value: "yup"
			}
		] as const,
		urlClasses,
	}
)
export default class Gelbooru {
	declare static readonly metadata: ParserMetadata;

	@option(
		{
			type: "boolean",
			default: false,
			description: "Submit a post's source URL back to the parser pipeline.",
		}
	)
	accessor followSource = false;

	@option(
		{
			type: "number",
			default: 0,
			minimum: 0,
			integer: true,
			description: "Maximum search pages to process; zero means unlimited.",
		}
	)
	accessor pageLimit = 0;

	@option(
		{
			type: "string[]",
			default: [],
			description: "Tags excluded by the site's blacklist cookie.",
		}
	)
	accessor tagBlacklist: readonly string[] = [];

	constructor(private readonly site: DomainContext) {
		applyOptions(this, site.options);
	}

	async configure(): Promise<void> {
		const blacklist = this.tagBlacklist;

		if (blacklist.length === 0) {
			return;
		}

		await this.site.setCookie("tag_blacklist", encodeURIComponent(blacklist.join("%20")));
	}

	@generator("search", parameters.search)
	async search(
		input: GeneratorInput<SearchParams>,
		session: Session
	): Promise<void> {
		const tags = input.params.tags ?? parameters.search.tags.default;
		const site = new URL(input.domain);
		const auth = credentials(session);
		let before: bigint | undefined;

		for (let page = 0; ; ++page) {
			const url = apiUrl(
				site,
				auth,
				"post",
				{
					limit: String(POST_PAGE_SIZE),
					pid: "0",
					tags: postSearchTags(tags, before),
				}
			);
			const body = await requestApi<APIRecords<Post>>(session, url, "Gelbooru post API");
			const posts = records(body, "post");
			before = olderPostCursor(posts, before);
			await importPosts(
				session,
				site,
				auth,
				posts,
				this.followSource
			);

			if (posts.length < POST_PAGE_SIZE) {
				return;
			}


			if (this.pageLimit !== 0 && page + 1 >= this.pageLimit) {
				return;
			}
		}
	}

	@generator("post", parameters.post)
	async post(
		input: GeneratorInput<PostParams>,
		session: Session
	): Promise<void> {
		const {id} = input.params;

		if (!decimalId.test(id)) {
			throw new Error("Post ID must be a decimal integer");
		}

		const site = new URL(input.domain);
		const auth = credentials(session);
		const url = apiUrl(
			site,
			auth,
			"post",
			{
				id
			}
		);
		const body = await requestApi<APIRecords<Post>>(session, url, "Gelbooru post API");
		const posts = records(body, "post");

		if (posts.length === 0) {
			throw new Error(`Gelbooru has no post ${id}`);
		}


		if (posts.some((entry) => String(entry.id) !== id)) {
			throw new Error(`Gelbooru returned an unexpected post for ${id}`);
		}

		await importPosts(
			session,
			site,
			auth,
			posts,
			this.followSource
		);
	}
}

export const run = bindParser(Gelbooru);
