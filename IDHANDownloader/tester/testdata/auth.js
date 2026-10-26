let authCalls = 0;

export const manifest = {
    id: "test.auth",
    version: 1,
    auth: "auth",
};

export function auth(server, idhan) {
    ++authCalls;

    const username = idhan.secret("test.username");
    const password = idhan.secret("test.password");

    if (idhan.secret("test.missing") !== null) {
        throw new Error("Missing secrets must return null");
    }

    return {
        basicAuth: {
            host: "httpbin.org",
            username,
            password,
        },
    };
}

export async function test(input, idhan) {
    if (authCalls !== 1) {
        throw new Error(`Expected one auth call, received ${authCalls}`);
    }

    const response = await idhan.request({
        url: new URL("/basic-auth/test-user/test-token", input.url).href,
        responseType: "json",
    });

    if (response.body.authenticated !== true || response.body.user !== "test-user") {
        throw new Error("The session did not apply Basic authentication");
    }

    return {complete: true};
}
