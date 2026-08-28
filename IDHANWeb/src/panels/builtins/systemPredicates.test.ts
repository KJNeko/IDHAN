import {describe, expect, it} from 'vitest';
import {
    SYSTEM_PREDICATES,
    matchMimes,
    matchSystemPredicates,
    mimeArgument,
    systemCompletion,
} from './systemPredicates';

const texts = (input: string, limit?: number) => matchSystemPredicates(input, limit).map((p) => p.text);

describe('system predicate matching', () => {
    it('stays quiet for an ordinary tag', () => {
        expect(matchSystemPredicates('samus aran')).toEqual([]);
    });

    it('stays quiet for a token too short to mean system:', () => {
        expect(matchSystemPredicates('sy')).toEqual([]);
    });

    it('opens the catalogue once the token can only be system:', () => {
        expect(texts('sys')).toEqual(SYSTEM_PREDICATES.slice(0, 12).map((p) => p.text));
        expect(texts('system:')).toEqual(SYSTEM_PREDICATES.slice(0, 12).map((p) => p.text));
    });

    it('offers nothing for a negated predicate, which the server would read as a literal tag', () => {
        expect(matchSystemPredicates('-system:width')).toEqual([]);
        expect(matchSystemPredicates('-system:')).toEqual([]);
    });

    it('narrows by name', () => {
        expect(texts('system:wid')).toEqual(['system:width']);
    });

    it('matches interior words, shortest first among equal matches', () => {
        expect(texts('system:tags')).toEqual(['system:no tags', 'system:has tags', 'system:number of tags']);
    });

    it('ranks an exact name above its longer neighbours', () => {
        expect(texts('system:archive')[0]).toBe('system:archive');
    });

    it('matches an alias without listing it as its own row', () => {
        expect(texts('system:untagged')).toEqual(['system:no tags']);
        expect(texts('system:size')).toEqual(['system:filesize']);
    });

    it('stops once the value is being written', () => {
        expect(matchSystemPredicates('system:width > 19')).toEqual([]);
        expect(matchSystemPredicates('system:sha256 = abc')).toEqual([]);
    });

    it('stops once a complete name is followed by a space', () => {
        expect(matchSystemPredicates('system:width ')).toEqual([]);
        expect(matchSystemPredicates('system:no tags ')).toEqual([]);
    });

    it('ignores case and leading whitespace', () => {
        expect(texts('  SYSTEM:WID')).toEqual(['system:width']);
    });

    it('caps the list at the requested limit', () => {
        expect(matchSystemPredicates('system:', 3)).toHaveLength(3);
    });
});

describe('system predicate completion', () => {
    it('commits a flag whole', () => {
        expect(systemCompletion({text: 'system:has audio', kind: 'flag', hint: ''})).toBe('system:has audio');
    });

    it('leaves room for the value after an argument predicate', () => {
        expect(systemCompletion({text: 'system:width', kind: 'argument', hint: ''})).toBe('system:width ');
    });

    it('never completes to a bare argument predicate the server would reject', () => {
        for (const predicate of SYSTEM_PREDICATES) {
            if (predicate.kind === 'argument') expect(systemCompletion(predicate).endsWith(' ')).toBe(true);
        }
    });
});


const CATALOGUE = {
    entries: [
        {id: 1000, name: 'image/jpeg'},
        {id: 1001, name: 'image/png'},
        {id: 5000, name: 'application/zip'},
        {id: 5002, name: 'application/zip'},
    ],
    names: [
        {id: 1000, name: 'image/jpeg'},
        {id: 1001, name: 'image/png'},
        {id: 5000, name: 'application/zip'},
    ],
};

describe('mime argument position', () => {
    it('ignores a term that is not a mime predicate', () => {
        expect(mimeArgument('system:width = 5')).toBeNull();
        expect(mimeArgument('samus')).toBeNull();
    });

    it('ignores a mime predicate with no = yet', () => {
        expect(mimeArgument('system:mime')).toBeNull();
    });

    it('reads the value position of each field', () => {
        expect(mimeArgument('system:mime = zip')).toEqual({field: 'mime', head: 'system:mime =', needle: 'zip'});
        expect(mimeArgument('system:mime_id = 50')).toEqual({
            field: 'mime_id',
            head: 'system:mime_id =',
            needle: '50',
        });
    });

    it('does not mistake mime_id for mime', () => {
        expect(mimeArgument('system:mime_id = 5')?.field).toBe('mime_id');
    });

    it('accepts negation, which the server renders as NOT (...)', () => {
        expect(mimeArgument('system:mime != zip')?.needle).toBe('zip');
    });

    it('rejects an operator the server would not take', () => {
        expect(mimeArgument('system:mime > zip')).toBeNull();
    });

    it('rejects a field name that only starts like mime', () => {
        expect(mimeArgument('system:mimetype = zip')).toBeNull();
    });

    it('keeps earlier list entries in the head so a comma list builds up', () => {
        expect(mimeArgument('system:mime = image/png, ap')).toEqual({
            field: 'mime',
            head: 'system:mime = image/png,',
            needle: 'ap',
        });
    });
});

describe('mime completion', () => {
    it('offers a name once even when several ids report it', () => {
        const argument = mimeArgument('system:mime = zip')!;
        expect(matchMimes(argument, CATALOGUE).map((m) => m.text)).toEqual(['system:mime = application/zip']);
    });

    it('offers every id separately, labelled with the name it reports', () => {
        const argument = mimeArgument('system:mime_id = zip')!;
        expect(matchMimes(argument, CATALOGUE)).toEqual([
            {text: 'system:mime_id = 5000', value: '5000', hint: 'application/zip'},
            {text: 'system:mime_id = 5002', value: '5002', hint: 'application/zip'},
        ]);
    });

    it('matches an id by its leading digits', () => {
        const argument = mimeArgument('system:mime_id = 100')!;
        expect(matchMimes(argument, CATALOGUE).map((m) => m.value)).toEqual(['1000', '1001']);
    });

    it('appends to a comma list rather than replacing it', () => {
        const argument = mimeArgument('system:mime = image/png, zip')!;
        expect(matchMimes(argument, CATALOGUE).map((m) => m.text)).toEqual([
            'system:mime = image/png, application/zip',
        ]);
    });

    it('offers everything while the value is still empty', () => {
        const argument = mimeArgument('system:mime = ')!;
        expect(matchMimes(argument, CATALOGUE)).toHaveLength(CATALOGUE.names.length);
    });
});
