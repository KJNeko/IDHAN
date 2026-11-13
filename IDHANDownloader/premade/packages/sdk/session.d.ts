export interface ImportOutcome {
	readonly existing: boolean;
}

export interface ImportHandle {
	/** Created lazily on first access. Resolves after persistence; repeated reads return the same promise. */
	readonly importFinished: Promise<ImportOutcome>;
}

export interface RequestOptions {
	url: string;
	responseType: "text";
	/** Return HTTP error responses to the caller, including a discovery probe's expected 404. */
	allowHttpErrors?: boolean;
	referer?: string;
	headers?: Readonly<Record<string, string>>;
}

export type MetadataValue = string | number | boolean | null
	| readonly MetadataValue[] | {
	readonly [name: string]: MetadataValue
};

export interface TextResponse {
	url: string;
	status: number;
	headers: Readonly<Record<string, string>>;
	body: string;
}

export interface FileImport {
	/** Source post publication time as integer Unix seconds; omit when the site supplies no date. */
	postedAt?: number | undefined;
	request: {
		url: string;
		referer: string
	};
	filename: string;
	urls: readonly {
		url: string;
		type: "post" | "file"
	}[];
	discoveredUrls: readonly string[];
	tags: Readonly<Record<string, readonly string[]>>;
	metadata: Readonly<Record<string, MetadataValue>>;
}

export type ImportBytes = Uint8Array | ArrayBuffer;

/** An IDHAN-managed temporary file. Unfinished writers are discarded on invocation teardown. */
export interface DirectImportWriter {
	/** Appends bytes in call order. Keep the supplied buffer unchanged until this promise resolves. */
	write(data: ImportBytes): Promise<void>;

	/** Finalizes the file and imports its record. Call only after producer integrity checks pass. */
	finish(): Promise<void>;

	/** Discards partial data. Repeated aborts are harmless. */
	abort(): Promise<void>;
}

export type DirectFileImport = Omit<FileImport, "request"> & {
	/** Provenance URL; defaults to the current parser URL. Never treated as a direct file URL. */
	url?: string;
};

/** In-memory JSON cache shared by this parser across site origins, sessions and instances. */
export interface ParserKeyValueStore {
	/** Returns undefined for a missing/expired key. A hit restarts its stored lifetime. */
	get<T extends MetadataValue = MetadataValue>(key: string): T | undefined;

	/** Stores a JSON copy. Lifetime is in whole seconds and defaults to one day. */
	set(key: string, value: MetadataValue, lifetimeSeconds?: number): void;

	delete(key: string): boolean;
}

export interface DomainContext {
	readonly kv: ParserKeyValueStore;
	/** Configured site origin. Its hostname defines the secret scope for this session. */
	readonly domain: string;
	/** Host-provided overrides. applyOptions validates these against the parser's decorators. */
	readonly options: Readonly<Record<string, unknown>>;
	/** The configured domain's JSON object, shared with its subdomains. Empty when absent. */
	readonly secrets: Readonly<Record<string, MetadataValue>>;

	setCookie(name: string, value: string): Promise<void>;

	setBasicAuth(username: string, password: string): Promise<void>;

}

/** Bound to one URL invocation, including after awaits. */
export interface Session extends DomainContext {
	request(options: RequestOptions): Promise<TextResponse>;

	/** Uses this invocation's host even when called by a retained parser method. */
	fetch(input: RequestInfo | URL, init?: RequestInit): Promise<Response>;

	delay(milliseconds: number): Promise<void>;

	/** Resolves once the source URL has been accepted for processing. */
	follow(options: {
		url: string;
		/** Added to descendant imports alongside the destination parser's tags. */
		tags?: Readonly<Record<string, readonly string[]>>
	}): Promise<void>;

	/** Queues immediately without a promise. Read the handle's importFinished property to wait for persistence. */
	queueImport(options: FileImport): ImportHandle;

	importDirect(options: DirectFileImport): Promise<DirectImportWriter>;
}
