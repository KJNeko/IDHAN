/**
 * The chip list behind the search box. Terms are unique: the list is a set, so a term that would
 * duplicate one already in it collapses into that one rather than appearing twice.
 *
 * Kept apart from SearchPanel because editing a chip is the one operation whose result is not
 * obvious from the call: rewriting a term onto one that already exists merges the two.
 */

/** Appends `term` unless the list already carries it. */
export function addTerm(terms: readonly string[], term: string): string[] {
    const trimmed = term.trim();
    if (trimmed.length === 0 || terms.includes(trimmed)) return [...terms];
    return [...terms, trimmed];
}

export function removeTerm(terms: readonly string[], term: string): string[] {
    return terms.filter((existing) => existing !== term);
}

/**
 * Rewrites `original` as `next`, keeping its position. Rewriting it onto a term already in the list
 * drops `original` instead of duplicating that term, and an empty `next` leaves the list alone: the
 * caller cancels an edit rather than committing nothing.
 */
export function replaceTerm(terms: readonly string[], original: string, next: string): string[] {
    const trimmed = next.trim();
    if (trimmed.length === 0) return [...terms];
    if (!terms.includes(original)) return addTerm(terms, trimmed);
    if (trimmed !== original && terms.includes(trimmed)) return removeTerm(terms, original);
    return terms.map((existing) => (existing === original ? trimmed : existing));
}
