/**
 * The term model behind the Embedding Search panel, and the shorthand that produces it.
 *
 * Text terms are free text. They are NOT tags and never reach the tag tables — embedding search is a
 * separate system from tag search, so `rating:safe` typed here is a phrase handed to the model, not
 * a namespaced tag.
 */

export interface TextTerm {
  kind: 'text';
  text: string;
  weight: number;
  positive: boolean;
  enabled: boolean;
}

export interface RecordTerm {
  kind: 'record';
  recordId: number;
  weight: number;
  positive: boolean;
  enabled: boolean;
}

export type Term = TextTerm | RecordTerm;

/** `record:1234` or `#1234` — a reference to a record already in the collection. */
const RECORD_REF = /^(?:record:\s*|#)(\d+)$/i;

/**
 * Parses `phrase:weight` shorthand into a term row.
 *
 * The weight is taken only when what follows the FINAL colon parses as a number, so a phrase may
 * contain colons of its own — `character:hatsune miku:0.8` is that phrase at weight 0.8, while
 * `rating:safe` is the whole phrase at the default weight. A leading `-` makes the term negative.
 *
 * `record:1234` and `#1234` produce a reference term instead of a phrase, so the one input box
 * covers both kinds. The record form is matched against the WHOLE string before the weight suffix
 * is stripped: `record:1234` has to mean record 1234, not the phrase `record` at weight 1234.
 * Stripping first and re-matching is what still allows `record:1234:0.5`.
 *
 * Returns null for input with no phrase left in it.
 */
export function parseTermInput(input: string): Term | null {
  let rest = input.trim();
  if (!rest) return null;

  let positive = true;
  if (rest.startsWith('-')) {
    positive = false;
    rest = rest.slice(1).trim();
    if (!rest) return null;
  }

    const bare = rest.match(RECORD_REF);
    if (bare) return {kind: 'record', recordId: Number(bare[1]), weight: 1, positive, enabled: true};

  let weight = 1;
  const lastColon = rest.lastIndexOf(':');

  if (lastColon > 0) {
    const suffix = rest.slice(lastColon + 1).trim();
    // Guarded against empty: Number('') is 0, which would silently eat a trailing colon and set the
    // weight to zero — a term that contributes nothing, with nothing on screen to explain why.
    const parsed = suffix.length > 0 ? Number(suffix) : Number.NaN;

    if (Number.isFinite(parsed)) {
      weight = parsed;
      rest = rest.slice(0, lastColon).trim();
    }
  }

  if (!rest) return null;

    const weighted = rest.match(RECORD_REF);
    if (weighted) return {kind: 'record', recordId: Number(weighted[1]), weight, positive, enabled: true};

  return { kind: 'text', text: rest, weight, positive, enabled: true };
}

/** The `terms` array POST /embeddings/search expects, with the sign folded into the weight. */
export function termsToRequest(terms: readonly Term[]): Array<Record<string, unknown>> {
  return terms
    .filter((term) => term.enabled)
    .map((term) => {
      const weight = term.positive ? term.weight : -term.weight;
      return term.kind === 'text'
        ? { type: 'text', text: term.text, weight }
        : { type: 'record', record_id: term.recordId, weight };
    });
}

/** Human-readable tokens for the results header, in the same shorthand that produces them. */
export function termsToTokens(terms: readonly Term[]): string[] {
  return terms
    .filter((term) => term.enabled)
    .map((term) => {
      const sign = term.positive ? '' : '-';
      const body = term.kind === 'text' ? term.text : `record:${term.recordId}`;
      return term.weight === 1 ? `${sign}${body}` : `${sign}${body}:${term.weight}`;
    });
}
