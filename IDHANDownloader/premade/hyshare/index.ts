import {bindParser} from "../packages/sdk/runtime.js";
import {applyOptions, discovery, generator, parser} from "../packages/sdk/decorators.js";
import type {DiscoveryInput, DiscoveryResult, GeneratorInput, ParserMetadata} from "../packages/sdk/parser.js";
import type {DomainContext, Session} from "../packages/sdk/session.js";
import type {BoundURLClass, ParameterDefinitions, ParameterValues} from "../packages/sdk/url-class.js";
import * as v from "../packages/sdk/validation.js";

export const parameters = {
	gallery: {
		id: {
			name: "Gallery ID",
			type: "string"
		}
	},
	post: {
		// SHA-256 in https://share.example/view/e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855.
		hash: {
			name: "File hash",
			type: "string",
			pattern: "^[0-9a-f]{64}$"
		},
	},
} as const satisfies Record<string, ParameterDefinitions>;

const parserId = "hyshare";

export const urlClasses = [
	{
		id: "hyshare",
		version: 1,
		hosts: [],
		sortQueryParameters: true,
		requestRate: false,
		parser: parserId,
		url_patterns: [
			{
				name: "Gallery",
				generator: "gallery",
				patterns: [
					{
						pathname: "/gallery/:id"
					}
				]
			},
			{
				name: "Post",
				generator: "post",
				patterns: [
					{
						pathname: "/view/:hash"
					}
				]
			},
		],
		generators: [
			{
				id: "gallery",
				handler: "gallery",
				name: "Gallery",
				params: parameters.gallery
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

function hash(value: unknown, label: string): string {
	const result = v.string(value, label);

	// Matches a lowercase SHA-256, e.g. e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855.
	if (!/^[0-9a-f]{64}$/.test(result)) {
		throw new Error(`${label} must be a lowercase SHA-256`);
	}

	return result;
}

function decodeGallery(value: unknown, label: string): string[] {
	return v.array(v.object(value, label).hashes, hash, `${label}.hashes`);
}

function decodePost(value: unknown, label: string) {
	const post = v.object(value, label);

	return {
		hash: hash(post.hash, `${label}.hash`),
		ext: v.string(post.ext, `${label}.ext`),
		knownUrls: post.detailed_known_urls == null ? [] : v.array(
			post.detailed_known_urls,
			(entry, path) => v.string(v.object(entry, path).normalised_url, `${path}.normalised_url`),
			"known_urls"
		),
		tags: post.tag_services_to_tags == null ? {} : v.stringMap(post.tag_services_to_tags, "tag_services_to_tags"),
		title: v.nullish(post.title, v.string, "title"),
		mime: v.nullish(post.mime, v.string, "mime"),
		size: v.nullish(post.size, v.number, "size"),
		width: v.nullish(post.width, v.number, "width"),
		height: v.nullish(post.height, v.number, "height"),
	};
}

@parser(
	{
		id: parserId,
		version: 1,
		urlClasses,
	}
)
export default class HyShare {
	declare static readonly metadata: ParserMetadata;

	constructor(site: DomainContext) {
		applyOptions(this, site.options);
	}

	@generator("gallery", parameters.gallery)
	async gallery(
		input: GeneratorInput<ParameterValues<typeof parameters.gallery>>,
		session: Session
	): Promise<void> {
		const id = v.nonempty(input.params.id, "Gallery ID");
		const response = await session.request(
			{
				url: new URL(`/gallery/${encodeURIComponent(id)}/data.json`, input.domain).href,
				responseType: "text",
			}
		);
		const hashes = v.parseJson(response, decodeGallery, "HyShare gallery API");

		for (const hash of hashes) {
			await session.follow(
				{
					url: new URL(`/view/${hash}`, response.url).href
				}
			);
		}
	}

	@discovery("hyshare")
	async discover(input: DiscoveryInput, session: Session): Promise<DiscoveryResult | null> {
		const candidate = new URL(input.url);

		if (candidate.protocol !== "http:" && candidate.protocol !== "https:") {
			return null;
		}

		const response = await session.request(
			{
				url: new URL("/", candidate).href,
				responseType: "text",
				allowHttpErrors: true,
			}
		);

		// Footer contains the project github link
		const footerLinkPattern = /href\s*=\s*["']https:\/\/github\.com\/floogulinc\/hyshare\/?["']/i;

		if (!footerLinkPattern.test(response.body)) {
			return null;
		}

		return {origin: candidate.origin};
	}

	@generator("post", parameters.post)
	async post(
		input: GeneratorInput<ParameterValues<typeof parameters.post>>,
		session: Session
	): Promise<void> {
		const expected = hash(input.params.hash, "File hash");
		const response = await session.request(
			{
				url: new URL(`/view/${expected}/data.json`, input.domain).href,
				responseType: "text",
			}
		);
		const found = v.parseJson(response, decodePost, "HyShare post API");

		if (found.hash !== expected) {
			throw new Error(`HyShare returned hash ${found.hash} instead of ${expected}`);
		}

		const source = new URL(`/view/${found.hash}`, response.url);
		const file = new URL(`/file/${found.hash}`, response.url);
		session.queueImport(
			{
				request: {
					url: file.href,
					referer: source.href
				},
				filename: `${found.hash}${found.ext}`,
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
				discoveredUrls: [...new Set(found.knownUrls.filter(url => URL.canParse(url)))],
				tags: {
					[source.hostname]: [...new Set(Object.values(found.tags).flat())]
				},
				metadata: {
					site: "hyshare",
					sha256: found.hash,
					title: found.title,
					mime: found.mime,
					size: found.size,
					width: found.width,
					height: found.height,
				},
			}
		);
	}
}

export const run = bindParser(HyShare);
