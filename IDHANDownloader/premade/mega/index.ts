import './platform.js';

import {API, File, type APICallback} from 'megajs/dist/main.browser-es.mjs';
import {MegaDecryption} from './decryption.js';

import {applyOptions, generator, option, parser} from '../packages/sdk/decorators.js';
import type {GeneratorInput, ParserMetadata} from '../packages/sdk/parser.js';
import {bindParser} from '../packages/sdk/runtime.js';
import type {DirectImportWriter, DomainContext, Session} from '../packages/sdk/session.js';
import type {BoundURLClass, ParameterDefinitions} from '../packages/sdk/url-class.js';

const parameters = {
	shared: {url: {name: 'MEGA share URL', type: 'string', optional: true}}
} as const satisfies Record<string, ParameterDefinitions>;

export const urlClasses = [
	{
		id: 'mega',
		version: 1,
		parser: 'mega',
		hosts: [
			'mega.nz',
			'mega.co.nz',
			{
				host: '*.userstorage.mega.co.nz',
				requestRate: false,
				lane: {concurrency: 4}
			}
		],
		requestRate: false,
		url_patterns: [{
			name: 'Shared file or folder',
			generator: 'shared',
			patterns:
				[{pathname: '/file/:id', hash: '*'}, {pathname: '/folder/:id', hash: '*'}, {pathname: '/', hash: '*'}]
		}],
		generators: [{id: 'shared', handler: 'shared', name: 'Shared file or folder', params: parameters.shared}]
	}
] as const satisfies readonly BoundURLClass<typeof parameters>[];

interface DownloadOptions {
	connections: number;
	maxChunkSize: number;
}

interface DownloadLocation {
	url: string;
	size: number;
}

type ChunkRange = readonly [first: number, last: number];

const FIRST_CHUNK_SIZE = 128 * 1024;
const CHUNK_SIZE_STEP = 128 * 1024;
const CHUNK_ATTEMPTS = 5;

/** MEGAJS's global API and retry timers retain the realm of the first invocation. */
class SessionAPI extends API {
	constructor(private readonly session: Session) {
		super(false, {fetch: session.fetch.bind(session)});
	}

	override request(command: Record<string, unknown>, callback?: APICallback): Promise<unknown> {
		const result = this.requestWithRetry(command);
		if (callback === undefined) return result;

		return result.then(
			response => {
				callback(null, response);
				return response;
			},
			error => {
				callback(error instanceof Error ? error : new Error(String(error)));
			}
		);
	}

	private async requestWithRetry(command: Record<string, unknown>): Promise<unknown> {
		const request = {...command};
		for (let attempt = 1; ; ++attempt) {
			const retry: Record<string, unknown> = {...request, _querystring: command._querystring};
			try {
				// Disable library timers; delays must belong to the session making this request.
				return await super.request(retry, undefined, 4);
			} catch (error) {
				if (!(error instanceof Error) || !error.message.startsWith('EAGAIN (-3)') || attempt >= 5) throw error;

				// MEGAJS leaves a computed hashcash token on the command when a challenge needs a retry.
				if (typeof retry._hashcash === 'string') request._hashcash = retry._hashcash;
				await this.session.delay(1000 * 2 ** attempt);
			}
		}
	}
}

class MegaBandwidthLimit extends Error {
	constructor(secondsLeft: string | null) {
		super(`Bandwidth limit reached: ${secondsLeft ?? 'unknown'} seconds until it resets`);
	}
}

async function locateDownload(file: File): Promise<DownloadLocation> {
	const command: Record<string, unknown> = {a: 'g', g: 1, ssl: 2};
	if (file.nodeId) {
		command.n = file.nodeId;
	} else if (Array.isArray(file.downloadId)) {
		command._querystring = {n: file.downloadId[0]};
		command.n = file.downloadId[1];
	} else {
		command.p = file.downloadId;
	}

	const response = await file.api.request(command) as { g?: unknown; s?: unknown };
	if (typeof response.g !== 'string' || !response.g.startsWith('http'))
		throw new Error('MEGA servers returned an invalid download location, maybe caused by rate limit');

	if (typeof response.s !== 'number') throw new Error('MEGA servers returned no file size');

	return {url: response.g, size: response.s};
}

// Chunks grow by 128 KiB from 128 KiB up to maxChunkSize, as MEGAJS sizes them, so the first bytes arrive quickly.
function chunkRanges(size: number, maxChunkSize: number): ChunkRange[] {
	const ranges: ChunkRange[] = [];
	let chunkSize = Math.min(FIRST_CHUNK_SIZE, maxChunkSize);
	let offset = 0;
	while (offset < size) {
		const last = Math.min(size, offset + chunkSize) - 1;
		ranges.push([offset, last]);
		offset = last + 1;
		chunkSize = Math.min(chunkSize + CHUNK_SIZE_STEP, maxChunkSize);
	}
	return ranges;
}

async function fetchChunkOnce(file: File, location: DownloadLocation, [first, last]: ChunkRange): Promise<Uint8Array> {
	const response = await file.api.fetch(`${location.url}/${first}-${last}`);
	if (response.status === 509) throw new MegaBandwidthLimit(response.headers.get('x-mega-time-left'));

	if (response.status !== 200) throw new Error(`MEGA returned HTTP ${response.status} for bytes ${first}-${last}`);

	const bytes = new Uint8Array(await response.arrayBuffer());
	const expected = last - first + 1;
	if (bytes.length !== expected) throw new Error(`MEGA returned ${bytes.length} bytes for a ${expected} byte range`);

	return bytes;
}

async function fetchChunk(file: File, location: DownloadLocation, range: ChunkRange, session: Session): Promise<Uint8Array> {
	for (let attempt = 1; ; ++attempt) {
		try {
			return await fetchChunkOnce(file, location, range);
		} catch (error) {
			if (error instanceof MegaBandwidthLimit || attempt >= CHUNK_ATTEMPTS) throw error;

			await session.delay(1000 * 2 ** attempt);
		}
	}
}

function startChunk(file: File, location: DownloadLocation, range: ChunkRange, session: Session): Promise<Uint8Array> {
	const chunk = fetchChunk(file, location, range, session);
	// A failure surfaces when the chunk's turn to be written comes; until then it is not unhandled.
	chunk.catch(() => undefined);
	return chunk;
}

/**
 * Keeps up to `connections` chunks fetching or waiting to be written. Writing a chunk starts the next one,
 * so every slot is refilled no matter what order the chunks finish in.
 */
async function downloadChunks(
	file: File,
	location: DownloadLocation,
	options: DownloadOptions,
	decryption: MegaDecryption,
	writer: DirectImportWriter,
	session: Session
): Promise<void> {
	const ranges = chunkRanges(location.size, options.maxChunkSize);
	const window: Promise<Uint8Array>[] = [];
	let started = 0;
	while (started < ranges.length && window.length < options.connections)
		window.push(startChunk(file, location, ranges[started++]!, session));

	while (window.length > 0) {
		const ciphertext = await window.shift()!;
		if (started < ranges.length) window.push(startChunk(file, location, ranges[started++]!, session));

		await writer.write(await decryption.decrypt(ciphertext));
	}
}

async function importFile(file: File, source: string, path: string, session: Session, options: DownloadOptions): Promise<void> {
	if (!file.name) throw new Error('MEGA file has no decrypted filename; check the share key');
	if (!file.key) throw new Error('MEGA file has no decryption key');
	const decryption = await MegaDecryption.create(file.key);
	const location = await locateDownload(file);
	const writer = await session.importDirect({
		url: source,
		filename: file.name,
		urls: [{url: source, type: 'post'}],
		discoveredUrls: [],
		tags: {},
		metadata: {megaPath: path, megaSize: file.size}
	});
	try {
		await downloadChunks(file, location, options, decryption, writer, session);
		await decryption.verify();
		try {
			await writer.finish();
		} catch {
			// The host reports import failures (including unknown MIME types), and the writer discards
			// the rejected file. Keep processing the remaining folder entries.
		}
	} catch (error) {
		try {
			await writer.abort();
		} catch {
			// Preserve the original download or storage failure.
		}
		throw error;
	}
}

async function importTree(file: File, source: string, path: string, session: Session, options: DownloadOptions): Promise<void> {
	if (!file.directory) {
		await importFile(file, source, path, session, options);
		return;
	}
	for (const child of file.children ?? []) {
		const childPath = path ? `${path}/${child.name ?? ''}` : child.name ?? '';
		const nodeId = child.nodeId ?? (Array.isArray(child.downloadId) ? child.downloadId[1] : undefined);
		if (!nodeId) throw new Error('MEGA folder entry has no node ID');
		const childUrl = new URL(source);
		childUrl.hash = `${childUrl.hash.slice(1).split('/file/')[0]}/file/${nodeId}`;
		await importTree(child, childUrl.href, childPath, session, options);
	}
}

@parser({id: 'mega', version: 1, urlClasses})
export default class Mega {
	declare static readonly metadata: ParserMetadata;

	@option({
		type: 'number',
		default: 10,
		minimum: 1,
		integer: true,
		description: 'Chunks fetched or waiting to be written per file. IDHAN also limits host concurrency.'
	})
	accessor maxConnections = 10;

	@option({
		type: 'number',
		default: 1024 * 1024 * 4,
		minimum: 1,
		integer: true,
		description: 'Maximum download chunk size in bytes.'
	})
	accessor maxChunkSize = 1024 * 1024 * 4;

	constructor(site: DomainContext) {
		applyOptions(this, site.options);
	}

	@generator('shared', parameters.shared)
	async shared(input: GeneratorInput<{ url?: string }>, session: Session): Promise<void> {
		const url = input.params.url || input.url;
		if (!url) throw new Error('A MEGA share URL including its decryption key is required');
		const file = File.fromURL(url, {api: new SessionAPI(session)});
		// Folder links selecting a child return that child rather than mutating the root object.
		const loaded = await file.loadAttributes();
		const source = file.directory && loaded === file ? await file.link({}) : url;
		await importTree(loaded, source, loaded.name ?? '', session, {
			connections: this.maxConnections,
			maxChunkSize: this.maxChunkSize
		});
	}
}

export const run = bindParser(Mega);
