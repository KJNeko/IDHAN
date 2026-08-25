/**
 * The `system:` predicates the server implements (SearchBuilder::setSystemTags), as a completion
 * source for the search box. The set is fixed at build time; nothing here talks to the server.
 *
 * `filetype`, `modified date` and `time imported` are parsed server-side but ignored, so they are
 * left out: completing them would hand back a term that silently matches everything.
 */

import type {MimeCatalogue} from '../../host/mimeCatalogue';

/** A flag is a finished tag on its own. An argument predicate is invalid until a value follows it. */
export type SystemPredicateKind = 'flag' | 'argument';

export interface SystemPredicate {
    /** The full tag text, e.g. `system:width`. */
    text: string;
    kind: SystemPredicateKind;
    /** Right-hand column: the argument shape, or a note separating predicates that read alike. */
    hint: string;
    /** Other spellings the server accepts. Matched, but never shown as rows of their own. */
    aliases?: readonly string[];
}

const PREFIX = 'system:';

/** The shortest bare token that opens the whole list; below this it collides with ordinary tags. */
const MIN_PREFIX = 3;

export const SYSTEM_PREDICATES: readonly SystemPredicate[] = [
    {text: 'system:everything', kind: 'flag', hint: 'every record'},
    {text: 'system:has tags', kind: 'flag', hint: ''},
    {text: 'system:no tags', kind: 'flag', hint: '', aliases: ['untagged']},
    {text: 'system:number of tags', kind: 'argument', hint: '> 5'},
    {text: 'system:width', kind: 'argument', hint: '> 1920'},
    {text: 'system:height', kind: 'argument', hint: '> 1080'},
    {text: 'system:filesize', kind: 'argument', hint: '> 5 MB', aliases: ['size']},
    {text: 'system:has duration', kind: 'flag', hint: ''},
    {text: 'system:no duration', kind: 'flag', hint: ''},
    {text: 'system:has audio', kind: 'flag', hint: ''},
    {text: 'system:no audio', kind: 'flag', hint: ''},
    {text: 'system:has exif', kind: 'flag', hint: ''},
    {text: 'system:no exif', kind: 'flag', hint: ''},
    {text: 'system:has embedded metadata', kind: 'flag', hint: 'exif, xmp or iptc'},
    {text: 'system:no embedded metadata', kind: 'flag', hint: ''},
    {text: 'system:has icc profile', kind: 'flag', hint: ''},
    {text: 'system:no icc profile', kind: 'flag', hint: ''},
    {text: 'system:is archive', kind: 'flag', hint: 'the file is an archive'},
    {text: 'system:is not archive', kind: 'flag', hint: '', aliases: ['not archive']},
    {text: 'system:in archive', kind: 'flag', hint: 'the file sits inside an archive'},
    {text: 'system:not in archive', kind: 'flag', hint: ''},
    {text: 'system:archive', kind: 'argument', hint: '> 2 (archives holding it)'},
    {text: 'system:mime', kind: 'argument', hint: '= application/zip'},
    {text: 'system:mime_id', kind: 'argument', hint: '= 5002'},
    {text: 'system:sha256', kind: 'argument', hint: '= 64 hex characters', aliases: ['hash']},
    {text: 'system:record', kind: 'argument', hint: '> 1000 (record id)'},
    {text: 'system:nearby', kind: 'argument', hint: '1234 distance 8 (perceptual hash)'},
    {text: 'system:limit', kind: 'argument', hint: '= 1000'},
];

/** An operator or a digit: the name is settled and the value is being written. */
const ARGUMENT_CHARS = /[<>=!~≠≈\d]/;

const subtagOf = (predicate: SystemPredicate): string => predicate.text.slice(PREFIX.length);

const namesOf = (predicate: SystemPredicate): string[] => [subtagOf(predicate), ...(predicate.aliases ?? [])];

/** Where a needle may attach: the whole name, or any interior word of it. */
const anchors = (name: string): string[] => {
    const words = name.split(' ');
    return [name, ...words.slice(1)];
};

/** Lower is a better match; null is no match at all. */
function matchRank(predicate: SystemPredicate, needle: string): number | null {
    let best: number | null = null;
    for (const name of namesOf(predicate)) {
        if (name === needle) return 0;
        anchors(name).forEach((anchor, index) => {
            if (anchor.startsWith(needle) && (best === null || index + 1 < best)) best = index + 1;
        });
    }
    return best;
}

const isExactName = (predicate: SystemPredicate, needle: string): boolean =>
    namesOf(predicate).includes(needle);

/**
 * System predicates matching what has been typed so far. Returns every predicate while the token is
 * still just a prefix of `system:`, then narrows by name, then stops once an argument is underway.
 */
export function matchSystemPredicates(input: string, limit = 12): SystemPredicate[] {
    const raw = input.trimStart().toLowerCase();
    const token = raw.trimEnd();

    // The server routes `-system:x` to an ordinary tag lookup, so a negated predicate has no completion.
    if (token.startsWith('-')) return [];

    if (!token.startsWith(PREFIX)) {
        return token.length >= MIN_PREFIX && PREFIX.startsWith(token) ? SYSTEM_PREDICATES.slice(0, limit) : [];
    }

    const argument = raw.slice(PREFIX.length);
    if (ARGUMENT_CHARS.test(argument)) return [];

    const needle = argument.trimEnd();
    if (needle.length === 0) return SYSTEM_PREDICATES.slice(0, limit);

    // A complete name followed by a space: the caret has moved on to the value.
    if (argument.length > needle.length && SYSTEM_PREDICATES.some((p) => isExactName(p, needle))) return [];

    return SYSTEM_PREDICATES.map((predicate) => ({predicate, rank: matchRank(predicate, needle)}))
        .filter((entry): entry is { predicate: SystemPredicate; rank: number } => entry.rank !== null)
        .sort(
            (a, b) =>
                a.rank - b.rank || a.predicate.text.length - b.predicate.text.length ||
                a.predicate.text.localeCompare(b.predicate.text),
        )
        .slice(0, limit)
        .map((entry) => entry.predicate);
}

/** What accepting a suggestion puts in the input; an argument predicate stops short of committing. */
export const systemCompletion = (predicate: SystemPredicate): string =>
    predicate.kind === 'flag' ? predicate.text : `${predicate.text} `;


/** The two predicates whose values come from the mime table rather than being typed free-hand. */
export type MimeField = 'mime' | 'mime_id';

/** Where the caret sits inside a `system:mime = ...` term. */
export interface MimeArgument {
    field: MimeField;
    /** Everything up to and including the `=` or the last `,`, so earlier list entries survive. */
    head: string;
    /** The fragment being typed after that, trimmed. */
    needle: string;
}

/**
 * Reads the value position of a `system:mime`/`system:mime_id` term, or null when the caret is
 * anywhere else. Only `=` and `!=` reach the server, so any other operator yields null.
 */
export function mimeArgument(input: string): MimeArgument | null {
    const raw = input.trimStart();
    const lower = raw.toLowerCase();
    if (!lower.startsWith(PREFIX)) return null;

    const subtag = lower.slice(PREFIX.length);
    const field: MimeField | null = subtag.startsWith('mime_id')
        ? 'mime_id'
        : subtag.startsWith('mime')
            ? 'mime'
            : null;
    if (field === null) return null;

    const equals = raw.indexOf('=');
    if (equals === -1) return null;

    // Everything between the field name and the `=` must be operator punctuation, never a value.
    const operator = subtag.slice(field.length, equals - PREFIX.length);
    if (/[^!≠\s]/.test(operator)) return null;

    const values = raw.slice(equals + 1);
    const comma = values.lastIndexOf(',');

    return {
        field,
        head: raw.slice(0, equals + 1 + (comma === -1 ? 0 : comma + 1)),
        needle: values.slice(comma + 1).trim().toLowerCase(),
    };
}

/** A mime the value position could be completed to. */
export interface MimeSuggestion {
    /** The finished term, ready to commit as a chip. */
    text: string;
    /** The value itself, for display. */
    value: string;
    /** The name a `mime_id` reports. Empty for a name completion, which is already the name. */
    hint: string;
}

/**
 * Mime completions for the value position. `mime` offers each distinct name once, since every id
 * carrying that name matches; `mime_id` offers every id, labelled with the name it reports.
 */
export function matchMimes(argument: MimeArgument, catalogue: MimeCatalogue, limit = 12): MimeSuggestion[] {
    const {field, head, needle} = argument;

    if (field === 'mime') {
        return catalogue.names
            .filter((entry) => entry.name.includes(needle))
            .slice(0, limit)
            .map((entry) => ({text: `${head} ${entry.name}`, value: entry.name, hint: ''}));
    }

    return catalogue.entries
        .filter((entry) => String(entry.id).startsWith(needle) || entry.name.includes(needle))
        .slice(0, limit)
        .map((entry) => ({text: `${head} ${entry.id}`, value: String(entry.id), hint: entry.name}));
}
