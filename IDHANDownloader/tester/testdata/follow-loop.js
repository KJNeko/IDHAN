export const manifest = {
    id: "test.follow-loop",
    version: 1,
};

export function post(input, idhan) {
    const current = new URL(input.url);
    current.pathname = current.pathname === "/first" ? "/second" : "/first";
    const queued = idhan.follow({url: current.href});

    if (queued !== undefined) {
        throw new Error("idhan.follow must return immediately without a completion value");
    }
}
