import type {
	DirectFileImport,
	ImportHandle,
	DirectImportWriter,
	DomainContext,
	FileImport,
	MetadataValue,
	ParserKeyValueStore,
	RequestOptions,
	Session
} from "./session.js";
import {importDirect} from "./direct-import.js";
import type {ParserMetadata} from "./parser.js";
import {CompiledURLClassPattern} from "./url-class-pattern.js";
import type {ParameterDefinitions} from "./url-class.js";

export interface NativeHost {
	kv(operation: "get" | "set" | "delete", parser: string, key: string, value?: MetadataValue, lifetimeSeconds?: number): unknown;

	parserState(parser: string, origin: string, secrets: Readonly<Record<string, MetadataValue>>): object;

	secrets(): Readonly<Record<string, MetadataValue>>;

	setCookie(name: string, value: string): void;

	/** Gives a host found by discovery the request policy of its URL class. */
	learnHost(origin: string, urlClass: string): Promise<void> | void;

	request(options: RequestOptions): ReturnType<Session["request"]>;

	delay?(milliseconds: number): Promise<void>;

	queueImport(options: FileImport | { request: { url: string }; requireFile: boolean }): ImportHandle;

	importDirect(options: Readonly<Record<string, unknown>>, chunk?: Uint8Array): Promise<void>;

	follow(options: { url: string; tags?: Readonly<Record<string, readonly string[]>> }): unknown;
}

export interface Invocation {
	url: string;
	domain: string;
	operation: string;
	options: Readonly<Record<string, unknown>>;
	params?: Readonly<Record<string, string>>;
	urlClass?: string;
	discoveries?: readonly string[];
	parserOptions?: Readonly<Record<string, Readonly<Record<string, unknown>>>>;
}

export interface ParserConstructor {
	readonly metadata: ParserMetadata;

	new(site: DomainContext): object;
}

function basicAuthorization(username: string, password: string): string {
	const escaped = encodeURIComponent(username + ":" + password);
	const bytes: number[] = [];

	for (let index = 0; index < escaped.length; ++index) {
		if (escaped[index] === "%") {
			bytes.push(Number.parseInt(escaped.slice(index + 1, index + 3), 16));
			index += 2;
			continue;
		}

		bytes.push(escaped.charCodeAt(index));
	}

	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	let result = "";

	for (let index = 0; index < bytes.length; index += 3) {
		const a = bytes[index]!;
		const b = bytes[index + 1];
		const c = bytes[index + 2];
		const bits = (a << 16) | ((b ?? 0) << 8) | (c ?? 0);
		result += alphabet[(bits >>> 18) & 63]! + alphabet[(bits >>> 12) & 63]!;
		result += b === undefined ? "=" : alphabet[(bits >>> 6) & 63]!;
		result += c === undefined ? "=" : alphabet[bits & 63]!;
	}

	return "Basic " + result;
}

class NativeDomain implements DomainContext {
	authorization: string | undefined;
	readonly cookies = new Map<string, string>();

	constructor(
		readonly domain: string,
		readonly options: Readonly<Record<string, unknown>>,
		readonly secrets: Readonly<Record<string, MetadataValue>>,
		readonly kv: ParserKeyValueStore
	) {
	}

	async setCookie(name: string, value: string): Promise<void> {
		this.cookies.set(name, value);
	}

	async setBasicAuth(username: string, password: string): Promise<void> {
		this.authorization = basicAuthorization(username, password);
	}
}

class NativeSession implements Session {
	private readonly fetcher = globalThis.fetch.bind(globalThis);

	constructor(private readonly site: NativeDomain, private readonly host: NativeHost) {
	}

	get kv() {
		return this.site.kv;
	}

	get domain() {
		return this.site.domain;
	}

	get options() {
		return this.site.options;
	}

	get secrets() {
		return this.site.secrets;
	}

	async setCookie(name: string, value: string): Promise<void> {
		await this.site.setCookie(name, value);
		this.host.setCookie(name, value);
	}

	async setBasicAuth(username: string, password: string): Promise<void> {
		await this.site.setBasicAuth(username, password);
	}

	fetch(input: RequestInfo | URL, init?: RequestInit): Promise<Response> {
		if (this.site.authorization !== undefined) {
			const request = new Request(input, init);
			if (new URL(request.url).origin === this.domain) request.headers.set("Authorization", this.site.authorization);
			return this.fetcher(request);
		}
		return this.fetcher(input, init);
	}

	async delay(milliseconds: number): Promise<void> {
		if (this.host.delay === undefined) throw new Error("This host cannot delay execution");
		await this.host.delay(milliseconds);
	}

	request(options: RequestOptions): ReturnType<Session["request"]> {
		if (this.site.authorization !== undefined && new URL(options.url).origin === this.domain) {
			return this.host.request({
				...options,
				headers: {...options.headers, Authorization: this.site.authorization}
			});
		}

		return this.host.request(options);
	}

	async follow(options: { url: string; tags?: Readonly<Record<string, readonly string[]>> }): Promise<void> {
		await this.host.follow(options);
	}

	importDirect(options: DirectFileImport): Promise<DirectImportWriter> {
		return importDirect(this.host, options);
	}

	queueImport(options: FileImport): ImportHandle {
		return this.host.queueImport(options);
	}
}

interface ParserEntry {
	site: NativeDomain;
	instance: object;
	initialized?: Promise<void>;
}

interface ParserState {
	entry?: ParserEntry;
}

function parserEntry(Parser: ParserConstructor, input: Invocation, host: NativeHost): ParserEntry {
	const domain = new URL(input.domain).origin;
	const secrets = host.secrets();
	const state = host.parserState(Parser.metadata.id, domain, secrets) as ParserState;

	if (state.entry === undefined) {
		const parser = Parser.metadata.id;
		const kv: ParserKeyValueStore = {
			get<T extends MetadataValue>(key: string): T | undefined {
				return host.kv("get", parser, key) as T | undefined;
			},
			set(key, value, lifetimeSeconds) {
				host.kv("set", parser, key, value, lifetimeSeconds);
			},
			delete(key) {
				return host.kv("delete", parser, key) as boolean;
			}
		};
		const site = new NativeDomain(domain, input.options, secrets, kv);
		state.entry = {site, instance: new Parser(site)};
	}

	return state.entry;
}

async function configure(entry: ParserEntry, metadata: ParserMetadata): Promise<void> {
	for (const cookie of metadata.cookies ?? []) {
		await entry.site.setCookie(cookie.name, cookie.value);
	}

	if (typeof Reflect.get(entry.instance, "configure") === "function") {
		await invoke(entry.instance, "configure", undefined);
	}
}

function invocationSession(entry: ParserEntry, host: NativeHost): Session {
	for (const [name, value] of entry.site.cookies) {
		host.setCookie(name, value);
	}

	return new NativeSession(entry.site, host);
}

async function invoke(instance: object, method: string, input: unknown, session?: Session): Promise<unknown> {
	const handler: unknown = Reflect.get(instance, method);

	if (typeof handler !== "function") {
		throw new Error("Parser method is missing: " + method);
	}

	return Reflect.apply(handler, instance, [input, session]);
}

function validateParameters(values: Readonly<Record<string, string>>, definitions: ParameterDefinitions): Record<string, string> {
	const result: Record<string, string> = Object.create(null);

	for (const [name, definition] of Object.entries(definitions)) {
		const value = values[name] ?? definition.default;

		if (value === undefined) {
			if (!definition.optional) throw new Error("Missing generator parameter: " + name);

			continue;
		}

		if (typeof value !== "string") throw new Error("Generator parameters must be strings");

		// The parameter definition supplies the expression, e.g. ^[0-9]+$ for a post ID.
		if (definition.pattern !== undefined && !new RegExp(definition.pattern).test(value)) {
			throw new Error("Invalid generator parameter: " + name);
		}

		result[name] = value;
	}

	return result;
}

/** Native entry point; generator IDs remain independent of TypeScript method names. */
export function bindParser(Parser: ParserConstructor) {
	return async function run(input: Invocation, host: NativeHost): Promise<void> {
		const entry = parserEntry(Parser, input, host);
		const metadata = Parser.metadata;
		entry.initialized ??= configure(entry, metadata);
		await entry.initialized;
		const session = invocationSession(entry, host);
		const instance = entry.instance;

		if (input.params !== undefined) {
			const handler = metadata.handlers.find((entry) => entry.id === input.operation);

			if (handler === undefined) throw new Error("Unimplemented generator: " + input.operation);

			await invoke(instance, handler.method, {
				url: input.url, domain: input.domain,
				params: validateParameters(input.params, handler.parameters)
			}, session);
			return;
		}

		if (input.operation !== "__confirmed") {
			throw new Error("Generator invocation requires parameters: " + input.operation);
		}

		for (const urlClass of metadata.urlClasses) {
			if (input.urlClass !== undefined && input.urlClass !== urlClass.id) continue;

			for (const rule of urlClass.url_patterns) {
				for (const definition of rule.patterns) {
					const match = new CompiledURLClassPattern(definition).exec(input.url);

					if (match === null) {
						continue;
					}

					const generator = urlClass.generators.find((entry) => entry.id === rule.generator);
					const handler = metadata.handlers.find((entry) => entry.id === generator?.handler);

					if (handler === undefined) {
						throw new Error("URL class requires an unimplemented generator: " + rule.generator);
					}

					await invoke(instance, handler.method, {
						url: input.url, domain: input.domain,
						params: validateParameters(match.parameters, handler.parameters)
					}, session);
					return;
				}
			}
		}

		host.queueImport({request: {url: input.url}, requireFile: true});
	};
}

export async function probeParser(Parser: ParserConstructor, input: Invocation, host: NativeHost) {
	const options = input.parserOptions?.[Parser.metadata.id] ?? {};
	const entry = parserEntry(Parser, {...input, options}, host);
	const session = invocationSession(entry, host);
	const instance = entry.instance;

	for (const discovery of Parser.metadata.discoveries) {
		const result = await invoke(instance, discovery.method, {url: input.url}, session);

		if (result === null) continue;

		if (typeof result !== "object" || typeof Reflect.get(result, "origin") !== "string")
			throw new Error("Discovery must return an origin or null");

		const origin = new URL(Reflect.get(result, "origin") as string);
		const source = new URL(input.url);

		if (origin.hostname !== source.hostname) throw new Error("Discovery changed the site hostname");

		return {origin: origin.origin, urlClass: discovery.urlClass, options};
	}

	return null;
}
