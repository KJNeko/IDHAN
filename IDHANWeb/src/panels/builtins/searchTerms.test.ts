import {describe, expect, it} from 'vitest';
import {addTerm, removeTerm, replaceTerm} from './searchTerms';

describe('addTerm', () => {
    it('appends a new term', () => {
        expect(addTerm(['a'], 'b')).toEqual(['a', 'b']);
    });

    it('trims what it stores', () => {
        expect(addTerm([], '  a  ')).toEqual(['a']);
    });

    it('ignores a term already in the list', () => {
        expect(addTerm(['a', 'b'], 'a')).toEqual(['a', 'b']);
    });

    it('ignores an empty term', () => {
        expect(addTerm(['a'], '   ')).toEqual(['a']);
    });

    it('never mutates the list it was given', () => {
        const terms = ['a'];
        addTerm(terms, 'b');
        expect(terms).toEqual(['a']);
    });
});

describe('replaceTerm', () => {
    it('rewrites a term where it sits', () => {
        expect(replaceTerm(['a', 'b', 'c'], 'b', 'z')).toEqual(['a', 'z', 'c']);
    });

    it('keeps the list unchanged when the term is rewritten as itself', () => {
        expect(replaceTerm(['a', 'b'], 'b', 'b')).toEqual(['a', 'b']);
    });

    it('merges into the existing term rather than duplicating it', () => {
        expect(replaceTerm(['a', 'b', 'c'], 'c', 'a')).toEqual(['a', 'b']);
    });

    it('leaves the list alone when the replacement is empty', () => {
        expect(replaceTerm(['a', 'b'], 'a', '  ')).toEqual(['a', 'b']);
    });

    it('adds the replacement when the original is already gone', () => {
        expect(replaceTerm(['a'], 'gone', 'z')).toEqual(['a', 'z']);
    });

    it('trims the replacement', () => {
        expect(replaceTerm(['a'], 'a', ' z ')).toEqual(['z']);
    });
});

describe('removeTerm', () => {
    it('drops the term', () => {
        expect(removeTerm(['a', 'b'], 'a')).toEqual(['b']);
    });

    it('leaves the list alone when the term is absent', () => {
        expect(removeTerm(['a'], 'b')).toEqual(['a']);
    });
});
