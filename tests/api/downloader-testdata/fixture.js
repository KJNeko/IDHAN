export const manifest = {
    cookies: [{name: "fringeBenefits", value: "yup", domain: "127.0.0.1", path: "/"}],
};

export async function gallery(input, idhan) {
    const response = await idhan.request({
        url: new URL("/gallery.json", input.url).href,
        responseType: "json",
    });

    idhan.follow({url: `${response.body.post}?source=one`});
    idhan.follow({url: `${response.body.post}?source=two`});
}

export function post(input, idhan) {
    idhan.import({
        request: {url: new URL("/file.gif", input.url).href},
        filename: "fixture.gif",
        urls: [{url: input.url, type: "post"}],
        discoveredUrls: [new URL("/source", input.url).href],
        tags: ["fixture"],
    });
}

export function failure() {
    throw new Error("deterministic parser failure");
}
