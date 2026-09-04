import {parseJsonResponse, requireOk, z} from "validation";

export const manifest = {
    id: "com.gelbooru",
    version: 1,
    flags: [
        {
            name: "gelbooru.followSource",
            description: "Submit a post's source URL back to the parser pipeline.",
            default: false,
        },
    ],
    cookies: [
        {
            name: "fringeBenefits",
            value: "yup",
            domain: "gelbooru.com",
            path: "/",
        },
    ],
};

export function configure(server) {
    const tagBlacklist = server.tagBlacklist ?? [];

    if (tagBlacklist.length === 0) {
        return {};
    }

    return {
        cookies: [
            {
                name: "tag_blacklist",
                value: encodeURIComponent(tagBlacklist.join("%20")),
                domain: "gelbooru.com",
                path: "/",
            },
        ],
    };
}

function absoluteUrl(value, base) {
    return new URL(value, base);
}

// Matches a decimal post ID such as 12345.
const decimalId = /^\d+$/;

function postId(value) {
    const id = new URL(value).searchParams.get("id");

    if (!decimalId.test(id ?? "")) {
        throw new Error(`Invalid Gelbooru post URL: ${value}`);
    }

    return id;
}

function postUrl(id) {
    const url = new URL("https://gelbooru.com/index.php");
    url.searchParams.set("page", "post");
    url.searchParams.set("s", "view");
    url.searchParams.set("id", id);

    return url;
}

function filenameFromUrl(url) {
    const pathname = url.pathname;

    return pathname.slice(pathname.lastIndexOf("/") + 1);
}

const API_PAGE_SIZE = 100;

const TAG_NAMESPACES = {
    1: "artist",
    2: "series",
    3: "copyright",
    4: "character",
    5: "meta",
};
const idSchema = z.union([z.string(), z.number()]);
const postSchema = z.object({
    id: idSchema,
    file_url: z.string().min(1),
    source: z.string().nullish(),
    tags: z.string().default(""),
});
const tagSchema = z.object({
    name: z.string(),
    type: z.coerce.number(),
});

// Gelbooru varies between omitted, singleton, wrapped, and bare-array results.
function recordsSchema(member, entrySchema) {
    const entriesSchema = z.union([entrySchema, z.array(entrySchema)]).nullish();

    return z.union([
        z.array(entrySchema),
        z.object({[member]: entriesSchema}),
        z.null(),
    ]).transform((body) => {
        if (Array.isArray(body)) {
            return body;
        }

        const records = body?.[member];

        if (records === undefined || records === null) {
            return [];
        }

        return Array.isArray(records) ? records : [records];
    });
}

const postRecordsResponseSchema = recordsSchema("post", postSchema);
const tagRecordsResponseSchema = recordsSchema("tag", tagSchema);

function apiCredentials(idhan) {
    const apiKey = idhan.secret("gelbooru.apiKey");
    const userID = idhan.secret("gelbooru.userID");

    if (apiKey === null || userID === null) {
        throw new Error(
            "Gelbooru's API needs the gelbooru.apiKey and gelbooru.userID secrets. " +
            "Both are on the site's account options page.",
        );
    }

    return {apiKey, userID};
}

function apiUrl(base, credentials, section, parameters) {
    const url = new URL("/index.php", base);

    url.searchParams.set("page", "dapi");
    url.searchParams.set("s", section);
    url.searchParams.set("q", "index");
    url.searchParams.set("json", "1");

    for (const [name, value] of Object.entries(parameters)) {
        url.searchParams.set(name, value);
    }

    url.searchParams.set("api_key", credentials.apiKey);
    url.searchParams.set("user_id", credentials.userID);

    return url;
}

function redactApiUrl(value) {
    const url = new URL(value);

    for (const name of ["api_key", "user_id"]) {
        if (url.searchParams.has(name)) url.searchParams.set(name, "<redacted>");
    }

    return url;
}

// Bad credentials may return an empty 200, so retain the raw response for diagnostics.
async function requestApi(idhan, url, schema, description) {
    const response = await idhan.request({
        url: url.href,
        responseType: "text",
        sensitiveQuery: ["api_key", "user_id"],
    });

    requireOk(response, `${description} at ${redactApiUrl(url)}`);

    if (response.body.trim() === "") {
        throw new Error(
            "Gelbooru's API returned an empty body. This normally means gelbooru.apiKey or " +
            "gelbooru.userID is wrong: each wants the bare value, not the whole 'user_id=123' pair.",
        );
    }

    return parseJsonResponse(schema, response, description);
}

// The post API omits tag categories, so fetch them in batches.
async function tagTypes(idhan, base, credentials, names) {
    const types = new Map();

    for (let index = 0; index < names.length; index += API_PAGE_SIZE) {
        const batch = names.slice(index, index + API_PAGE_SIZE);
        const url = apiUrl(base, credentials, "tag", {
            names: batch.join(" "),
            limit: String(API_PAGE_SIZE),
        });

        for (const tag of await requestApi(idhan, url, tagRecordsResponseSchema, "Gelbooru tag API")) {
            types.set(tag.name, tag.type);
        }
    }

    return types;
}

function namespacedTags(names, types) {
    return names.map((name) => {
        const namespace = TAG_NAMESPACES[types.get(name)];

        return namespace === undefined ? name : `${namespace}:${name}`;
    });
}

function tagNames(post) {
    // Gelbooru returns tags as whitespace-separated names such as "cat blue_eyes".
    return post.tags.split(/\s+/).filter((name) => name !== "");
}

// Gallery pid is an offset; the API expects a page number.
function startingPage(url) {
    const offset = Number.parseInt(new URL(url).searchParams.get("pid") ?? "0", 10);

    if (!Number.isInteger(offset) || offset <= 0) {
        return 0;
    }

    return Math.floor(offset / API_PAGE_SIZE);
}

function importPost(idhan, input, entry, types) {
    const id = String(entry.id);
    const sourceUrl = postUrl(id);
    const fileUrl = absoluteUrl(entry.file_url, input.url);
    const originalSourceUrl = entry.source?.trim() ?? "";
    const discoveredUrls = [];

    if (originalSourceUrl !== "") {
        discoveredUrls.push(originalSourceUrl);

        if (input.flags["gelbooru.followSource"]) {
            idhan.follow({url: originalSourceUrl});
        }
    }

    idhan.import({
        request: {
            url: fileUrl.href,
            referer: sourceUrl.href,
        },
        filename: filenameFromUrl(fileUrl),
        urls: [
            {url: sourceUrl.href, type: "post"},
            {url: fileUrl.href, type: "file"},
        ],
        discoveredUrls,
        tags: namespacedTags(tagNames(entry), types),
        metadata: {
            site: "gelbooru",
            postId: id,
        },
    });
}

// Resolve the page's shared tag set in one batch.
async function importPosts(idhan, input, credentials, posts) {
    const names = new Set();

    for (const entry of posts) {
        for (const name of tagNames(entry)) {
            names.add(name);
        }
    }

    const types = await tagTypes(idhan, input.url, credentials, [...names]);

    for (const entry of posts) importPost(idhan, input, entry, types);
}

export async function gallery(input, idhan) {
    const credentials = apiCredentials(idhan);
    const tags = new URL(input.url).searchParams.get("tags") ?? "";

    let page = startingPage(input.url);

    for (; ;) {
        const url = apiUrl(input.url, credentials, "post", {
            limit: String(API_PAGE_SIZE),
            pid: String(page),
            tags,
        });
        const posts = await requestApi(idhan, url, postRecordsResponseSchema, "Gelbooru post API");

        console.log(`Gelbooru gallery page ${page} returned ${posts.length} posts`);

        await importPosts(idhan, input, credentials, posts);

        if (posts.length < API_PAGE_SIZE) {
            return;
        }

        ++page;
    }
}

export async function post(input, idhan) {
    const credentials = apiCredentials(idhan);
    const id = postId(input.url);

    const url = apiUrl(input.url, credentials, "post", {id});
    const posts = await requestApi(idhan, url, postRecordsResponseSchema, "Gelbooru post API");

    if (posts.length === 0) {
        throw new Error(`Gelbooru has no post ${id}`);
    }

    await importPosts(idhan, input, credentials, posts);
}
