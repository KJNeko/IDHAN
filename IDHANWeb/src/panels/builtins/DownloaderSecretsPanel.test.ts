import {describe, expect, it} from 'vitest';
import {canonicalSecrets, normalizeSecretDomain, parseSecretObject, validSecrets} from './DownloaderSecretsPanel';

describe('downloader secrets helpers', () => {
    it('compares maps independently of insertion order', () => {
        expect(canonicalSecrets({'e621.net': {second: 2, first: {enabled: true, label: 'example'}}}))
            .toBe(canonicalSecrets({'e621.net': {first: {label: 'example', enabled: true}, second: 2}}));
    });

    it('accepts domain objects containing arbitrary JSON', () => {
        expect(validSecrets({
            'e621.net': {
                credentials: {token: 'secret'},
                retries: 3,
                enabled: true,
                tags: ['a'],
                value: null
            }
        })).toBe(true);
        expect(validSecrets({'': {}})).toBe(false);
        expect(validSecrets({'e621.net': 'secret'})).toBe(false);
        expect(validSecrets({'https://e621.net': {}})).toBe(false);
        expect(validSecrets({'e621.net:443': {}})).toBe(false);
        expect(validSecrets(['secret'])).toBe(false);
    });

    it('canonicalizes domain names without guessing a parent domain', () => {
        expect(normalizeSecretDomain('EXAMPLE.co.uk.')).toBe('example.co.uk');
        expect(normalizeSecretDomain('sub.example.co.uk')).toBe('sub.example.co.uk');
        expect(normalizeSecretDomain('bücher.example')).toBe('xn--bcher-kva.example');
    });

    it('keeps incomplete edits out of saved JSON and accepts nested values', () => {
        expect(parseSecretObject('{"credentials":{"token":"a"},"enabled":true,"ids":[1,null]}'))
            .toEqual({credentials: {token: 'a'}, enabled: true, ids: [1, null]});
        for (const text of ['{', '[]', 'null', '42', '"string"']) {
            expect(() => parseSecretObject(text)).toThrow();
        }
    });
});
