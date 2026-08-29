import {parseJsonResponse, z} from "validation";

const pageSize = 200;

export const manifest = {
    id: "net.e621",
    version: 1,
    auth: "auth",
    flags: [
        {
            name: "e621.followSiblings",
            description: "Follow a post's parent and all children belonging to that parent.",
            default: false,
        },
    ],
};

// Matches the path from a post URL such as https://e621.net/posts/12345.
const postPath = /^\/posts\/(\d+)\/?$/;
const idSchema = z.union([z.string(), z.number()]);
const galleryResponseSchema = z.object({
    posts: z.array(z.object({id: idSchema})),
});
const postResponseSchema = z.object({
    post: z.object({
        id: idSchema,
        file: z.object({
            url: z.string().nullish(),
            md5: z.string().nullish(),
            ext: z.string().nullish(),
            width: z.number().nullish(),
            height: z.number().nullish(),
            size: z.number().nullish(),
        }),
        tags: z.record(z.string(), z.array(z.string())),
        relationships: z.object({
            parent_id: idSchema.nullable(),
            children: z.array(idSchema),
        }),
        sources: z.array(z.string()).nullish().transform((sources) => sources ?? []),
        rating: z.string().nullish(),
        created_at: z.string().nullish(),
        updated_at: z.string().nullish(),
        uploader_id: idSchema.nullish(),
        uploader_name: z.string().nullish(),
        score: z.object({total: z.number()}).nullish(),
        fav_count: z.number().nullish(),
    }),
});

function postId(value) {
    const match = postPath.exec(new URL(value).pathname);

    if (match === null) {
        throw new Error(`Invalid e621 post URL: ${value}`);
    }

    return match[1];
}

function postUrl(id) {
    return `https://e621.net/posts/${id}`;
}

function postDataUrl(id) {
    return `https://e621.net/posts/${id}.json`;
}

function postTags(post) {
    const tags = [];

    for (const [group, values] of Object.entries(post.tags)) {
        for (const tag of values) {
            tags.push(group === "general" ? tag : `${group}:${tag}`);
        }
    }

    return [...new Set(tags)];
}

function sourceUrls(value) {
    return [...new Set((value ?? []).filter(Boolean))];
}

function followSiblings(idhan, post, id) {
    const relationships = post.relationships;

    if (relationships.parent_id !== null) {
        const parentId = String(relationships.parent_id);

        if (parentId !== id) {
            idhan.follow({
                url: postUrl(parentId),
            });
        }
    }

    for (const childId of relationships.children) {
        if (String(childId) === id) {
            continue;
        }

        idhan.follow({
            url: postUrl(childId),
        });
    }
}

export function auth(server, idhan) {
    const username = idhan.secret("e621.username");
    const apiKey = idhan.secret("e621.apiKey");

    if (username === null && apiKey === null) {
        return;
    }

    if (!username || !apiKey) {
        throw new Error("e621 authentication requires both a username and API key");
    }

    return {
        basicAuth: {
            host: "e621.net",
            username,
            password: apiKey,
        },
    };
}

export async function gallery(input, idhan) {
    const sourceUrl = new URL(input.url);
    const tags = sourceUrl.searchParams.get("tags") ?? "";
    const apiUrl = new URL("https://e621.net/posts.json");
    apiUrl.searchParams.set("tags", tags);
    apiUrl.searchParams.set("limit", String(pageSize));
    let previousLastId = null;

    while (true) {
        const response = await idhan.request({
            url: apiUrl.href,
            referer: sourceUrl.href,
            responseType: "text",
        });
        const {posts} = parseJsonResponse(galleryResponseSchema, response, "e621 gallery API");

        if (posts.length === 0) {
            break;
        }

        for (const post of posts) {
            idhan.follow({url: postUrl(post.id)});
        }

        if (posts.length < pageSize) {
            break;
        }

        const lastId = posts.at(-1).id;

        if (lastId === previousLastId) {
            throw new Error(`e621 pagination repeated post ${lastId}`);
        }

        previousLastId = lastId;
        apiUrl.searchParams.set("page", `b${lastId}`);
    }
}

export async function post(input, idhan) {
    const id = postId(input.url);
    const sourceUrl = postUrl(id);
    const response = await idhan.request({
        url: postDataUrl(id),
        referer: sourceUrl,
        responseType: "text",
    });
    const {post} = parseJsonResponse(postResponseSchema, response, "e621 post API");

    if (String(post.id) !== id) {
        throw new Error(`e621 returned post ${post.id} instead of ${id}`);
    }

    const file = post.file;
    const fileUrl = file.url;

    if (!fileUrl) {
        throw new Error(`e621 post ${id} has no downloadable file`);
    }

    const filename = new URL(fileUrl).pathname.split("/").pop();

    if (input.flags["e621.followSiblings"]) {
        followSiblings(idhan, post, id);
    }

    idhan.import({
        request: {
            url: fileUrl,
            referer: sourceUrl,
        },
        filename,
        urls: [
            {url: sourceUrl, type: "post"},
            {url: fileUrl, type: "file"},
        ],
        discoveredUrls: sourceUrls(post.sources),
        tags: postTags(post),
        metadata: {
            site: "e621",
            postId: id,
            md5: file.md5,
            extension: file.ext,
            width: file.width,
            height: file.height,
            expectedSize: file.size,
            rating: post.rating,
            createdAt: post.created_at,
            updatedAt: post.updated_at,
            uploaderId: post.uploader_id,
            uploaderName: post.uploader_name,
            score: post.score?.total,
            favorites: post.fav_count,
        },
    });
}
