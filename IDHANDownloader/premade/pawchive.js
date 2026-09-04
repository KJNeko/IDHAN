import {ApiClient, PostsApi} from "pawchive-api";
import {parseResponse, z} from "validation";

const site = "https://pawchive.pw";
const cdn = "https://file.pawchive.pw"
const pageSize = 50;
const fileSchema = z.object({
    name: z.string().min(1),
    path: z.string().min(1),
});
const creatorResponseSchema = z.array(z.object({
    id: z.string(),
}));
const postResponseSchema = z.object({
    title: z.string().nullish(),
    published: z.date().nullish(),
    edited: z.date().nullish(),
    added: z.date().nullish(),
    file: fileSchema.nullish(),
    attachments: z.array(fileSchema).nullish().transform((attachments) => attachments ?? []),
});

export const manifest = {
    id: "pw.pawchive",
    version: 1,
    flags: [
        {
            name: "pawchive.importAttachments",
            description: "Import a post's attachments alongside its main file.",
            default: true,
        },
    ],
};

function api() {
    const client = new ApiClient(`${site}/api/v1`);
    delete client.defaultHeaders["User-Agent"];

    return new PostsApi(client);
}

// Matches the path in a post URL such as https://pawchive.pw/patreon/user/123/post/456.
const postPath = /^\/([^/]+)\/user\/([^/]+)\/post\/([^/]+)$/;
// Matches the path in a creator URL such as https://pawchive.pw/patreon/user/123.
const creatorPath = /^\/([^/]+)\/user\/([^/]+)$/;
// Removes trailing slashes so /patreon/user/123/// becomes /patreon/user/123.
const trailingSlashes = /\/+$/;

function target(value) {
    const path = new URL(value).pathname.replace(trailingSlashes, "");
    const post = postPath.exec(path);

    if (post !== null) {
        return {kind: "post", service: post[1], creator: post[2], post: post[3]};
    }

    const creator = creatorPath.exec(path);

    if (creator !== null) {
        return {kind: "creator", service: creator[1], creator: creator[2]};
    }

    return null;
}

function postUrl(service, creator, post) {
    return `${site}/${service}/user/${creator}/post/${post}`;
}

function fileUrl(file) {
    return `${cdn}/data${file.path}?f=${encodeURIComponent(file.name)}`;
}

// Preserve useful errors from the generated client's plain-object rejections.
async function call(request, schema, description) {
    let response;

    try {
        response = await request;
    } catch (failure) {
        const status = failure?.status ?? failure?.response?.status;

        throw new Error(`pawchive ${description} failed${status === undefined ? "" : ` with ${status}`}`);
    }

    return parseResponse(schema, response, `pawchive ${description}`);
}

function importFile(idhan, file, source, metadata) {
    if (!file) {
        return;
    }

    const url = fileUrl(file);

    idhan.import({
        request: {url, referer: source},
        filename: file.name,
        urls: [
            {url: source, type: "post"},
            {url, type: "file"},
        ],
        tags: [`service:${metadata.service}`, `creator:${metadata.creatorId}`],
        metadata,
    });
}

async function creator(input, idhan, route) {
    const posts = api();
    let offset = 0;

    while (true) {
        const page = await call(
            posts.serviceUserCreatorIdGet(route.service, route.creator, {o: offset}),
            creatorResponseSchema,
            `creator ${route.service}/${route.creator}`,
        );

        for (const post of page) {
            idhan.follow({url: postUrl(route.service, route.creator, post.id)});
        }

        if (page.length < pageSize) {
            return;
        }

        offset += pageSize;
    }
}

async function post(input, idhan, route) {
    const source = postUrl(route.service, route.creator, route.post);
    const found = await call(
        api().serviceUserCreatorIdPostPostIdGet(route.service, route.creator, route.post),
        postResponseSchema,
        `post ${route.service}/${route.creator}/${route.post}`,
    );

    const metadata = {
        site: "pawchive",
        service: route.service,
        creatorId: route.creator,
        postId: route.post,
        title: found.title,
        published: found.published?.toISOString(),
        edited: found.edited?.toISOString(),
        added: found.added?.toISOString(),
    };

    importFile(idhan, found.file, source, metadata);

    if (input.flags["pawchive.importAttachments"]) {
        for (const [index, attachment] of found.attachments.entries()) {
            importFile(idhan, attachment, source, {...metadata, attachment: index});
        }
    }
}

export async function route(input, idhan) {
    const found = target(input.url);

    if (found === null) {
        throw new Error(`Unsupported pawchive URL: ${input.url}`);
    }

    if (found.kind === "creator") {
        return creator(input, idhan, found);
    }

    return post(input, idhan, found);
}
