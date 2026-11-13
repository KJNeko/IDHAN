export interface URLClassPatternDefinition extends Readonly<URLPatternInit> {
	/** Required query keys, with literal values or whole-value captures such as ':tags'. */
	readonly query?: Readonly<Record<string, string>>;
}

export interface ParameterDefinition {
	readonly name: string;
	readonly type: "string";
	readonly optional?: boolean;
	readonly pattern?: string;
	readonly default?: string;
}

export type ParameterDefinitions = Readonly<Record<string, ParameterDefinition>>;

/** Preserves required and optional parameters while deriving their value types. */
export type ParameterValues<Definitions extends ParameterDefinitions> = {
	[Name in keyof Definitions as Definitions[ Name ] extends {
		readonly optional: true
	} ? never : Name]: string;
} & {
	[Name in keyof Definitions as Definitions[ Name ] extends {
		readonly optional: true
	} ? Name : never]?: string;
};

export interface RequestRateDefinition {
	/** Space request starts evenly across the window instead of allowing a burst. Defaults to false. */
	readonly spread?: boolean;
	/** Maximum occupied transfers in this lane. Defaults to 1; an explicit lane.concurrency overrides it. */
	readonly concurrency?: number;
	readonly requests: number;
	readonly seconds: number;
	readonly shareSubdomains?: boolean;
}

export interface LaneDefinition {
	readonly concurrency?: number;
	/** HTTP error statuses that pause the lane. A 429 always backs off; repeated failures can disable the lane. */
	readonly backoffStatuses?: readonly number[];
	readonly keepAlive?: number;
	readonly errorBackoff?: number;
	readonly httpVersion?: "1.1" | "2" | "3";
	readonly group?: string;
}

export type HostDefinition = string | {
	/** A leading '*.' applies this host's lane settings to its subdomains too. */
	readonly host: string;
	readonly requestRate?: RequestRateDefinition | false;
	readonly lane?: LaneDefinition;
	readonly retry?: readonly {
		readonly code: number;
		readonly count: number;
		/** Seconds the whole lane pauses before each retry, for hosts that hide rate limits behind this status. */
		readonly wait?: number;
	}[];
	/**
	 * Host that stored and compared URLs on exactly this host use instead, e.g. "img4.gelbooru.com"
	 * on "img2.gelbooru.com". Only the host changes, and requests still go to the original URL.
	 * Not allowed on '*.' hosts.
	 */
	readonly pointsTo?: string;
};

/** Routing and lane settings for a site. A class without a parser only applies this policy. */
export interface URLClassPolicy {
	readonly id: string;
	readonly version: number;
	readonly hosts: readonly HostDefinition[];
	/** Recognized page URLs; without a matching parser these must not be inspected as files. */
	readonly url_patterns?: readonly {
		readonly name: string;
		readonly patterns: readonly URLClassPatternDefinition[];
		/** Query parameters deleted from URLs matching these patterns, e.g. "tags" on a booru post link. */
		readonly removeQuery?: readonly string[];
	}[];
	/** Sorts query parameters by name when URLs are normalized. Defaults to true. */
	readonly sortQueryParameters?: boolean;
	/** Classes without a request rate are limited to one request every five seconds. */
	readonly requestRate?: RequestRateDefinition | false;
	readonly removeQuery?: readonly string[];
	/**
	 * Query parameters kept for requests but stripped from stored and compared URLs, such as signed
	 * expiry tokens. Normalization keeps them aside, separately from the URL.
	 */
	readonly ephemeralQuery?: readonly string[];
}

export interface URLClassDefinition extends URLClassPolicy {
	readonly parser: string;
	readonly url_patterns: readonly {
		readonly name: string;
		readonly generator: string;
		readonly patterns: readonly URLClassPatternDefinition[];
		/** Query parameters deleted from URLs matching these patterns, e.g. "tags" on a booru post link. */
		readonly removeQuery?: readonly string[];
	}[];
	readonly generators: readonly {
		readonly id: string;
		readonly handler: string;
		readonly name: string;
		readonly params: ParameterDefinitions;
	}[];
}

type BoundGenerator<Parameters extends Record<string, ParameterDefinitions>> = {
	[Handler in keyof Parameters & string]: {
		readonly id: string;
		readonly name: string;
		readonly handler: Handler;
		readonly params: Parameters[ Handler ];
	};
}[ keyof Parameters & string ];

/** Links each bundled generator to the corresponding handler's parameter definitions. */
export type BoundURLClass<Parameters extends Record<string, ParameterDefinitions>> =
	Omit<URLClassDefinition, "generators"> & {
	readonly generators: readonly BoundGenerator<Parameters>[];
};
