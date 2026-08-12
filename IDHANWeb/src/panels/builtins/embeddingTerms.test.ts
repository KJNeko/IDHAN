import {describe, expect, it} from 'vitest';
import {parseTermInput, termsToRequest, termsToTokens, type Term} from './embeddingTerms';

describe('parseTermInput', () => {
    it('takes a bare phrase at the default weight', () => {
        expect(parseTermInput('catgirl')).toEqual({
            kind: 'text',
            text: 'catgirl',
            weight: 1,
            positive: true,
            enabled: true,
        });
    });

    it('reads a trailing numeric suffix as the weight', () => {
        expect(parseTermInput('catgirl:0.5')).toMatchObject({kind: 'text', text: 'catgirl', weight: 0.5});
    });

    it('keeps colons that are part of the phrase', () => {
        expect(parseTermInput('rating:safe')).toMatchObject({kind: 'text', text: 'rating:safe', weight: 1});
        expect(parseTermInput('character:hatsune miku:0.8')).toMatchObject({
            kind: 'text',
            text: 'character:hatsune miku',
            weight: 0.8,
        });
    });

    it('makes a leading dash negative', () => {
        expect(parseTermInput('-blurry')).toMatchObject({kind: 'text', text: 'blurry', positive: false});
    });

    it('rejects input with no phrase left in it', () => {
        expect(parseTermInput('')).toBeNull();
        expect(parseTermInput('   ')).toBeNull();
        expect(parseTermInput('-')).toBeNull();
    });

    it('does not let a trailing colon zero the weight', () => {
        expect(parseTermInput('catgirl:')).toMatchObject({kind: 'text', text: 'catgirl:', weight: 1});
    });

    // The record forms are what the manual-id entry rides on; the whole-string match has to win over
    // the weight suffix or `record:1234` would become the phrase `record` at weight 1234.
    it('reads record:<id> as a reference, not a weighted phrase', () => {
        expect(parseTermInput('record:1234')).toEqual({
            kind: 'record',
            recordId: 1234,
            weight: 1,
            positive: true,
            enabled: true,
        });
    });

    it('accepts the #<id> shorthand', () => {
        expect(parseTermInput('#1234')).toMatchObject({kind: 'record', recordId: 1234, weight: 1});
    });

    it('weights and negates a reference like any other term', () => {
        expect(parseTermInput('record:1234:0.5')).toMatchObject({kind: 'record', recordId: 1234, weight: 0.5});
        expect(parseTermInput('-#1234')).toMatchObject({kind: 'record', recordId: 1234, positive: false});
        expect(parseTermInput('-record:1234:2')).toMatchObject({
            kind: 'record',
            recordId: 1234,
            weight: 2,
            positive: false,
        });
    });

    it('tolerates case and space around the record prefix', () => {
        expect(parseTermInput('Record: 1234')).toMatchObject({kind: 'record', recordId: 1234});
    });

    it('leaves a non-numeric record-ish phrase as text', () => {
        expect(parseTermInput('record:player')).toMatchObject({kind: 'text', text: 'record:player'});
    });
});

describe('termsToRequest', () => {
    const terms: Term[] = [
        {kind: 'text', text: 'catgirl', weight: 0.5, positive: true, enabled: true},
        {kind: 'record', recordId: 12, weight: 2, positive: false, enabled: true},
        {kind: 'text', text: 'skipped', weight: 1, positive: true, enabled: false},
    ];

    it('folds the sign into the weight and drops disabled terms', () => {
        expect(termsToRequest(terms)).toEqual([
            {type: 'text', text: 'catgirl', weight: 0.5},
            {type: 'record', record_id: 12, weight: -2},
        ]);
    });

    it('round-trips its own token shorthand', () => {
        const tokens = termsToTokens(terms);
        expect(tokens).toEqual(['catgirl:0.5', '-record:12:2']);
        expect(tokens.map(parseTermInput)).toMatchObject([
            {kind: 'text', text: 'catgirl', weight: 0.5, positive: true},
            {kind: 'record', recordId: 12, weight: 2, positive: false},
        ]);
    });
});
