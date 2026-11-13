import {bindParser} from "../packages/sdk/runtime.js";
import {applyOptions, generator, parser} from "../packages/sdk/decorators.js";
import type {GeneratorInput, ParserMetadata} from "../packages/sdk/parser.js";
import type {DomainContext, Session} from "../packages/sdk/session.js";
import type {BoundURLClass, ParameterDefinitions, ParameterValues} from "../packages/sdk/url-class.js";
import * as v from "../packages/sdk/validation.js";

const idParameter = {
	// Decimal ID in https://www.pixiv.net/artworks/123 or https://www.pixiv.net/users/123.
	name: "ID",
	type: "string",
	pattern: "^\\d+$",
} as const;

const localeParameter = {
	// Two-letter locale in https://www.pixiv.net/en/artworks/123.
	name: "Locale",
	type: "string",
	pattern: "^[a-z]{2}$",
	optional: true,
} as const;

export const parameters = {
	post: {
		id: idParameter,
		locale: localeParameter
	},
	user: {
		id: idParameter,
		locale: localeParameter
	},
	image: {
		path: {
			name: "Original image path",
			type: "string"
		}
	},
} as const satisfies Record<string, ParameterDefinitions>;

const parserId = "pixiv";

export const urlClasses = [
	{
		id: "pixiv",
		version: 1,
		hosts: ["www.pixiv.net"],
		sortQueryParameters: true,
		requestRate: {requests: 1, seconds: 2},
		parser: parserId,
		url_patterns: [
			{
				name: "Artwork",
				generator: "post",
				patterns: [
					{
						pathname: "/artworks/:id"
					},
					// Locale-prefixed artwork, e.g. https://www.pixiv.net/en/artworks/123.
					{
						pathname: "/:locale([a-z]{2})/artworks/:id"
					},
				]
			},
			{
				name: "Creator",
				generator: "user",
				patterns: [
					{
						pathname: "/users/:id"
					},
					{
						pathname: "/users/:id/illustrations"
					},
					// Locale-prefixed creator pages, e.g. https://www.pixiv.net/en/users/123/illustrations.
					{
						pathname: "/:locale([a-z]{2})/users/:id"
					},
					{
						pathname: "/:locale([a-z]{2})/users/:id/illustrations"
					},
				]
			},
		],
		generators: [
			{
				id: "post",
				handler: "post",
				name: "Artwork",
				params: parameters.post
			},
			{
				id: "user",
				handler: "user",
				name: "Creator",
				params: parameters.user
			},
		],
	},
	{
		id: "pixiv-image",
		version: 1,
		hosts: ["i.pximg.net"],
		sortQueryParameters: true,
		requestRate: {requests: 1, seconds: 2},
		parser: parserId,
		url_patterns: [
			{
				name: "Original image",
				generator: "image",
				patterns: [
					{
						pathname: "/img-original/:path*"
					}
				]
			}
		],
		generators: [
			{
				id: "image",
				handler: "image",
				name: "Find artwork from image",
				params: parameters.image
			}
		],
	}
] as const satisfies readonly BoundURLClass<typeof parameters>[];

function resultBody(value: unknown, label: string): Record<string, unknown> {
	const result = v.object(value, label);
	const error = v.boolean(result.error, `${label}.error`);
	const message = v.nullish(result.message, v.string, `${label}.message`);

	if (error) {
		throw new Error(`${label} rejected the request: ${message ?? "unknown error"}`);
	}

	return v.object(result.body, `${label}.body`);
}

function workIds(value: unknown, label: string): string[] {
	if (value == null) {
		return [];
	}


	if (Array.isArray(value)) {
		return v.array(value, v.id, label);
	}

	return Object.keys(v.object(value, label));
}

function decodeArtist(value: unknown, label: string): string[] {
	const body = resultBody(value, label);

	return [...new Set([
		...workIds(body.illusts, "illusts"),
		...workIds(body.manga, "manga")
	])];
}

function decodeArtwork(value: unknown, label: string) {
	const details = v.object(resultBody(value, label).illust_details, `${label}.illust_details`);
	const author = v.object(details.author_details, "author_details");

	if (details.ugoira_meta !== null) {
		v.object(details.ugoira_meta, "ugoira_meta");
	}

	return {
		id: v.id(details.id, "illust_details.id"),
		animated: details.ugoira_meta !== null,
		mask: v.nullish(details.mask_reason, v.string, "mask_reason"),
		original: v.nullish(details.url_big, v.string, "url_big"),
		pageCount: v.nullish(details.page_count, v.id, "page_count"),
		tags: details.tags == null ? [] : v.array(details.tags, v.string, "tags"),
		title: v.string(details.title, "title"),
		author: {
			id: v.id(author.user_id, "author.user_id"),
			name: v.string(author.user_name, "author.user_name"),
			account: v.string(author.user_account, "author.user_account"),
		},
		uploaded: v.nullish(details.upload_timestamp, v.id, "upload_timestamp"),
		rating: v.nullish(details.x_restrict, v.numeric, "x_restrict"),
		aiType: v.nullish(details.ai_type, v.numeric, "ai_type"),
	};
}

function pageUrl(original: string, page: number): URL {
	const url = new URL(original);
	// Matches the first-page suffix in https://i.pximg.net/img-original/img/123_p0.jpg.
	const match = /_p0(\.[a-zA-Z0-9]+)$/.exec(url.pathname);

	if (match === null || match[1] === undefined) {
		throw new Error(`Unexpected Pixiv original URL: ${original}`);
	}

	url.pathname = `${url.pathname.slice(0, match.index)}_p${page}${match[1]}`;

	return url;
}

function inputId(params: {
	id: string;
	locale?: string
}): string {
	if (params.locale !== undefined) {
		// Matches the locale in https://www.pixiv.net/en/artworks/123.
		if (!/^[a-z]{2}$/.test(params.locale)) {
			throw new Error("Invalid Pixiv locale");
		}
	}

	return v.decimalId(params.id, "Pixiv ID");
}

@parser(
	{
		id: parserId,
		version: 1,
		urlClasses,
	}
)
export default class Pixiv {
	declare static readonly metadata: ParserMetadata;

	constructor(site: DomainContext) {
		applyOptions(this, site.options);
	}

	@generator("image", parameters.image)
	async image(
		input: GeneratorInput<ParameterValues<typeof parameters.image>>,
		session: Session
	): Promise<void> {
		const url = new URL(`/img-original/${v.nonempty(input.params.path, "Image path")}`, input.domain);
		// Extracts the ID from an original filename, e.g. https://i.pximg.net/img-original/img/123_p0.jpg.
		const match = /^(\d+)/.exec(v.filename(url));

		if (match === null || match[1] === undefined) {
			throw new Error("Image filename has no Pixiv artwork ID");
		}

		await session.follow(
			{
				url: new URL(`/artworks/${match[1]}`, "https://www.pixiv.net").href
			}
		);
	}

	@generator("user", parameters.user)
	async user(
		input: GeneratorInput<ParameterValues<typeof parameters.user>>,
		session: Session
	): Promise<void> {
		const id = inputId(input.params);
		const site = new URL(input.domain);
		const source = new URL(`/users/${id}/illustrations`, site);
		const ids = await v.requestJson(
			session,
			{
				url: new URL(`/ajax/user/${id}/profile/all`, site).href,
				responseType: "text",
				referer: source.href,
			},
			decodeArtist,
			"Pixiv artist API"
		);

		for (const id of ids) {
			await session.follow(
				{
					url: new URL(`/artworks/${v.decimalId(id, "Artwork ID")}`, site).href
				}
			);
		}
	}

	@generator("post", parameters.post)
	async post(
		input: GeneratorInput<ParameterValues<typeof parameters.post>>,
		session: Session
	): Promise<void> {
		const id = inputId(input.params);
		const site = new URL(input.domain);
		const source = new URL(`/artworks/${id}`, site);
		const url = new URL("/touch/ajax/illust/details", site);
		url.searchParams.set("illust_id", id);

		const found = await v.requestJson(
			session,
			{
				url: url.href,
				responseType: "text",
				referer: source.href,
			},
			decodeArtwork,
			"Pixiv artwork API"
		);

		if (found.id !== id) {
			throw new Error(`Pixiv returned artwork ${found.id} instead of ${id}`);
		}


		if (found.animated) {
			throw new Error(`Pixiv artwork ${id} is an unsupported ugoira`);
		}


		if (found.mask === "login_only") {
			throw new Error(`Pixiv artwork ${id} requires login`);
		}


		if (!found.original?.trim()) {
			throw new Error(`Pixiv artwork ${id} has no downloadable pages`);
		}

		const pageCount = Number(found.pageCount);

		if (!Number.isSafeInteger(pageCount) || pageCount < 1) {
			throw new Error(`Invalid page count for artwork ${id}`);
		}

		const tags = [...new Set([
			...found.tags,
			`creator:${found.author.name}`
		])];

		for (let page = 0; page < pageCount; ++page) {
			const file = pageUrl(found.original, page);
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
					discoveredUrls: [],
					tags: {
						pixiv: tags
					},
					metadata: {
						site: "pixiv",
						postId: id,
						page,
						pageCount,
						title: found.title,
						authorId: found.author.id,
						authorName: found.author.name,
						authorAccount: found.author.account,
						uploadedAt: found.uploaded,
						rating: found.rating,
						aiType: found.aiType,
					},
				}
			);
		}
	}
}

export const run = bindParser(Pixiv);
