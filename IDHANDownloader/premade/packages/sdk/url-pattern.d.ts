export {};

// QuickJS receives this global from IDHAN before evaluating a parser module.
// These declarations describe the injected API; they emit no JavaScript.
declare global {
	interface URLPatternInit {
		baseURL?: string;
		protocol?: string;
		username?: string;
		password?: string;
		hostname?: string;
		port?: string;
		pathname?: string;
		search?: string;
		hash?: string;
	}

	interface URLPatternOptions {
		ignoreCase?: boolean;
	}

	type URLPatternInput = string | URLPatternInit;

	interface URLPatternComponentResult {
		input: string;
		groups: Record<string, string | undefined>;
	}

	interface URLPatternResult {
		inputs: [URLPatternInput] | [URLPatternInput, string];
		protocol: URLPatternComponentResult;
		username: URLPatternComponentResult;
		password: URLPatternComponentResult;
		hostname: URLPatternComponentResult;
		port: URLPatternComponentResult;
		pathname: URLPatternComponentResult;
		search: URLPatternComponentResult;
		hash: URLPatternComponentResult;
	}

	class URLPattern {
		readonly protocol: string;
		readonly username: string;
		readonly password: string;
		readonly hostname: string;
		readonly port: string;
		readonly pathname: string;
		readonly search: string;
		readonly hash: string;
		readonly hasRegExpGroups: boolean;

		constructor(input?: URLPatternInput, options?: URLPatternOptions);

		constructor(input: URLPatternInput, baseURL: string, options?: URLPatternOptions);

		test(input?: URLPatternInput, baseURL?: string): boolean;

		exec(input?: URLPatternInput, baseURL?: string): URLPatternResult | null;
	}
}
