// Enough of the superagent request builder for the OpenAPI generator's JavaScript client, backed
// by idhan.request. Multipart bodies are not supported: the downloader has no upload path.

function lowerKeys(headers) {
    const output = {};

    for (const [name, value] of Object.entries(headers ?? {})) {
        output[name.toLowerCase()] = value;
    }

    return output;
}

// application/json, and the structured suffixes: application/problem+json, application/vnd.api+json.
const jsonType = /^application\/(\w+\+)?json/i;

function isJsonType(type) {
    return jsonType.test(type ?? "");
}

const base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

function base64(text) {
    let output = "";

    for (let index = 0; index < text.length; index += 3) {
        const bytes = [text.charCodeAt(index), text.charCodeAt(index + 1), text.charCodeAt(index + 2)];
        const held = (bytes[0] << 16) | ((bytes[1] || 0) << 8) | (bytes[2] || 0);

        output += base64Alphabet[(held >> 18) & 63];
        output += base64Alphabet[(held >> 12) & 63];
        output += Number.isNaN(bytes[1]) ? "=" : base64Alphabet[(held >> 6) & 63];
        output += Number.isNaN(bytes[2]) ? "=" : base64Alphabet[held & 63];
    }

    return output;
}

class Response {
    constructor(request, response, parsed) {
        this.req = request;
        this.xhr = null;
        this.status = response.status;
        this.statusCode = response.status;
        this.ok = response.status >= 200 && response.status < 300;
        this.headers = lowerKeys(response.headers);
        this.header = this.headers;
        this.type = (this.headers["content-type"] ?? "").split(";")[0].trim();
        this.text = parsed.text;
        this.body = parsed.body;
    }

    get(name) {
        return this.headers[name.toLowerCase()];
    }
}

class Agent {
    _attachCookies() {
    }

    _saveCookies() {
    }
}

class Request {
    constructor(method, url) {
        this.method = String(method).toUpperCase();
        this.url = url;
        this.header = {};
        this._query = new URLSearchParams();
        this._rawQuery = [];
        this._body = null;
        this._responseType = null;
        this._timeout = null;
    }

    use(plugin) {
        plugin(this);
        return this;
    }

    set(name, value) {
        if (typeof name === "object" && name !== null) {
            for (const [key, held] of Object.entries(name)) {
                this.set(key, held);
            }

            return this;
        }

        this.header[name] = String(value);
        return this;
    }

    unset(name) {
        delete this.header[name];
        return this;
    }

    auth(username, password) {
        return this.set("Authorization", `Basic ${base64(`${username}:${password}`)}`);
    }

    query(values) {
        if (typeof values === "string") {
            if (values !== "") {
                this._rawQuery.push(values);
            }

            return this;
        }

        for (const [name, value] of Object.entries(values ?? {})) {
            if (value === undefined || value === null) {
                continue;
            }

            if (Array.isArray(value)) {
                for (const held of value) {
                    this._query.append(name, String(held));
                }
            } else {
                this._query.append(name, String(value));
            }
        }

        return this;
    }

    type(value) {
        return this.set("Content-Type", value);
    }

    accept(value) {
        return this.set("Accept", value);
    }

    send(body) {
        if (typeof body === "string" || body === null || body === undefined) {
            this._body = body;
            return this;
        }

        if (!this.header["Content-Type"]) {
            this.type("application/json");
        }

        this._body = JSON.stringify(body);
        return this;
    }

    field() {
        throw new Error("superagent: multipart form bodies are not supported by the downloader");
    }

    attach() {
        throw new Error("superagent: file uploads are not supported by the downloader");
    }

    responseType(value) {
        this._responseType = value;
        return this;
    }

    timeout(value) {
        this._timeout = value;
        return this;
    }

    agent() {
        return this;
    }

    withCredentials() {
        return this;
    }

    href() {
        const encoded = this._query.toString();
        const query = [...this._rawQuery, ...(encoded === "" ? [] : [encoded])].join("&");

        if (query === "") {
            return this.url;
        }

        return `${this.url}${this.url.includes("?") ? "&" : "?"}${query}`;
    }

    end(callback) {
        idhan.request({
            url: this.href(),
            method: this.method,
            headers: this.header,
            body: this._body ?? undefined,
            responseType: this._responseType === "blob" ? "bytes" : "text",
        }).then(
            (response) => {
                let parsed;

                try {
                    parsed = this._parse(response);
                } catch (error) {
                    callback(error, null);
                    return;
                }

                const result = new Response(this, response, parsed);

                if (result.ok) {
                    callback(null, result);
                    return;
                }

                const error = new Error(`cannot ${this.method} ${this.url} (${result.status})`);
                error.status = result.status;
                error.response = result;
                callback(error, result);
            },
            (error) => callback(error, null),
        );

        return this;
    }

    then(resolve, reject) {
        return new Promise((settle, fail) => {
            this.end((error, response) => (error ? fail(error) : settle(response)));
        }).then(resolve, reject);
    }

    _parse(response) {
        if (this._responseType === "blob") {
            return {text: undefined, body: response.body};
        }

        const type = (lowerKeys(response.headers)["content-type"] ?? "").split(";")[0].trim();

        if (this._responseType === "text" || !isJsonType(type) || response.body === "") {
            return {text: response.body, body: null};
        }

        return {text: response.body, body: JSON.parse(response.body)};
    }
}

function superagent(method, url) {
    return new Request(method, url);
}

superagent.agent = Agent;
superagent.Request = Request;
superagent.Response = Response;

for (const method of ["get", "post", "put", "patch", "delete", "head", "options"]) {
    superagent[method] = (url) => new Request(method, url);
}

export default superagent;
