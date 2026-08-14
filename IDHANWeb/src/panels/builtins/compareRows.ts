/**
 * The arithmetic behind the Embedding Compare table, kept out of the component so it can be tested
 * without rendering anything.
 *
 * The panel encodes the *difference* between the two distances rather than the distances themselves.
 * With CLIP-style models the text and image towers occupy different regions of the space, so every
 * text-to-image cosine distance lands in a narrow band well away from zero — a bar drawn on the true
 * 0..2 scale would look identical on every row and say nothing. The absolute values are still shown
 * numerically on both sides of the bar, so nothing is hidden by this choice.
 */

import { compareTermLabel, type CompareTerm } from './embeddingTerms';

export interface CompareRow {
  label: string;
  distanceA: number;
  distanceB: number;
  /** distanceA - distanceB. Negative means A is the closer image. */
  delta: number;
}

/** Floor on the bar's scale, so a set of identical deltas cannot divide by zero. */
export const MIN_DELTA_SCALE = 1e-6;

/**
 * Zips the terms that were sent with the matrix that came back.
 *
 * Only indices present on both sides produce a row. The matrix comes from our own request, so a
 * mismatch means the server disagrees with us about what was asked — rendering the rows that do line
 * up is better than discarding a whole response, and far better than pairing a number with the wrong
 * label.
 */
export function buildCompareRows(
  terms: readonly CompareTerm[],
  distances: readonly (readonly number[])[],
): CompareRow[] {
  const rows: CompareRow[] = [];

  for (let index = 0; index < Math.min(terms.length, distances.length); index += 1) {
    const term = terms[index];
    const pair = distances[index];
    if (!term || !pair) continue;

    const [distanceA, distanceB] = pair;
    if (distanceA === undefined || distanceB === undefined) continue;

    rows.push({
      label: compareTermLabel(term),
      distanceA,
      distanceB,
      delta: distanceA - distanceB,
    });
  }

  return rows;
}

/**
 * Rows keyed by the label of the term that produced them.
 *
 * Keying by label rather than by position is what lets one list carry both the terms and their
 * numbers: a term added or removed since the last run shifts every later index, so an index-based
 * lookup would quietly show one term's distance against another term's name.
 */
export function indexRowsByLabel(rows: readonly CompareRow[]): Map<string, CompareRow> {
    return new Map(rows.map((row) => [row.label, row]));
}

export type CompareSortMode =
    | 'entered'
    | 'delta'
    | 'toward-a'
    | 'toward-b'
    | 'closest-a'
    | 'closest-b'
    | 'label';

/** The dropdown's contents, in the order they are offered. */
export const COMPARE_SORT_MODES: ReadonlyArray<{ value: CompareSortMode; label: string }> = [
    {value: 'delta', label: 'Strongest difference'},
    {value: 'toward-a', label: 'Leans toward A'},
    {value: 'toward-b', label: 'Leans toward B'},
    {value: 'closest-a', label: 'Closest to A'},
    {value: 'closest-b', label: 'Closest to B'},
    {value: 'entered', label: 'As entered'},
    {value: 'label', label: 'Alphabetical'},
];

/**
 * How each mode ranks two rows. Returning a negative number puts `left` first.
 *
 * "Leans toward" and "closest to" are deliberately separate: a term can sit close to both images
 * while leaning slightly to one, and can lean hard toward one while being far from both.
 */
const COMPARATORS: Record<
    Exclude<CompareSortMode, 'entered' | 'label'>,
    (left: CompareRow, right: CompareRow) => number
> = {
    delta: (left, right) => Math.abs(right.delta) - Math.abs(left.delta),
    'toward-a': (left, right) => left.delta - right.delta,
    'toward-b': (left, right) => right.delta - left.delta,
    'closest-a': (left, right) => left.distanceA - right.distanceA,
    'closest-b': (left, right) => left.distanceB - right.distanceB,
};

/**
 * The order the term list renders in.
 *
 * Under every result-dependent mode, terms with no row yet — added since the last run — sort last in
 * the order they were typed. Treating a missing result as a zero distance would file them among the
 * terms that genuinely do not discriminate, which is a different and misleading claim.
 *
 * Alphabetical is exempt: it says nothing about results, so an unrun term belongs in its alphabetical
 * place rather than exiled to the bottom.
 */
export function orderTerms(
    terms: readonly CompareTerm[],
    rowsByLabel: ReadonlyMap<string, CompareRow>,
    mode: CompareSortMode,
): CompareTerm[] {
    if (mode === 'entered') return [...terms];

    if (mode === 'label') {
        return [...terms].sort((left, right) => compareTermLabel(left).localeCompare(compareTermLabel(right)));
    }

    const compare = COMPARATORS[mode];

    const scored = terms.map((term, index) => ({term, index, row: rowsByLabel.get(compareTermLabel(term))}));

    scored.sort((left, right) => {
        if (!left.row || !right.row) {
            if (!left.row && !right.row) return left.index - right.index;
            return left.row ? -1 : 1;
        }
        return compare(left.row, right.row) || left.index - right.index;
    });

    return scored.map((entry) => entry.term);
}

/** The largest magnitude among the rows, floored so it is always safe to divide by. */
export function deltaScale(rows: readonly CompareRow[]): number {
  let largest = 0;
  for (const row of rows) largest = Math.max(largest, Math.abs(row.delta));
  return Math.max(largest, MIN_DELTA_SCALE);
}

/** How much of its half of the bar a row fills, in 0..1. */
export function barFraction(delta: number, scale: number): number {
  return Math.min(1, Math.abs(delta) / scale);
}
