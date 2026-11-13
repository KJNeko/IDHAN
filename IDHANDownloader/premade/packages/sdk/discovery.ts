import type {Invocation, NativeHost, ParserConstructor} from "./runtime.js";
import {bindParser, probeParser} from "./runtime.js";
import type {ParserMetadata} from "./parser.js";
import {SDK_VERSION} from "./version.js";

// Internal orchestration module. Discovery targets come from evaluated parser
// metadata; no site or parser IDs are embedded in the dispatcher.
export default class DiscoveryDispatcher {
	static readonly metadata: ParserMetadata = {
		formatVersion: 1,
		sdkVersion: SDK_VERSION,
		id: "idhan-discovery",
		version: 1,
		options: [],
		handlers: [],
		discoveries: [],
		urlClasses: []
	};
}

/**
 * A probe that cannot reach its site only means that parser does not claim the URL. The URL is
 * still imported, and a missing or unreachable file is reported by that request instead.
 */
async function probeQuietly(Parser: ParserConstructor, input: Invocation, host: NativeHost) {
	try {
		return await probeParser(Parser, input, host);
	} catch {
		return null;
	}
}

export async function run(input: Invocation, host: NativeHost): Promise<void> {
	for (const source of input.discoveries ?? []) {
		// Keep discovery targets dynamic when bundling this dispatcher independently.
		const specifier = "../../" + source;
		const module = await import(specifier);
		const Parser = module.default as ParserConstructor;
		const result = await probeQuietly(Parser, input, host);

		if (result === null) {
			continue;
		}

		await host.learnHost(result.origin, result.urlClass);

		await bindParser(Parser)({
			...input,
			domain: result.origin,
			operation: "__confirmed",
			options: result.options,
			urlClass: result.urlClass
		}, host);
		return;
	}

	host.queueImport({request: {url: input.url}, requireFile: true});
}
