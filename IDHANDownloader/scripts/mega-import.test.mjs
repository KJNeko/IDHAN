import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import {build} from 'esbuild';

const bundle = await build({
    entryPoints: ['premade/mega/index.ts'],
    bundle: true,
    platform: 'browser',
    target: 'es2022',
    format: 'esm',
    write: false
});
const moduleUrl = 'data:text/javascript;base64,' + Buffer.from(bundle.outputFiles[0].contents).toString('base64');
const fixture = JSON.parse(fs.readFileSync(new URL('../../tests/downloader/fixtures/mega-download.json', import.meta.url)));

for (const scenario of ['folder', 'single file', 'corrupt file', 'storage failure']) {
    test(`MEGA import rejection: ${scenario}`, async t => {
        // Each MEGAJS API instance captures fetch on first use; keep it local to this scenario.
        const parser = await import(moduleUrl + '#' + encodeURIComponent(scenario));
        const folder = structuredClone(fixture.folderResponse);
        folder.f.push({...folder.f[1], h: 'second'});
        const isFolder = scenario === 'folder';
        const url = isFolder ? `https://mega.nz/folder/example#${fixture.folderKey}`
            : `https://mega.nz/file/example#${fixture.key}`;
        t.mock.method(globalThis, 'fetch', async (url, options) => {
            if (url.startsWith('https://g.api.mega.co.nz/')) {
                const command = JSON.parse(options.body)[0];
                if (command.a === 'f') return new Response(JSON.stringify([folder]));
                assert.equal(command.a, 'g');
                return new Response(JSON.stringify([{
                    at: fixture.at,
                    s: fixture.bytes.length,
                    g: 'https://download.test/file'
                }]));
            }
            // MEGA ranged download URL, for example https://download.test/file/0-26.
            const range = /^https:\/\/download\.test\/file\/(\d+)-(\d+)$/.exec(url);
            assert.ok(range);
            const bytes = new Uint8Array(fixture.bytes);
            if (scenario === 'corrupt file') bytes[0] ^= 1;
            return new Response(bytes.slice(Number(range[1]), Number(range[2]) + 1));
        });
        const state = {};
        const imports = new Map();
        const operations = [];
        const stored = [];
        const failures = [];
        const host = {
            secrets: () => ({}),
            parserState: () => state,
            async importDirect(options, chunk) {
                const {id, operation} = options;
                operations.push(operation);
                if (operation === 'open') imports.set(id, {url: options.url, chunks: []});
                else if (operation === 'write') imports.get(id).chunks.push(Buffer.from(chunk));
                else if (operation === 'abort') imports.delete(id);
                else if (operation === 'finish') {
                    const file = imports.get(id);
                    if (failures.length === 0) {
                        const error = scenario === 'storage failure' ? 'disk full'
                            : `Downloaded file has an unrecognised MIME type: ${file.url}`;
                        // The native host records the file's import failure before rejecting finish.
                        failures.push({url: file.url, error});
                        throw new Error(error);
                    }
                    stored.push(Buffer.concat(file.chunks).toString());
                    imports.delete(id);
                } else assert.fail(`Unexpected direct import operation: ${operation}`);
            }
        };
        const run = parser.run({url, domain: 'https://mega.nz', operation: 'shared', params: {url}, options: {}}, host);
        if (scenario === 'corrupt file') {
            await assert.rejects(run, /MAC verification failed/);
            assert.deepEqual(failures, []);
            assert.ok(!operations.includes('finish'));
        } else {
            await run;
            assert.equal(failures.length, 1);
            if (isFolder) {
                assert.equal(new URL(failures[0].url).pathname, '/folder/example');
                assert.ok(new URL(failures[0].url).hash.endsWith('/file/child'));
            } else assert.equal(failures[0].url, url);
        }
        assert.deepEqual(stored, isFolder ? [fixture.text] : []);
        assert.equal(operations.filter(operation => operation === 'abort').length, 1);
        assert.equal(imports.size, 0);
    });
}
