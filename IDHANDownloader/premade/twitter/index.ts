import type {URLClassPolicy} from "../packages/sdk/url-class.js";

export const urlClasses = [
	{
		id: "twitter",
		version: 1,
		hosts: ["x.com", "twitter.com"],
		sortQueryParameters: true,
		url_patterns: [
			{
				name: "Status page",
				patterns: [
					// status ID, e.g. https://x.com/example/status/123.
					{pathname: "/:user/status/:id(\\d+)"},
					// Media, e.g. https://x.com/example/status/123/photo/1.
					{pathname: "/:user/status/:id(\\d+)/:rest*"},
				]
			}
		],
	}
] as const satisfies readonly URLClassPolicy[];
