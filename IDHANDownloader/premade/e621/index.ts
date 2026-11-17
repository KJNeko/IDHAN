import {bindParser} from "../packages/sdk/runtime.js";
import {applyOptions, generator, option, parser} from "../packages/sdk/decorators.js";
import type {GeneratorInput, ParserMetadata} from "../packages/sdk/parser.js";
import type {DomainContext, Session} from "../packages/sdk/session.js";
import type {BoundURLClass, ParameterDefinitions, ParameterValues} from "../packages/sdk/url-class.js";
import * as v from "../packages/sdk/validation.js";

const PAGE_SIZE = 200;

export const parameters = {
	search: {
		tags: {
			name: "Tags",
			type: "string",
			optional: true,
			default: ""
		}
	},
	post: {
		// Decimal ID in https://e621.net/posts/123.
		id: {
			name: "Post ID",
			type: "string",
			pattern: "^\\d+$"
		},
	},
} as const satisfies Record<string, ParameterDefinitions>;

const parserId = "e621";

export const urlClasses = [
	{
		id: "e621",
		version: 1,
		hosts: ["e621.net"],
		sortQueryParameters: true,
		requestRate: {requests: 1, seconds: 1, shareSubdomains: true},
		parser: parserId,
		url_patterns: [
			{
				name: "Tag search",
				generator: "search",
				patterns: [
					{
						pathname: "/posts",
						query: {
							tags: ":tags"
						}
					}
				]
			},
			{
				name: "All posts",
				generator: "search",
				patterns: [
					{
						pathname: "/posts"
					}
				]
			},
			{
				name: "Post",
				generator: "post",
				patterns: [
					{
						pathname: "/posts/:id"
					}
				],
				// The search a post was opened from, e.g. q=wolf in https://e621.net/posts/1?q=wolf.
				removeQuery: ["q"]
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

function decodeGallery(value: unknown, label: string): string[] {
	const body = v.object(value, label);

	return v.array(
		body.posts,
		(entry, path) => v.id(v.object(entry, path).id, `${path}.id`),
		`${label}.posts`
	);
}

function decodePost(value: unknown, label: string) {
	const post = v.object(v.object(value, label).post, `${label}.post`);
	const file = v.object(post.file, `${label}.post.file`);
	const relationships = v.object(post.relationships, `${label}.post.relationships`);
	const score = post.score == null ? null : v.object(post.score, `${label}.post.score`);

	return {
		id: v.id(post.id, `${label}.post.id`),
		file: {
			url: v.nullish(file.url, v.string, "file.url"),
			md5: v.nullish(file.md5, v.string, "file.md5"),
			ext: v.nullish(file.ext, v.string, "file.ext"),
			width: v.nullish(file.width, v.number, "file.width"),
			height: v.nullish(file.height, v.number, "file.height"),
			size: v.nullish(file.size, v.number, "file.size"),
		},
		tags: v.stringMap(post.tags, `${label}.post.tags`),
		parent: relationships.parent_id === null ? null : v.id(relationships.parent_id, "parent_id"),
		children: v.array(relationships.children, v.id, "children"),
		sources: post.sources == null ? [] : v.array(post.sources, v.string, "sources"),
		rating: v.nullish(post.rating, v.string, "rating"),
		created: v.nullish(post.created_at, v.string, "created_at"),
		updated: v.nullish(post.updated_at, v.string, "updated_at"),
		uploaderId: v.nullish(post.uploader_id, v.id, "uploader_id"),
		uploaderName: v.nullish(post.uploader_name, v.string, "uploader_name"),
		score: score === null ? null : v.number(score.total, "score.total"),
		favorites: v.nullish(post.fav_count, v.number, "fav_count"),
	};
}

type Post = ReturnType<typeof decodePost>;

function postUrl(site: URL, id: string): URL {
	return new URL(`/posts/${id}`, site);
}

function tags(post: Post): string[] {
	const result: string[] = [];

	for (const [group, names] of Object.entries(post.tags)) {
		for (const name of names) {
			result.push(group === "general" ? name : `${group}:${name}`);
		}
	}

	return [...new Set(result)];
}

@parser(
	{
		id: parserId,
		version: 1,
		urlClasses,
	}
)
export default class E621 {
	declare static readonly metadata: ParserMetadata;

	@option(
		{
			type: "boolean",
			default: false,
			description: "Follow the post's parent and children."
		}
	)
	accessor followSiblings = false;

	constructor(private readonly site: DomainContext) {
		applyOptions(this, site.options);
	}

	async configure(): Promise<void> {
		const username = this.site.secrets.username;
		const apiKey = this.site.secrets.apiKey;

		if (username == null && apiKey == null) {
			return;
		}

		v.requiredSecrets(this.site.secrets, ["username", "apiKey"]);
		await this.site.setBasicAuth(
			v.nonempty(username, "Secrets.username"),
			v.nonempty(apiKey, "Secrets.apiKey")
		);
	}

	@generator("search", parameters.search)
	async search(
		input: GeneratorInput<ParameterValues<typeof parameters.search>>,
		session: Session
	): Promise<void> {
		const site = new URL(input.domain);
		const url = new URL("/posts.json", site);
		url.search = new URLSearchParams(
			{
				tags: input.params.tags ?? "",
				limit: String(PAGE_SIZE)
			}
		).toString();

		const source = new URL("/posts", site);
		source.searchParams.set("tags", input.params.tags ?? "");

		let previousLastId: string | null = null;

		while (true) {
			const ids = await v.requestJson(
				session,
				{
					url: url.href,
					responseType: "text",
					referer: input.url ?? source.href,
				},
				decodeGallery,
				"e621 gallery API"
			);

			for (const id of ids) {
				await session.follow(
					{
						url: postUrl(site, id).href
					}
				);
			}

			if (ids.length < PAGE_SIZE) {
				return;
			}

			const last = ids.at(-1);

			if (last === undefined) {
				return;
			}


			if (last === previousLastId) {
				throw new Error(`e621 pagination repeated post ${last}`);
			}

			previousLastId = last;
			url.searchParams.set("page", `b${last}`);
		}
	}

	@generator("post", parameters.post)
	async post(
		input: GeneratorInput<ParameterValues<typeof parameters.post>>,
		session: Session
	): Promise<void> {
		const id = v.decimalId(input.params.id, "Post ID");
		const site = new URL(input.domain);
		const source = postUrl(site, id);
		const found = await v.requestJson(
			session,
			{
				url: new URL(`/posts/${id}.json`, site).href,
				responseType: "text",
				referer: source.href,
			},
			decodePost,
			"e621 post API"
		);

		if (found.id !== id) {
			throw new Error(`e621 returned post ${found.id} instead of ${id}`);
		}


		if (!found.file.url) {
			throw new Error(`e621 post ${id} has no downloadable file`);
		}

		const file = new URL(found.file.url, site);

		if (this.followSiblings) {
			const siblings = new Set(found.children);

			if (found.parent !== null) {
				siblings.add(found.parent);
			}

			siblings.delete(id);

			for (const sibling of siblings) {
				await session.follow(
					{
						url: postUrl(site, sibling).href
					}
				);
			}
		}

		session.queueImport(
			{
				request: {
					url: file.href,
					referer: source.href
				},
				filename: v.filename(file),
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
				discoveredUrls: [...new Set(found.sources.filter(url => URL.canParse(url)))],
				tags: {
					e621: tags(found)
				},
				postedAt: v.postedAt(found.created, "created_at"),
				metadata: {
					site: "e621",
					postId: id,
					md5: found.file.md5,
					extension: found.file.ext,
					width: found.file.width,
					height: found.file.height,
					expectedSize: found.file.size,
					rating: found.rating,
					createdAt: found.created,
					updatedAt: found.updated,
					uploaderId: found.uploaderId,
					uploaderName: found.uploaderName,
					score: found.score,
					favorites: found.favorites,
				},
			}
		);
	}
}

export const run = bindParser(E621);
