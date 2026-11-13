import type {RequestOptions, Session, TextResponse} from "./session.js";

export type Decoder<T> = (value: unknown, label: string) => T;

export function object(value: unknown, label: string): Record<string, unknown> {
	if (typeof value !== "object" || value === null || Array.isArray(value)) {
		throw new Error(`${label} must be an object`);
	}

	return value as Record<string, unknown>;
}

export function string(value: unknown, label: string): string {
	if (typeof value !== "string") {
		throw new Error(`${label} must be a string`);
	}

	return value;
}

export function nonempty(value: unknown, label: string): string {
	const result = string(value, label);

	if (result.length === 0) {
		throw new Error(`${label} must not be empty`);
	}

	return result;
}

/** Report absent credentials without exposing a script stack trace to the user. */
export function requiredSecrets(secrets: Record<string, unknown>, names: readonly string[]): void {
	const missing = names.filter((name) => secrets[name] == null || secrets[name] === "");
	if (missing.length > 0) {
		throw `Missing downloader secrets: ${missing.join(", ")}. Add them for this domain in Downloader Secrets.`;
	}
}

export function number(value: unknown, label: string): number {
	if (typeof value !== "number" || !Number.isFinite(value)) {
		throw new Error(`${label} must be a finite number`);
	}

	return value;
}

export function boolean(value: unknown, label: string): boolean {
	if (typeof value !== "boolean") {
		throw new Error(`${label} must be a boolean`);
	}

	return value;
}

export function id(value: unknown, label: string): string {
	if (typeof value === "number") {
		if (!Number.isSafeInteger(value) || value < 0) {
			throw new Error(`${label} must be a safe integer ID`);
		}

		return String(value);
	}

	return nonempty(value, label);
}

export function decimalId(value: unknown, label: string): string {
	const result = id(value, label);

	// Matches decimal IDs, e.g. 123 in https://e621.net/posts/123.
	if (!/^\d+$/.test(result)) {
		throw new Error(`${label} must be a decimal ID`);
	}

	return result;
}

export function array<T>(
	value: unknown,
	decode: Decoder<T>,
	label: string
): T[] {
	if (!Array.isArray(value)) {
		throw new Error(`${label} must be an array`);
	}

	return value.map((entry: unknown, index: number) => decode(entry, `${label}[${index}]`));
}

export function nullish<T>(
	value: unknown,
	decode: Decoder<T>,
	label: string
): T | null {
	return value == null ? null : decode(value, label);
}

export function numeric(value: unknown, label: string): number {
	return number(typeof value === "string" ? Number(value) : value, label);
}

export function stringMap(value: unknown, label: string): Record<string, string[]> {
	return Object.fromEntries(
		Object.entries(object(value, label))
		.map(
			([name, entries]) => [
				name,
				array(entries, string, `${label}.${name}`)
			]
		)
	);
}

/** JSON dates arrive as strings; generated clients previously converted them to Date objects. */
export function date(value: unknown, label: string): string {
	const input = string(value, label);
	const timestamp = Date.parse(input);

	if (!Number.isFinite(timestamp)) {
		throw new Error(`${label} must be a valid date`);
	}

	return new Date(timestamp).toISOString();
}

export function filename(url: URL): string {
	return url.pathname.slice(url.pathname.lastIndexOf("/") + 1);
}

/** Keep status and parsing failures visible before decoding the untrusted API payload. */
export function parseJson<T>(
	response: TextResponse,
	decode: Decoder<T>,
	description: string
): T {
	if (response.status < 200 || response.status >= 300) {
		throw new Error(`${description} returned HTTP ${response.status}`);
	}


	if (response.body.trim() === "") {
		throw new Error(`${description} returned an empty body`);
	}

	let body: unknown;

	try {
		body = JSON.parse(response.body);
	} catch {
		throw new Error(`${description} returned invalid JSON`);
	}

	return decode(body, description);
}

export async function requestJson<T>(
	session: Session,
	options: RequestOptions,
	decode: Decoder<T>,
	description: string,
): Promise<T> {
	return parseJson(await session.request(options), decode, description);
}

/** Converts a source publication date to the import contract's Unix seconds. */
export function postedAt(value: unknown, label: string): number | undefined {
	if (value == null) return undefined;

	return Math.floor(Date.parse(date(value, label)) / 1000);
}
