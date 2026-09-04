export async function gallery(input, idhan) {
    const response = await idhan.request({url: `${input.url}/data`, responseType: "json"});
    if (response.body.ok !== true) throw new Error("request result was not propagated");

    idhan.follow({url: `${input.url}/child`});
    idhan.follow({url: `${input.url}/child`});
    idhan.import({
        request: {url: `${input.url}/file`},
        filename: "fixture.png",
        urls: [{url: input.url, type: "gallery"}],
        tags: ["fixture"],
    });
}
