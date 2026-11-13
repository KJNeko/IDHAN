import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import {transform} from 'esbuild';
import {encrypt} from 'megajs';

const source = fs.readFileSync(new URL('../premade/mega/decryption.ts', import.meta.url), 'utf8');
const {code} = await transform(source, {loader: 'ts', target: 'es2022', format: 'esm'});
const {MegaDecryption} = await import('data:text/javascript;base64,' + Buffer.from(code).toString('base64'));

async function encryptedFixture(plaintext) {
    const stream = encrypt(Buffer.alloc(24, 7));
    const chunks = [];
    const consume = (async () => {
        for await (const chunk of stream) chunks.push(chunk);
    })();
    stream.end(Buffer.from(plaintext));
    await consume;
    return {key: Uint8Array.from(stream.key), ciphertext: Buffer.concat(chunks)};
}

test('native MEGA decryption matches MEGAJS across MAC boundaries and arbitrary stream chunks', async () => {
    // Include empty input, partial AES blocks, exact MAC boundaries, and the 1 MiB MAC segment cap.
    for (const size of [0, 1, 15, 16, 17, 131072, 131073, 393216, 6 * 1024 * 1024 + 3]) {
        const plaintext = Buffer.alloc(size);
        for (let index = 0; index < size; ++index) plaintext[index] = index * 31 & 255;
        const fixture = await encryptedFixture(plaintext);
        const decryption = await MegaDecryption.create(fixture.key);
        const chunks = [];
        for (let offset = 0; offset < fixture.ciphertext.length; offset += 65521) {
            chunks.push(await decryption.decrypt(fixture.ciphertext.subarray(offset, offset + 65521)));
        }
        await decryption.verify();
        assert.deepEqual(Buffer.concat(chunks), plaintext, `size ${size}`);
    }
});

test('native MEGA decryption rejects corrupt ciphertext and MACs', async () => {
    const fixture = await encryptedFixture(Buffer.alloc(300000, 42));
    for (const corruptKey of [false, true]) {
        const key = fixture.key.slice();
        const ciphertext = Uint8Array.from(fixture.ciphertext);
        if (corruptKey) key[31] ^= 1;
        else ciphertext[0] ^= 1;
        const decryption = await MegaDecryption.create(key);
        await decryption.decrypt(ciphertext);
        await assert.rejects(decryption.verify(), {message: 'MAC verification failed'});
    }
    await assert.rejects(MegaDecryption.create(new Uint8Array(31)), {message: 'MEGA file key must contain 32 bytes'});
});
