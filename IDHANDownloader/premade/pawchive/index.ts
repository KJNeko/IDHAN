import {bindParser} from "../packages/sdk/runtime.js";
import {applyOptions, generator, option, parser} from "../packages/sdk/decorators.js";
import type {GeneratorInput, ParserMetadata} from "../packages/sdk/parser.js";
import type {DomainContext, MetadataValue, Session} from "../packages/sdk/session.js";
import type {BoundURLClass, ParameterDefinitions, ParameterValues} from "../packages/sdk/url-class.js";
import * as v from "../packages/sdk/validation.js";

const PAGE_SIZE = 50;

const service = {
	name: "Service",
	type: "string"
} as const;

const creatorId = {
	name: "Creator ID",
	type: "string"
} as const;

export const parameters = {
	creator: {
		service,
		creator: creatorId
	},
	post: {
		service,
		creator: creatorId,
		id: {
			name: "Post ID",
			type: "string"
		}
	},
} as const satisfies Record<string, ParameterDefinitions>;

const parserId = "pawchive";

export const urlClasses = [
	{
		id: "pawchive",
		version: 1,
		hosts: [
			"pawchive.pw",
			"kemono.cr",
			"kemono.su"
		],
		sortQueryParameters: true,
		requestRate: {requests: 1, seconds: 1},
		parser: parserId,
		url_patterns: [
			{
				name: "Post",
				generator: "post",
				patterns: [
					{
						pathname: "/:service/user/:creator/post/:id"
					}
				]
			},
			{
				name: "Creator",
				generator: "creator",
				patterns: [
					{
						pathname: "/:service/user/:creator"
					}
				]
			},
		],
		generators: [
			{
				id: "creator",
				handler: "creator",
				name: "Creator",
				params: parameters.creator
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

function decodeCreators(value: unknown, label: string): string[] {
	return v.array(
		value,
		(entry, path) => v.string(v.object(entry, path).id, `${path}.id`),
		label
	);
}

function decodeFile(value: unknown, label: string) {
	const file = v.object(value, label);

	return {
		name: v.nonempty(file.name, `${label}.name`),
		path: v.nonempty(file.path, `${label}.path`)
	};
}

type File = ReturnType<typeof decodeFile>;

function decodePost(value: unknown, label: string) {
	const post = v.object(value, label);

	return {
		title: v.nullish(post.title, v.string, "title"),
		published: v.nullish(post.published, v.date, "published"),
		edited: v.nullish(post.edited, v.date, "edited"),
		added: v.nullish(post.added, v.date, "added"),
		file: v.nullish(post.file, decodeFile, "file"),
		attachments: post.attachments == null ? [] : v.array(post.attachments, decodeFile, "attachments"),
	};
}

function creatorPath(params: ParameterValues<typeof parameters.creator>): string {
	const service = encodeURIComponent(v.nonempty(params.service, "Service"));
	const creator = encodeURIComponent(v.nonempty(params.creator, "Creator ID"));

	return `/${service}/user/${creator}`;
}

function postUrl(site: URL, params: ParameterValues<typeof parameters.post>): URL {
	return new URL(
		`${creatorPath(params)}/post/${encodeURIComponent(v.nonempty(params.id, "Post ID"))}`,
		site
	);
}

function mediaOrigin(site: URL, configured: string): URL {
	const origin = configured === ""
		? (
			site.hostname === "pawchive.pw"
				? "https://file.pawchive.pw"
				: site.origin
		)
		: configured;
	const url = new URL(origin);

	if (url.protocol !== "https:" && url.protocol !== "http:") {
		throw new Error("Media origin must use HTTP(S)");
	}

	return url;
}

function importFile(
	session: Session,
	file: File,
	source: URL,
	metadata: Record<string, MetadataValue>,
	configuredOrigin: string,
): void {
	const url = new URL(`/data${file.path}`, mediaOrigin(source, configuredOrigin));
	url.searchParams.set("f", file.name);
	session.queueImport(
		{
			request: {
				url: url.href,
				referer: source.href
			},
			postedAt: v.postedAt(metadata.published, "published"),
			filename: file.name,
			urls: [
				{
					url: source.href,
					type: "post"
				},
				{
					url: url.href,
					type: "file"
				}
			],
			discoveredUrls: [],
			tags: {
				pawchive: [
					`service:${metadata.service}`,
					`creator:${metadata.creatorId}`
				]
			},
			metadata,
		}
	);
}

@parser(
	{
		id: parserId,
		version: 1,
		urlClasses,
	}
)
export default class Pawchive {
	declare static readonly metadata: ParserMetadata;

	@option(
		{
			type: "boolean",
			default: true,
			description: "Import attachments alongside the main file."
		}
	)
	accessor importAttachments = true;

	@option(
		{
			type: "string",
			default: "",
			description: "Optional media origin for this host; blank uses the site's default media location.",
		}
	)
	accessor mediaOrigin = "";

	constructor(site: DomainContext) {
		applyOptions(this, site.options);
	}

	@generator("creator", parameters.creator)
	async creator(
		input: GeneratorInput<ParameterValues<typeof parameters.creator>>,
		session: Session
	): Promise<void> {
		const site = new URL(input.domain);
		const path = creatorPath(input.params);
		for (let offset = 0; ; offset += PAGE_SIZE) {
			const url = new URL(`/api/v1${path}`, site);
			url.searchParams.set("o", String(offset));

			const ids = await v.requestJson(
				session,
				{
					url: url.href,
					responseType: "text",
					referer: new URL(path, site).href,
				},
				decodeCreators,
				"Pawchive creator API"
			);

			for (const id of ids) {
				await session.follow(
					{
						url: postUrl(
							site,
							{
								...input.params,
								id
							}
						).href
					}
				);
			}

			if (ids.length < PAGE_SIZE) {
				return;
			}
		}
	}

	@generator("post", parameters.post)
	async post(
		input: GeneratorInput<ParameterValues<typeof parameters.post>>,
		session: Session
	): Promise<void> {
		const source = postUrl(new URL(input.domain), input.params);
		const found = await v.requestJson(
			session,
			{
				url: new URL(`/api/v1${source.pathname}`, source).href,
				responseType: "text",
				referer: source.href,
			},
			decodePost,
			"Pawchive post API"
		);
		const metadata = {
			site: "pawchive",
			service: input.params.service,
			creatorId: input.params.creator,
			postId: input.params.id,
			title: found.title,
			published: found.published,
			edited: found.edited,
			added: found.added,
		};

		if (found.file !== null) {
			importFile(
				session,
				found.file,
				source,
				metadata,
				this.mediaOrigin
			);
		}

		if (this.importAttachments) {
			for (const [attachment, file] of found.attachments.entries()) {
				importFile(
					session,
					file,
					source,
					{
						...metadata,
						attachment
					},
					this.mediaOrigin
				);
			}
		}
	}
}

export const run = bindParser(Pawchive);
