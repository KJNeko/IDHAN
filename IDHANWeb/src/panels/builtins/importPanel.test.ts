import {describe, expect, it} from 'vitest';
import {parseNewlineUrlList} from './ImportPanel';

describe('parseNewlineUrlList', () => {
    it('keeps a single URL', () => {
        expect(parseNewlineUrlList('https://example.test/one')).toEqual(['https://example.test/one']);
    });

    it('splits mixed LF and CRLF clipboard content', () => {
        expect(parseNewlineUrlList('https://example.test/one\r\nhttps://example.test/two\nhttps://example.test/three')).toEqual([
            'https://example.test/one',
            'https://example.test/two',
            'https://example.test/three',
        ]);
    });

    it('trims lines, discards blanks, and preserves order and duplicates', () => {
        expect(parseNewlineUrlList('  https://example.test/one  \n \t\r\nhttps://example.test/two\nhttps://example.test/one\n')).toEqual([
            'https://example.test/one',
            'https://example.test/two',
            'https://example.test/one',
        ]);
    });
});
