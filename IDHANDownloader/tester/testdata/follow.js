export const manifest = {
    id: "test.follow",
    version: 1,
};

export function gallery(input, idhan) {
    const first = idhan.follow({url: `${input.url}/one`});
    const second = idhan.follow({url: `${input.url}/two`});

    if (first !== undefined || second !== undefined) {
        throw new Error("idhan.follow must return immediately without a completion value");
    }
}

export function post(input, idhan) {
    const queued = idhan.import({
        request: {url: `${input.url}.jpg`},
        filename: `${new URL(input.url).pathname.slice(1)}.jpg`,
        urls: [{url: input.url, type: "post"}],
        tags: ["tester"],
    });

    if (queued !== undefined) {
        throw new Error("idhan.import must return immediately without a completion value");
    }
}
