export const manifest = {
    id: "com.idhan.test.cookies",
    version: 1,
    cookies: [
        {
            name: "manifest_cookie",
            value: "available",
            domain: "httpbin.org",
            path: "/",
            secure: true,
        },
    ],
};

export function configure() {
    return {
        cookies: [
            {
                name: "configured_cookie",
                value: "available",
                domain: "httpbin.org",
                path: "/",
                secure: true,
            },
        ],
    };
}

function expectCookie(response, name, value) {
    if (response.body.cookies[name] !== value) {
        throw new Error(`Expected ${name} to be sent`);
    }
}

export async function test(input, idhan) {
    const initial = await idhan.request({
        url: new URL("/cookies", input.url).href,
        responseType: "json",
    });

    expectCookie(initial, "manifest_cookie", "available");
    expectCookie(initial, "configured_cookie", "available");

    await idhan.request({
        url: new URL(
            "/response-headers?Set-Cookie=response_cookie%3Dstored%3B%20Path%3D%2F%3B%20Secure",
            input.url,
        ).href,
        responseType: "json",
    });

    const persisted = await idhan.request({
        url: new URL("/cookies", input.url).href,
        responseType: "json",
    });

    expectCookie(persisted, "response_cookie", "stored");
}
