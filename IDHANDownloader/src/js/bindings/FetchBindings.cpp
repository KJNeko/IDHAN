#include "js/bindings/FetchBindings.hpp"

#include <string_view>

namespace idhan::downloader
{
namespace
{
constexpr std::string_view fetch_bootstrap { R"IDHAN_FETCH(
(function (global) {
    "use strict";

    const nativeRequest = global.idhan.request.bind(global.idhan);
    const redirectStatuses = new Set([301, 302, 303, 307, 308]);

    function headerName(value) {
        const name = String(value).toLowerCase();

        if (name === "" || !/^[!#$%&'*+.^_`|~0-9a-z-]+$/.test(name)) {
            throw new TypeError(`Invalid header name: ${value}`);
        }

        return name;
    }

    function headerValue(value) {
        return String(value).trim();
    }

    class Headers {
        constructor(init = undefined) {
            this._values = new Map();

            if (init === undefined || init === null) {
                return;
            }

            if (init instanceof Headers) {
                for (const [name, value] of init) {
                    this.set(name, value);
                }

                return;
            }

            if (Array.isArray(init) || typeof init[Symbol.iterator] === "function") {
                for (const entry of init) {
                    if (!Array.isArray(entry) || entry.length !== 2) {
                        throw new TypeError("Header entries must contain a name and value");
                    }

                    this.append(entry[0], entry[1]);
                }

                return;
            }

            if (typeof init !== "object") {
                throw new TypeError("Headers initializer must be an object or iterable");
            }

            for (const [name, value] of Object.entries(init)) {
                this.set(name, value);
            }
        }

        append(name, value) {
            const normalizedName = headerName(name);
            const normalizedValue = headerValue(value);
            const current = this._values.get(normalizedName);
            this._values.set(normalizedName, current === undefined ? normalizedValue : `${current}, ${normalizedValue}`);
        }

        delete(name) {
            this._values.delete(headerName(name));
        }

        get(name) {
            return this._values.get(headerName(name)) ?? null;
        }

        has(name) {
            return this._values.has(headerName(name));
        }

        set(name, value) {
            this._values.set(headerName(name), headerValue(value));
        }

        entries() {
            return this._values.entries();
        }

        keys() {
            return this._values.keys();
        }

        values() {
            return this._values.values();
        }

        forEach(callback, thisArg = undefined) {
            for (const [name, value] of this._values) {
                callback.call(thisArg, value, name, this);
            }
        }

        [Symbol.iterator]() {
            return this.entries();
        }

        _toObject() {
            return Object.fromEntries(this._values);
        }
    }

    function copyBody(body, headers) {
        if (body === undefined || body === null) {
            return null;
        }

        if (typeof body === "string") {
            if (!headers.has("content-type")) {
                headers.set("content-type", "text/plain;charset=UTF-8");
            }

            return body;
        }

        if (typeof URLSearchParams !== "undefined" && body instanceof URLSearchParams) {
            if (!headers.has("content-type")) {
                headers.set("content-type", "application/x-www-form-urlencoded;charset=UTF-8");
            }

            return body.toString();
        }

        if (body instanceof ArrayBuffer) {
            return body.slice(0);
        }

        if (ArrayBuffer.isView(body)) {
            return body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength);
        }

        throw new TypeError("Fetch bodies must be strings, URLSearchParams, ArrayBuffers, or typed arrays");
    }

    class Request {
        constructor(input, init = {}) {
            const source = input instanceof Request ? input : null;
            const rawUrl = source === null ? input : source.url;
            this.url = new URL(String(rawUrl)).href;
            this.method = String(init.method ?? source?.method ?? "GET").toUpperCase();

            if (["CONNECT", "TRACE", "TRACK"].includes(this.method)) {
                throw new TypeError(`Forbidden fetch method: ${this.method}`);
            }

            this.headers = new Headers(init.headers ?? source?.headers);
            const suppliedBody = Object.prototype.hasOwnProperty.call(init, "body") ? init.body : source?._body;
            this._body = copyBody(suppliedBody, this.headers);

            if ((this.method === "GET" || this.method === "HEAD") && this._body !== null) {
                throw new TypeError(`${this.method} requests cannot have a body`);
            }

            this.redirect = String(init.redirect ?? source?.redirect ?? "follow");

            if (!["follow", "error", "manual"].includes(this.redirect)) {
                throw new TypeError(`Invalid redirect mode: ${this.redirect}`);
            }

            this.credentials = String(init.credentials ?? source?.credentials ?? "same-origin");

            if (!["omit", "same-origin", "include"].includes(this.credentials)) {
                throw new TypeError(`Invalid credentials mode: ${this.credentials}`);
            }

            this.referrer = String(init.referrer ?? source?.referrer ?? "about:client");
            this.referrerPolicy = String(init.referrerPolicy ?? source?.referrerPolicy ?? "");
            this.mode = String(init.mode ?? source?.mode ?? "cors");
            this.cache = String(init.cache ?? source?.cache ?? "default");
            this.integrity = String(init.integrity ?? source?.integrity ?? "");
            this.keepalive = Boolean(init.keepalive ?? source?.keepalive ?? false);
            this.signal = init.signal ?? source?.signal ?? null;
            this.bodyUsed = false;
        }

        clone() {
            if (this.bodyUsed) {
                throw new TypeError("Cannot clone a used Request body");
            }

            return new Request(this);
        }
    }

    function consume(response) {
        if (response.bodyUsed) {
            throw new TypeError("Response body has already been consumed");
        }

        response.bodyUsed = true;
    }

    class Response {
        constructor(body = null, init = {}) {
            this.status = Number(init.status ?? 200);

            if (!Number.isInteger(this.status) || this.status < 200 || this.status > 599) {
                throw new RangeError("Response status must be between 200 and 599");
            }

            this.statusText = String(init.statusText ?? "");
            this.headers = new Headers(init.headers);
            this.url = "";
            this.redirected = false;
            this.type = "default";
            this.bodyUsed = false;
            this.body = null;
            this._text = body === null ? "" : String(body);
            this._bytes = body instanceof ArrayBuffer ? body.slice(0) : null;
        }

        get ok() {
            return this.status >= 200 && this.status <= 299;
        }

        async text() {
            consume(this);
            return this._text;
        }

        async json() {
            return JSON.parse(await this.text());
        }

        async arrayBuffer() {
            consume(this);

            if (this._bytes === null) {
                throw new TypeError("ArrayBuffer conversion is only available for fetched binary bodies");
            }

            return this._bytes.slice(0);
        }

        async blob() {
            throw new TypeError("Blob is not available in the IDHAN parser sandbox");
        }

        async formData() {
            throw new TypeError("FormData is not available in the IDHAN parser sandbox");
        }

        clone() {
            if (this.bodyUsed) {
                throw new TypeError("Cannot clone a used Response body");
            }

            return Response._fromNative({
                url: this.url,
                status: this.status,
                headers: this.headers._toObject(),
                body: this._bytes === null ? new ArrayBuffer(0) : this._bytes.slice(0),
                bodyText: this._text,
            }, this.redirected, this.type);
        }

        static error() {
            const response = Response._fromNative({url: "", status: 0, headers: {}, body: new ArrayBuffer(0), bodyText: ""});
            response.type = "error";
            return response;
        }

        static redirect(url, status = 302) {
            if (!redirectStatuses.has(status)) {
                throw new RangeError("Invalid redirect status");
            }

            return new Response(null, {status, headers: {location: new URL(String(url)).href}});
        }

        static json(data, init = {}) {
            const headers = new Headers(init.headers);

            if (!headers.has("content-type")) {
                headers.set("content-type", "application/json");
            }

            return new Response(JSON.stringify(data), {...init, headers});
        }

        static _fromNative(native, redirected = false, type = "basic") {
            const response = Object.create(Response.prototype);
            response.status = native.status;
            response.statusText = "";
            response.headers = new Headers(native.headers);
            response.url = native.url;
            response.redirected = redirected;
            response.type = type;
            response.bodyUsed = false;
            response.body = null;
            response._text = native.bodyText;
            response._bytes = native.body;
            return response;
        }
    }

    async function fetch(input, init = {}) {
        const request = new Request(input, init);

        if (request.signal?.aborted) {
            throw new Error("The fetch request was aborted");
        }

        const originalOrigin = new URL(request.url).origin;
        let url = request.url;
        let method = request.method;
        let body = request._body;
        let headers = new Headers(request.headers);
        let redirected = false;

        for (let redirectCount = 0; redirectCount <= 20; ++redirectCount) {
            const targetOrigin = new URL(url).origin;
            const omitCredentials = request.credentials === "omit"
                || (request.credentials === "same-origin" && targetOrigin !== originalOrigin);
            const options = {
                url,
                method,
                headers: headers._toObject(),
                body,
                responseType: "fetch",
                allowHttpErrors: true,
                omitCredentials,
            };

            if (request.referrer !== "about:client") {
                options.referer = request.referrer;
            }

            const native = await nativeRequest(options);
            const response = Response._fromNative(native, redirected);

            if (!redirectStatuses.has(response.status)) {
                return response;
            }

            if (request.redirect === "manual") {
                return response;
            }

            if (request.redirect === "error") {
                throw new TypeError(`Redirect encountered while fetching ${url}`);
            }

            const location = response.headers.get("location");

            if (location === null) {
                return response;
            }

            if (redirectCount === 20) {
                throw new TypeError("Fetch exceeded 20 redirects");
            }

            const nextUrl = new URL(location, url).href;

            if (new URL(nextUrl).origin !== targetOrigin) {
                headers.delete("authorization");
                headers.delete("cookie");
            }

            if (response.status === 303 || ((response.status === 301 || response.status === 302) && method === "POST")) {
                method = "GET";
                body = null;
                headers.delete("content-length");
                headers.delete("content-type");
            }

            url = nextUrl;
            redirected = true;
        }

        throw new TypeError("Fetch redirect processing failed");
    }

    Object.defineProperties(global, {
        Headers: {value: Headers, writable: true, configurable: true},
        Request: {value: Request, writable: true, configurable: true},
        Response: {value: Response, writable: true, configurable: true},
        fetch: {value: fetch, writable: true, configurable: true},
    });
})(globalThis);
)IDHAN_FETCH" };
} // namespace

std::expected< void, std::string > installFetchBindings( JSContext* context )
{
	JSValue result { JS_Eval(
		context, fetch_bootstrap.data(), fetch_bootstrap.size(), "<IDHAN fetch bootstrap>", JS_EVAL_TYPE_GLOBAL ) };

	if ( !JS_IsException( result ) )
	{
		JS_FreeValue( context, result );
		return {};
	}

	JS_FreeValue( context, result );
	JSValue exception { JS_GetException( context ) };
	const char* text { JS_ToCString( context, exception ) };
	std::string message { text == nullptr ? "unknown failure" : text };

	if ( text != nullptr ) JS_FreeCString( context, text );

	JS_FreeValue( context, exception );
	return std::unexpected( std::move( message ) );
}
} // namespace idhan::downloader
