export const COMPACT_GAP = 5;

export interface CompactTile {
    index: number;
    x: number;
    y: number;
    width: number;
    height: number;
}

/** Unknown dimensions reserve a square until metadata is available. */
export function imageRatio(width?: number, height?: number): number {
    if (!width || !height || !Number.isFinite(width) || !Number.isFinite(height)) return 1;
    if (width <= 0 || height <= 0) return 1;
    const ratio = width / height;
    return Number.isFinite(ratio) && ratio > 0 ? ratio : 1;
}

/**
 * Equal-width masonry columns: each result goes in the shortest column, ties going left.
 * Columns fill the panel without exceeding the selected maximum width. Search order remains
 * the index order even when an image can fit above a taller neighbor in the visual layout.
 */
export function compactGridLayout(ratios: readonly number[], containerWidth: number, maxWidth: number) {
    const width = Math.max(1, containerWidth);
    const columns = Math.max(1, Math.ceil((width + COMPACT_GAP) / (Math.max(1, maxWidth) + COMPACT_GAP)));
    const tileWidth = (width - (columns - 1) * COMPACT_GAP) / columns;
    const bottoms = new Array<number>(columns).fill(0);
    const tiles: CompactTile[] = [];
    let height = 0;

    ratios.forEach((rawRatio, index) => {
        const ratio = Number.isFinite(rawRatio) && rawRatio > 0 ? rawRatio : 1;
        let column = 0;
        for (let candidate = 1; candidate < columns; candidate++) {
            if (bottoms[candidate]! < bottoms[column]!) column = candidate;
        }
        const tileHeight = tileWidth / ratio;
        const y = bottoms[column]!;
        tiles.push({index, x: column * (tileWidth + COMPACT_GAP), y, width: tileWidth, height: tileHeight});
        height = Math.max(height, y + tileHeight);
        bottoms[column] = y + tileHeight + COMPACT_GAP;
    });

    return {tiles, height};
}
