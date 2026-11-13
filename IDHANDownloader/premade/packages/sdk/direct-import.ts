import type {DirectFileImport, DirectImportWriter, ImportBytes} from "./session.js";

interface DirectImportHost {
	importDirect(options: Readonly<Record<string, unknown>>, chunk?: Uint8Array): Promise<void>;
}

let nextImport = 0;
const chunkSize = 1024 * 1024;

function bytes(value: ImportBytes): Uint8Array {
	// A shared parser's producer and the invocation's writer may belong to different realms.
	const kind = Object.prototype.toString.call(value);
	if (ArrayBuffer.isView(value) && kind === "[object Uint8Array]")
		return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
	if (kind === "[object ArrayBuffer]") return new Uint8Array(value);
	throw new TypeError("Direct imports require Uint8Array or ArrayBuffer chunks");
}

class ImportWriter implements DirectImportWriter {
	private tail = Promise.resolve();
	private accepting = true;
	private completed = false;
	private aborted = false;
	private failed = false;
	private failure: unknown;

	constructor(private readonly host: DirectImportHost, private readonly id: number) {
	}

	write(data: ImportBytes): Promise<void> {
		if (!this.accepting) return Promise.reject(new Error("Direct import writer is closed"));
		return this.enqueue(async () => {
			const value = bytes(data);
			for (let offset = 0; offset < value.byteLength; offset += chunkSize) {
				await this.host.importDirect({
					id: this.id,
					operation: "write"
				}, value.subarray(offset, offset + chunkSize));
			}
		});
	}

	finish(): Promise<void> {
		if (!this.accepting) return Promise.reject(new Error("Direct import writer is closed"));
		this.accepting = false;
		return this.enqueue(async () => {
			await this.host.importDirect({id: this.id, operation: "finish"});
			this.completed = true;
		});
	}

	abort(): Promise<void> {
		this.accepting = false;
		const result = this.tail.then(() => this.discard());
		this.tail = result.catch(() => {
		});
		return result;
	}

	private async discard(): Promise<void> {
		if (this.completed || this.aborted) return;
		await this.host.importDirect({id: this.id, operation: "abort"});
		this.aborted = true;
	}

	private enqueue(operation: () => Promise<void>): Promise<void> {
		const result = this.tail.then(async () => {
			if (this.failed) throw this.failure;
			try {
				await operation();
			} catch (error) {
				this.failed = true;
				this.accepting = false;
				this.failure = error;
				try {
					await this.discard();
				} catch {
					// Preserve the write/finalization error; session teardown also discards unfinished files.
				}
				throw error;
			}
		});
		this.tail = result.catch(() => {
		});
		return result;
	}
}

/** Opens an IDHAN-owned temporary file; only finish() publishes the completed import. */
export async function importDirect(host: DirectImportHost, options: DirectFileImport): Promise<DirectImportWriter> {
	const id = ++nextImport;
	await host.importDirect({...options, id, operation: "open"});
	return new ImportWriter(host, id);
}
