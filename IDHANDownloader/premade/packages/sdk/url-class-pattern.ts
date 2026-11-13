import type {URLClassPatternDefinition} from "./url-class.js";

export interface URLClassPatternMatch {
	readonly match: URLPatternResult;
	readonly parameters: Readonly<Record<string, string>>;
}

interface QueryRequirement {
	readonly key: string;
	readonly value: string;
	readonly capture: string | undefined;
}

// This is a compiled matcher for a declarative pattern, not a URL-class definition.
// Host selection and ordered generator selection belong to the URL-class router.
export class CompiledURLClassPattern {
	readonly pattern: URLPattern;

	private readonly query: readonly QueryRequirement[];

	constructor(definition: URLClassPatternDefinition) {
		const {query, ...init} = definition;

		this.pattern = new URLPattern(init);
		this.query = Object.entries(query ?? {}).map(compileQueryRequirement);
	}

	exec(input: string | URL): URLClassPatternMatch | null {
		let url: URL;

		try {
			url = new URL(String(input));
		} catch {
			return null;
		}

		const match = this.pattern.exec(url.href);

		if (match === null) {
			return null;
		}

		const parameters: Record<string, string> = Object.create(null);

		const components = [
			match.protocol, match.username, match.password, match.hostname,
			match.port, match.pathname, match.search, match.hash
		];

		for (const component of components) {
			for (const [name, value] of Object.entries(component.groups)) {
				// Skip positional wildcard/regexp groups, e.g. '0' for https://example.com/*.
				if (value === undefined || /^\d+$/u.test(name)) {
					continue;
				}

				let decoded: string;

				try {
					decoded = decodeURIComponent(value);
				} catch {
					return null;
				}

				if (!addCapture(parameters, name, decoded)) {
					return null;
				}
			}
		}

		for (const requirement of this.query) {
			const values = url.searchParams.getAll(requirement.key);

			// Reject repeated constrained keys instead of choosing an arbitrary value.
			if (values.length !== 1) {
				return null;
			}

			const value = values[0]!;

			if (requirement.capture === undefined) {
				if (value !== requirement.value) {
					return null;
				}

				continue;
			}

			if (!addCapture(parameters, requirement.capture, value)) {
				return null;
			}
		}

		return {match, parameters: Object.freeze(parameters)};
	}
}

function compileQueryRequirement([key, value]: [string, string]): QueryRequirement {
	// A whole query capture, e.g. ':tags' for https://gelbooru.com/index.php?tags=cat.
	const capture = /^:([$\p{ID_Start}_][$\p{ID_Continue}\u200C\u200D_]*)$/u.exec(value);

	return {key, value, capture: capture?.[1]};
}

function addCapture(parameters: Record<string, string>, name: string, value: string): boolean {
	if (Object.hasOwn(parameters, name) && parameters[name] !== value) {
		return false;
	}

	parameters[name] = value;
	return true;
}
