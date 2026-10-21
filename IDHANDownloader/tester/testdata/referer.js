export const manifest = {
    id: "test.referer",
    version: 1,
};

export async function post(input, idhan) {
    const response = await idhan.request({
        url: input.url,
        responseType: "json",
    });

    if (response.body.headers.Referer !== input.url) {
        throw new Error(`Expected default referer ${input.url}, received ${response.body.headers.Referer}`);
    }

    const userAgent = response.body.headers["User-Agent"];

    if (!/^IDHAN\/\d+\.\d+\.\d+ \(\+https:\/\/github\.com\/kj16609\/IDHAN\)$/.test(userAgent)) {
        throw new Error(`Expected a versioned IDHAN User-Agent, received ${userAgent}`);
    }

    const explicitReferer = "https://example.com/explicit";
    const overridden = await idhan.request({
        url: input.url,
        referer: explicitReferer,
        responseType: "json",
    });

    if (overridden.body.headers.Referer !== explicitReferer) {
        throw new Error(`Expected explicit referer ${explicitReferer}, received ${overridden.body.headers.Referer}`);
    }

    return {complete: true};
}
