export const manifest = {
    id: "test.html",
    version: 1,
};

function expect(actual, expected, description) {
    if (actual !== expected) {
        throw new Error(`${description}: expected '${expected}', received '${actual}'`);
    }
}

export function test(_input, idhan) {
    const document = idhan.parseHtml(`
        <!doctype html>
        <main>
            <a class="item primary" href="/one"><span>One</span></a>
            <a class="item" href="/two" data-kind="second">Two</a>
            <input id="source" value="source value">
        </main>
    `);

    const links = document.querySelectorAll("main > a.item");
    expect(links.length, 2, "selector result count");
    expect(links[0].href, "/one", "href property");
    expect(links[0].textContent, "One", "nested text content");
    expect(links[1].getAttribute("data-kind"), "second", "attribute lookup");
    expect(links[1].getAttribute("missing"), null, "missing attribute");
    expect(links[1].hasAttribute("data-kind"), true, "present attribute");
    expect(links[1].hasAttribute("missing"), false, "absent attribute");
    expect(links[1].tagName, "A", "tag name");
    expect(document.querySelector("#source").value, "source value", "value property");
    expect(document.querySelector(".missing"), null, "missing selector");
    expect(links[0].querySelector("span").textContent, "One", "element selector");

    return {complete: true};
}
