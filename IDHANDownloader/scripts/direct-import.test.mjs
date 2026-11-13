import {test} from 'node:test';
import assert from 'node:assert/strict';
import {runInNewContext} from 'node:vm';
import {build} from 'esbuild';

const result = await build({
    entryPoints: ['premade/packages/sdk/direct-import.ts'],
    bundle: true,
    format: 'esm',
    write: false
});
const {importDirect} = await import('data:text/javascript;base64,' + Buffer.from(result.outputFiles[0].contents).toString('base64'));
const metadata = {filename: 'test.bin', urls: [], discoveredUrls: [], tags: {}, metadata: {}};

test('shared parsers can write byte views and buffers from another realm', async () => {
    const chunks = [];
    const writer = await importDirect({
        async importDirect(options, chunk) {
            if (options.operation === 'write') chunks.push(...chunk);
        }
    }, metadata);
    await writer.write(runInNewContext('new Uint8Array([0, 1, 2]).subarray(1)'));
    await writer.write(runInNewContext('new Uint8Array([3, 4]).buffer'));
    await writer.finish();
    assert.deepEqual(chunks, [1, 2, 3, 4]);
});

test('opening returns a writer without finalizing; whole buffers use bounded writes', async () => {
    const operations = [];
    const sizes = [];
    const writer = await importDirect({
        async importDirect(options, bytes) {
            operations.push(options.operation);
            if (bytes) sizes.push(bytes.byteLength);
        }
    }, metadata);
    assert.deepEqual(operations, ['open']);
    await writer.write(new Uint8Array(2 * 1024 * 1024 + 3).subarray(1));
    assert.deepEqual(sizes, [1024 * 1024, 1024 * 1024, 2]);
    await writer.finish();
    assert.deepEqual(operations, ['open', 'write', 'write', 'write', 'finish']);
    await assert.rejects(writer.write(new Uint8Array([1])), /closed/);
    await assert.rejects(writer.finish(), /closed/);
    await writer.abort();
    assert.equal(operations.at(-1), 'finish');
});

test('queued writes preserve order and finish waits for storage', async () => {
    let release;
    const operations = [];
    const writer = await importDirect({
        async importDirect(options, bytes) {
            operations.push(options.operation === 'write' ? bytes[0] : options.operation);
            if (options.operation === 'write' && bytes[0] === 1) await new Promise(resolve => {
                release = resolve;
            });
        }
    }, metadata);
    const first = writer.write(new Uint8Array([1]));
    const second = writer.write(new Uint8Array([2]).buffer);
    const finish = writer.finish();
    while (!release) await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(operations, ['open', 1]);
    release();
    await Promise.all([first, second, finish]);
    assert.deepEqual(operations, ['open', 1, 2, 'finish']);
});

test('aborting discards the managed file and prevents later writes', async () => {
    const operations = [];
    const writer = await importDirect({
        async importDirect(options) {
            operations.push(options.operation);
        }
    }, metadata);
    await writer.write(new Uint8Array([1]));
    await writer.abort();
    await writer.abort();
    await assert.rejects(writer.write(new Uint8Array([2])), /closed/);
    await assert.rejects(writer.finish(), /closed/);
    assert.deepEqual(operations, ['open', 'write', 'abort']);
});

test('storage failures abort automatically and prevent queued finalization', async () => {
    const operations = [];
    const writer = await importDirect({
        async importDirect(options) {
            operations.push(options.operation);
            if (options.operation === 'write') throw Error('disk full');
        }
    }, metadata);
    const write = writer.write(new Uint8Array([1]));
    const finish = writer.finish();
    await Promise.all([assert.rejects(write, /disk full/), assert.rejects(finish, /disk full/)]);
    await writer.abort();
    assert.deepEqual(operations, ['open', 'write', 'abort']);
});

test('failed finalization discards the file and preserves the storage error', async () => {
    const writer = await importDirect({
        async importDirect(options) {
            if (options.operation === 'finish') throw Error('finalization failed');
            if (options.operation === 'abort') throw Error('cleanup failed');
        }
    }, metadata);
    await assert.rejects(writer.finish(), /finalization failed/);
});
