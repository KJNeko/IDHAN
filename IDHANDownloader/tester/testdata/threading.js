export const manifest = {
    id: "test.threading",
    version: 1,
};

export function gallery(_input, idhan) {
    console.log("THREAD parent queued child");
    idhan.follow({url: "https://thread-child.example/post"});

    const deadline = Date.now() + 500;

    while (Date.now() < deadline) {
        // Keep this JavaScript invocation occupied while another domain runs.
    }

    console.log("THREAD parent finished");
}

export function post() {
    console.log("THREAD child executed");
}
