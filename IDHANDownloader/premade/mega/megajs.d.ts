// The published browser entry has no declaration file; these are its APIs used by this parser.
declare module "megajs/dist/main.browser-es.mjs" {
	export type APICallback = (error: Error | null, response?: unknown) => void;

	export class API {
		constructor(keepalive?: boolean, options?: { fetch?: typeof globalThis.fetch });

		request(command: Record<string, unknown>, callback?: APICallback, retry?: number): Promise<unknown>;

		fetch(url: string, options?: RequestInit): Promise<Response>;
	}

	export class File {
		directory: boolean;
		nodeId?: string;
		downloadId: string | [string, string];
		name: string | null;
		key?: Uint8Array;
		size: number;
		children?: File[];
		api: {
			/** Sends one command to the MEGA API, retrying its EAGAIN (-3) responses. */
			request(command: Record<string, unknown>): Promise<unknown>;
			/** The fetch MEGAJS uses for transfers, with its user agent applied. */
			fetch(url: string): Promise<Response>;
		};

		static fromURL(url: string, options?: { api?: API }): File;

		loadAttributes(): Promise<File>;

		link(options: { noKey?: boolean }): Promise<string>;
	}
}
