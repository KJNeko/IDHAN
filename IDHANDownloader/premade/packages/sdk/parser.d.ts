import type {ParameterDefinitions, URLClassDefinition} from "./url-class.js";

/** Declared by the author; the decorated accessor must have the matching type and initializer. */
export type OptionDefinition =
	{
		readonly description: string
	}
	& (
	{
		readonly type: "boolean";
		readonly default: boolean
	}
	| {
	readonly type: "number";
	readonly default: number;
	readonly minimum?: number;
	readonly integer?: boolean
}
	| {
	readonly type: "string";
	readonly default: string
}
	| {
	readonly type: "string[]";
	readonly default: readonly string[]
}
	);

export type OptionValue = boolean | number | string | readonly string[];

export type OptionMetadata = OptionDefinition & {
	readonly name: string
};

export interface GeneratorMetadata {
	/** Stable operation ID referenced by URL classes. */
	readonly id: string;
	/** Public prototype method to invoke on the parser instance. */
	readonly method: string;
	readonly parameters: ParameterDefinitions;
}

export interface DiscoveryInput {
	/** Original absolute URL that no ordinary URL-class pattern accepted. */
	readonly url: string;
}

export interface DiscoveryResult {
	/** Origin to associate with the hook's URL class before retrying the original URL. */
	readonly origin: string;
}

export interface DiscoveryMetadata {
	readonly urlClass: string;
	readonly method: string;
}

/**
 * @parser publishes this object as the default-exported class's static `metadata` property.
 * QuickJS reads module.default.metadata and JSON-serializes that object for IDHAN.
 * Discovery requires module evaluation but never constructs a parser or a Session.
 */
export interface ParserMetadata {
	readonly formatVersion: 1;
	/** SDK release used to build this parser. */
	readonly sdkVersion: string;
	readonly id: string;
	readonly version: number;
	readonly options: readonly OptionMetadata[];
	readonly handlers: readonly GeneratorMetadata[];
	/** Invoked for unrecognized URLs before automatic file inspection, not as generators. */
	readonly discoveries: readonly DiscoveryMetadata[];
	/** These cookies apply to the session's current host. */
	readonly cookies?: readonly {
		readonly name: string;
		readonly value: string
	}[];
	/** Bundled definitions; independent URL classes may also bind to this parser. */
	readonly urlClasses: readonly URLClassDefinition[];
}

export interface GeneratorInput<Params> {
	/** Absolute origin, including scheme and any non-default port. */
	domain: string;
	params: Params;
	/** Absent for a direct generator invocation from a client UI. */
	url?: string;
}
