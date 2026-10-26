export const manifest = {
    id: "test.follow-imported",
    version: 1,
};

export function post(input, idhan) {
    const current = new URL(input.url);

    if (current.pathname === "/") {
        idhan.import({
            request: {url: current.href},
            filename: "example.html",
            urls: [{url: current.href, type: "post"}],
        });

        current.pathname = "/second";
        idhan.follow({url: current.href});
        return;
    }

    current.pathname = "/";
    idhan.follow({url: current.href});
}
