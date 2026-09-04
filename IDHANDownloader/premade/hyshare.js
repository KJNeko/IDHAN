import {parseJsonResponse, z} from "validation";

export const manifest = {
    id: "zip.lolicon.share",
    version: 1,
};

// Matches a lowercase SHA-256 such as e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855.
const sha256Pattern = /^[0-9a-f]{64}$/;
// Trims prose punctuation so "https://share.example/gallery/cats)." becomes "https://share.example/gallery/cats".
const closingPunctuation = /[)\]}>.,;]+$/;
const sha256Schema = z.string().regex(sha256Pattern);
const galleryResponseSchema = z.object({
    hashes: z.array(sha256Schema),
});
const wellKnownResponseSchema = z.object({
    hyshare: z.literal("1"),
});
const postResponseSchema = z.object({
    hash: sha256Schema,
    ext: z.string(),
    detailed_known_urls: z.array(z.object({normalised_url: z.string()})).nullish()
        .transform((urls) => urls ?? []),
    tag_services_to_tags: z.record(z.string(), z.array(z.string())).nullish()
        .transform((tags) => tags ?? {}),
    title: z.string().nullish(),
    mime: z.string().nullish(),
    size: z.number().nullish(),
    width: z.number().nullish(),
    height: z.number().nullish(),
});

function inputUrl(value) {
    return new URL(value.trim().replace(closingPunctuation, ""));
}

function routeValue(value, route) {
    const url = inputUrl(value);
    const prefix = `/${route}/`;
    const pathname = url.pathname.endsWith("/") ? url.pathname.slice(0, -1) : url.pathname;

    if (!pathname.startsWith(prefix)) {
        throw new Error(`Invalid HyShare ${route} URL: ${value}`);
    }

    const encodedValue = pathname.slice(prefix.length);

    if (encodedValue === "" || encodedValue.includes("/")) {
        throw new Error(`Invalid HyShare ${route} URL: ${value}`);
    }

    return decodeURIComponent(encodedValue);
}

function galleryDataUrl(value) {
    const url = inputUrl(value);
    const gallery = routeValue(value, "gallery");
    url.pathname = `/gallery/${encodeURIComponent(gallery)}/data.json`;
    url.search = "";
    url.hash = "";
    return url;
}

function postUrl(base, hash) {
    return new URL(`/view/${hash}`, base);
}

function postDataUrl(base, hash) {
    return new URL(`/view/${hash}/data.json`, base);
}

function fileUrl(base, hash) {
    return new URL(`/file/${hash}`, base);
}

function requireHash(value) {
    if (typeof value !== "string" || !sha256Pattern.test(value)) {
        throw new Error(`HyShare returned an invalid SHA-256 hash: ${value}`);
    }

    return value;
}

function postTags(tagServices) {
    return [...new Set(Object.values(tagServices).flat())];
}

function discoveredUrls(knownUrls) {
    return [...new Set(knownUrls.map((knownUrl) => knownUrl.normalised_url))];
}

function wellKnownUrl(url) {
    return new URL(".well-known/hyshare", url);
}

export async function gallery(input, idhan) {
    const response = await idhan.request({
        url: galleryDataUrl(input.url).href,
        responseType: "text",
    });
    const gallery = parseJsonResponse(galleryResponseSchema, response, "HyShare gallery API");

    for (const hash of gallery.hashes) {
        idhan.follow({
            url: postUrl(response.url, hash).href,
        });
    }
}

export async function checkWellKnown(input, idhan) {
    const response = await idhan.request({
        url: wellKnownUrl(input.url).href,
        responseType: "text",
    });
    parseJsonResponse(wellKnownResponseSchema, response, "HyShare well-known endpoint");
}

export async function post(input, idhan) {
    const expectedHash = requireHash(routeValue(input.url, "view"));
    const response = await idhan.request({
        url: postDataUrl(input.url, expectedHash).href,
        responseType: "text",
    });
    const post = parseJsonResponse(postResponseSchema, response, "HyShare post API");
    const hash = post.hash;

    const sourceUrl = postUrl(response.url, hash);
    const downloadUrl = fileUrl(response.url, hash);

    idhan.import({
        request: {
            url: downloadUrl.href,
        },
        filename: `${hash}${post.ext}`,
        urls: [
            {url: sourceUrl.href, type: "post"},
            {url: downloadUrl.href, type: "file"},
        ],
        discoveredUrls: discoveredUrls(post.detailed_known_urls),
        tags: postTags(post.tag_services_to_tags),
        metadata: {
            site: "hyshare",
            sha256: hash,
            title: post.title,
            mime: post.mime,
            size: post.size,
            width: post.width,
            height: post.height,
        },
    });
}
