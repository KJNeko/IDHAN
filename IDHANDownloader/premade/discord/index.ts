import type {URLClassPolicy} from "../packages/sdk/url-class.js";

export const urlClasses = [
	{
		id: "discord",
		version: 1,
		hosts: ["cdn.discordapp.com"],
		sortQueryParameters: true,
		requestRate: {requests: 1, seconds: 2, shareSubdomains: true},
		// Signed CDN links differ only by these expiry parameters.
		ephemeralQuery: ["ex", "hm"]
	}
] as const satisfies readonly URLClassPolicy[];
