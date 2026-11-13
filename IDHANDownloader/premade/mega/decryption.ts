/** Incremental MEGA file decryption using the host's native WebCrypto AES. */
export class MegaDecryption {
	private readonly buffer = new Uint8Array(1024 * 1024);
	private used = 0;
	private segmentSize = 128 * 1024;
	private offset = 0;
	private condensed = new Uint8Array(16);

	private constructor(
		private readonly ctrKey: CryptoKey,
		private readonly macKey: CryptoKey,
		private readonly counter: Uint8Array<ArrayBuffer>,
		private readonly macIV: Uint8Array<ArrayBuffer>,
		private readonly expected: Uint8Array<ArrayBuffer>
	) {
	}

	static async create(key: Uint8Array): Promise<MegaDecryption> {
		if (key.length !== 32) throw new Error('MEGA file key must contain 32 bytes');
		// MEGA merges the AES key with the nonce and expected MAC in its 32-byte file key.
		const raw = Uint8Array.from(key.subarray(0, 16), (value, index) => value ^ key[index + 16]!);
		const ctrKey = await crypto.subtle.importKey('raw', raw, 'AES-CTR', false, ['decrypt']);
		const macKey = await crypto.subtle.importKey('raw', raw, 'AES-CBC', false, ['encrypt']);
		const counter = new Uint8Array(16);
		counter.set(key.subarray(16, 24));
		const macIV = new Uint8Array(16);
		macIV.set(counter.subarray(0, 8));
		macIV.set(counter.subarray(0, 8), 8);
		return new MegaDecryption(ctrKey, macKey, counter, macIV, key.slice(24));
	}

	private async fold(mac: Uint8Array<ArrayBuffer>): Promise<void> {
		const encrypted = await crypto.subtle.encrypt({name: 'AES-CBC', iv: this.condensed}, this.macKey, mac);
		this.condensed = new Uint8Array(encrypted.slice(0, 16));
	}

	private async segment(): Promise<void> {
		if (this.used === 0) {
			// An exact MAC boundary still leaves a nonce-initialized MAC to fold at EOF.
			await this.fold(this.macIV);
			return;
		}

		// MEGA zero-pads the final block; WebCrypto adds its own PKCS#7 block.
		const padded = new Uint8Array(Math.ceil(this.used / 16) * 16);
		padded.set(this.buffer.subarray(0, this.used));
		const encrypted = await crypto.subtle.encrypt({name: 'AES-CBC', iv: this.macIV}, this.macKey, padded);
		await this.fold(new Uint8Array(encrypted.slice(-32, -16)));
		this.used = 0;
	}

	async decrypt(ciphertext: Uint8Array): Promise<Uint8Array<ArrayBuffer>> {
		// The library's stream may combine or split network chunks at arbitrary byte offsets.
		const skip = this.offset % 16;
		const input = new Uint8Array(skip + ciphertext.length);
		input.set(ciphertext, skip);
		const counter = this.counter.slice();
		new DataView(counter.buffer).setBigUint64(8, BigInt(Math.floor(this.offset / 16)), false);
		const decrypted = await crypto.subtle.decrypt({name: 'AES-CTR', counter, length: 64}, this.ctrKey, input);
		const plaintext = new Uint8Array(decrypted, skip);
		this.offset += plaintext.length;
		let position = 0;
		while (position < plaintext.length) {
			const size = Math.min(this.segmentSize - this.used, plaintext.length - position);
			this.buffer.set(plaintext.subarray(position, position + size), this.used);
			this.used += size;
			position += size;
			if (this.used === this.segmentSize) {
				await this.segment();
				this.segmentSize = Math.min(this.segmentSize + 128 * 1024, this.buffer.length);
			}
		}
		return plaintext;
	}

	async verify(): Promise<void> {
		await this.segment();
		const mac = new Uint8Array(8);
		for (let index = 0; index < 4; ++index) {
			mac[index] = this.condensed[index]! ^ this.condensed[index + 4]!;
			mac[index + 4] = this.condensed[index + 8]! ^ this.condensed[index + 12]!;
		}
		let difference = 0;
		for (let index = 0; index < mac.length; ++index) difference |= mac[index]! ^ this.expected[index]!;
		if (difference !== 0) throw new Error('MAC verification failed');
	}
}
