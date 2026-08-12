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
    const pair = distances[index];
    if (!pair || pair.length < 2) continue;

    rows.push({
      label: compareTermLabel(terms[index]),
      distanceA: pair[0],
      distanceB: pair[1],
      delta: pair[0] - pair[1],
    });
  }

  return rows;
}

/** Strongest difference first, in either direction. Returns a new array. */
export function sortRowsByDelta(rows: readonly CompareRow[]): CompareRow[] {
  return [...rows].sort((left, right) => Math.abs(right.delta) - Math.abs(left.delta));
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
