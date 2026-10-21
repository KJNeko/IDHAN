import {describe, expect, it} from 'vitest';
import {canonicalSecrets, validSecrets} from './DownloaderSecretsPanel';

describe('downloader secrets helpers', () => {
    it('compares maps independently of insertion order', () => {
        expect(canonicalSecrets({second: '2', first: '1'})).toBe(canonicalSecrets({first: '1', second: '2'}));
    });

    it('accepts only non-empty keys with string values', () => {
        expect(validSecrets({'site.token': 'secret'})).toBe(true);
        expect(validSecrets({'': 'secret'})).toBe(false);
        expect(validSecrets({'site.token': 3})).toBe(false);
        expect(validSecrets(['secret'])).toBe(false);
    });
});
