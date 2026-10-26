export const manifest = {
    id: "test.request-rate",
    version: 1,
};

export async function test(input, idhan) {
    const started = Date.now();

    await idhan.request({url: input.url, responseType: "text"});
    await idhan.request({url: input.url, responseType: "text"});

    const elapsed = Date.now() - started;

    if (elapsed < 9900) {
        throw new Error(`Two requests completed too quickly: ${elapsed} ms`);
    }

    console.log(`Two requests completed in ${elapsed} ms`);
}
