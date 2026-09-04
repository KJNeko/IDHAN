export const manifest = {
    id: "test.manifest-routing",
    version: 1,
};

export function gallery(input, idhan) {
    const queued = idhan.follow({
        url: new URL("/items/1?view=post", input.url).href,
    });

    if (queued !== undefined) {
        throw new Error("idhan.follow must return immediately without a completion value");
    }

    return {complete: true};
}

export function post() {
    return {complete: true};
}
