import * as z from "zod";

export {z};

const BODY_EXCERPT = 512;

// Runs of whitespace, which an HTML error page is full of: "<html>\n  <body>" -> "<html> <body>"
const WHITESPACE_RUN = /\s+/g;

function headerValue(headers, name) {
    const entry = Object.entries(headers ?? {}).find(([key]) => key.toLowerCase() === name);

    return entry === undefined ? "" : entry[1];
}

export function excerpt(body) {
    if (typeof body !== "string") {
        return "";
    }

    const collapsed = body.trim().replace(WHITESPACE_RUN, " ");

    return collapsed.length > BODY_EXCERPT ? `${collapsed.slice(0, BODY_EXCERPT)}...` : collapsed;
}

export function parseResponse(schema, value, description) {
    const result = schema.safeParse(value);

    if (!result.success) {
        throw new Error(`${description} returned invalid data:\n${z.prettifyError(result.error)}`);
    }

    return result.data;
}

export function requireOk(response, description) {
    if (response.status >= 200 && response.status < 300) {
        return response;
    }

    const type = headerValue(response.headers, "content-type").split(";")[0].trim();
    const detail = [type, excerpt(response.body)].filter(Boolean).join(": ");

    throw new Error(`${description} returned HTTP ${response.status}${detail === "" ? "" : ` (${detail})`}`);
}

// Requests must ask for text: responseType "json" rejects on an error page before the status is seen.
export function parseJsonResponse(schema, response, description) {
    requireOk(response, description);

    const body = response.body.trim();

    if (body === "") {
        throw new Error(`${description} returned an empty body`);
    }

    let parsed;

    try {
        parsed = JSON.parse(body);
    } catch {
        throw new Error(`${description} returned an unparseable body: ${excerpt(body)}`);
    }

    return parseResponse(schema, parsed, description);
}
