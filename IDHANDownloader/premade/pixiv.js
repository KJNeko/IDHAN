import {parseJsonResponse, z} from "validation";

export const manifest = {
    id: "net.pixiv",
    version: 1,
};

// Matches artwork URLs such as https://www.pixiv.net/en/artworks/12345.
const artworkPath = /^\/(?:[a-z]{2}\/)?artworks\/(\d+)\/?$/;
// Matches creator URLs such as https://www.pixiv.net/en/users/12345/illustrations.
const userPath = /^\/(?:[a-z]{2}\/)?users\/(\d+)(?:\/illustrations)?\/?$/;
// Extracts the artwork ID from an image filename such as 12345_p0.jpg.
const filenameArtworkId = /^(\d+)/;
// Matches the first-page suffix in an image URL such as https://i.pximg.net/img-original/12345_p0.jpg.
const firstPageSuffix = /_p0(\.[a-zA-Z0-9]+)$/;
const idSchema = z.union([z.string(), z.number()]);
const workCollectionSchema = z.union([
    z.array(idSchema),
    z.record(z.string(), z.unknown()),
]);

function resultSchema(body) {
    return z.discriminatedUnion("error", [
        z.object({
            error: z.literal(true),
            message: z.string().nullish(),
        }),
        z.object({
            error: z.literal(false),
            message: z.string().nullish(),
            body,
        }),
    ]);
}

const artistResponseSchema = resultSchema(z.object({
    illusts: workCollectionSchema.nullish().transform((works) => works ?? {}),
    manga: workCollectionSchema.nullish().transform((works) => works ?? {}),
}));
const artworkResponseSchema = resultSchema(z.object({
    illust_details: z.object({
        id: idSchema,
        ugoira_meta: z.union([z.null(), z.object({}).passthrough()]),
        url_big: z.string().nullish(),
        page_count: idSchema,
        tags: z.array(z.string()).nullish().transform((tags) => tags ?? []),
        title: z.string(),
        author_details: z.object({
            user_id: idSchema,
            user_name: z.string(),
            user_account: z.string(),
        }),
        upload_timestamp: idSchema.nullish(),
        x_restrict: z.coerce.number().nullish(),
        ai_type: z.coerce.number().nullish(),
    }),
}));

function artworkId(value) {
    const url = new URL(value);
    const match = artworkPath.exec(url.pathname);

    if (url.hostname !== "www.pixiv.net" || match === null) {
        throw new Error(`Invalid Pixiv artwork URL: ${value}`);
    }

    return match[1];
}

function imageArtworkId(value) {
    const url = new URL(value);
    const filename = url.pathname.slice(url.pathname.lastIndexOf("/") + 1);
    const match = filenameArtworkId.exec(filename);

    if (url.hostname !== "i.pximg.net" || match === null) {
        throw new Error(`Invalid Pixiv image URL: ${value}`);
    }

    return match[1];
}

function artistId(value) {
    const url = new URL(value);
    const match = userPath.exec(url.pathname);

    if (url.hostname !== "www.pixiv.net" || match === null) {
        throw new Error(`Invalid Pixiv artist URL: ${value}`);
    }

    return match[1];
}

function postUrl(id) {
    return `https://www.pixiv.net/artworks/${id}`;
}

function detailsUrl(id) {
    const url = new URL("https://www.pixiv.net/touch/ajax/illust/details");
    url.searchParams.set("illust_id", id);
    return url;
}

function artistUrl(id) {
    return `https://www.pixiv.net/users/${id}/illustrations`;
}

function artistProfileUrl(id) {
    return `https://www.pixiv.net/ajax/user/${id}/profile/all`;
}

function filenameFromUrl(url) {
    const pathname = url.pathname;
    return pathname.slice(pathname.lastIndexOf("/") + 1);
}

function pageUrl(originalUrl, page) {
    const url = new URL(originalUrl);
    const match = firstPageSuffix.exec(url.pathname);

    if (match === null) {
        throw new Error(`Pixiv returned an unexpected original file URL: ${originalUrl}`);
    }

    url.pathname = `${url.pathname.slice(0, match.index)}_p${page}${match[1]}`;
    return url;
}

function postTags(details) {
    return [...new Set(details.tags)];
}

function workIds(value) {
    return Array.isArray(value) ? value.map(String) : Object.keys(value ?? {});
}

export function image(input, idhan) {
    idhan.follow({
        url: postUrl(imageArtworkId(input.url)),
    });
}

export async function user(input, idhan) {
    const id = artistId(input.url);
    const sourceUrl = artistUrl(id);
    const response = await idhan.request({
        url: artistProfileUrl(id),
        referer: sourceUrl,
        responseType: "text",
    });
    const result = parseJsonResponse(artistResponseSchema, response, "Pixiv artist API");

    if (result.error !== false) {
        throw new Error(`Pixiv rejected artist ${id}: ${result.message ?? "unknown error"}`);
    }

    const illustrationIds = workIds(result.body.illusts);
    const mangaIds = workIds(result.body.manga);

    console.log(`Found ${illustrationIds.length} illustrations and ${mangaIds.length} manga`);

    for (const workId of [...illustrationIds, ...mangaIds]) {
        idhan.follow({
            url: postUrl(workId),
        });
    }
}

export async function post(input, idhan) {
    const id = artworkId(input.url);
    const sourceUrl = postUrl(id);
    const response = await idhan.request({
        url: detailsUrl(id).href,
        referer: sourceUrl,
        responseType: "text",
    });
    const result = parseJsonResponse(artworkResponseSchema, response, "Pixiv artwork API");

    if (result.error !== false) {
        throw new Error(`Pixiv rejected artwork ${id}: ${result.message ?? "unknown error"}`);
    }

    const details = result.body.illust_details;

    if (String(details.id) !== id) {
        throw new Error(`Pixiv returned artwork ${details.id} instead of ${id}`);
    }

    if (details.ugoira_meta !== null) {
        throw new Error(`Pixiv artwork ${id} is an ugoira, which this parser does not support yet`);
    }

    const originalUrl = details.url_big;
    const pageCount = Number(details.page_count);

    if (!originalUrl || pageCount < 1) {
        throw new Error(`Pixiv artwork ${id} has no downloadable pages`);
    }

    const tags = postTags(details);
    const author = details.author_details;

    for (let page = 0; page < pageCount; ++page) {
        const fileUrl = pageUrl(originalUrl, page);
        idhan.import({
            request: {
                url: fileUrl.href,
                referer: sourceUrl,
            },
            filename: filenameFromUrl(fileUrl),
            urls: [
                {url: sourceUrl, type: "post"},
                {url: fileUrl.href, type: "file"},
            ],
            discoveredUrls: [],
            tags,
            metadata: {
                site: "pixiv",
                postId: id,
                page,
                pageCount,
                title: details.title,
                authorId: String(author.user_id),
                authorName: author.user_name,
                authorAccount: author.user_account,
                uploadedAt: details.upload_timestamp,
                rating: details.x_restrict,
                aiType: details.ai_type,
            },
        });
    }
}
